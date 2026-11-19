#include "live_trial.h"
#include "live_trial_credentials.h"
#include "native_network.h"
#include "settings.h"

#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#if SWIPE_TRIAL_BENCHMARK
#include "display.h"
#endif

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <ctime>

namespace espframe::native {
namespace {

std::atomic<uint32_t> prepared_slides{};
std::atomic<uint32_t> first_photo_ms{};
std::atomic<uint32_t> first_photo_preview_requests{};
std::atomic<uint32_t> swipes{};
std::atomic<uint32_t> last_motion_us{};
std::atomic<uint32_t> prepare_attempts{};
std::atomic<uint32_t> candidate_retries{};
std::atomic<uint32_t> pixel_pauses{};
std::atomic<uint32_t> pixel_pause_ms{};
std::atomic<uint32_t> http_open_errors{};
std::atomic<int> http_last_open_error{};
std::atomic<uint32_t> http_header_timeouts{};
std::atomic<uint32_t> http_body_timeouts{};
std::atomic<uint32_t> photo_candidates{};
std::atomic<uint32_t> pair_candidates{};
std::atomic<uint32_t> preview_requests{};
std::atomic<int> search_http{};
std::atomic<int> pairing_http{};
std::atomic<int> preview_http{};

#if SWIPE_TRIAL_BENCHMARK
TrialSwipeBenchmark benchmark;
portMUX_TYPE benchmark_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t benchmark_status(httpd_req_t *request) {
  portENTER_CRITICAL(&benchmark_lock);
  const auto result = benchmark.results();
  portEXIT_CRITICAL(&benchmark_lock);
  const auto period = Display::nominal_frame_period_us();
  char body[720]{};
  std::snprintf(body, sizeof(body),
                "{\"scripted\":true,\"source\":\"%s\",\"target\":%u,\"started\":%" PRIu32 ",\"settled\":%" PRIu32
                ",\"wrong_slide\":%" PRIu32 ",\"cache_hits\":%" PRIu32
                ",\"dma_copies\":%" PRIu32 ",\"cpu_copies\":%" PRIu32
                ",\"frame_period_us\":%u,\"drag_gap_limit_us\":%u"
                ",\"first_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"compose_and_submit_worst_us\":%" PRIu32
                ",\"busy_first_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"busy_drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}}",
                SWIPE_TRIAL_BENCHMARK_FIXTURES ? "fixtures" : "immich", TrialSwipeBenchmark::swipe_limit,
                result.started, result.settled, result.wrong_slide, result.cache_hits,
                result.dma_copies, result.cpu_copies,
                period, period + period / 2,
                result.first_motion.count, result.first_motion.worst_us, result.first_motion.over_limit,
                result.worst_composition_us,
                result.busy_first_motion.count, result.busy_first_motion.worst_us, result.busy_first_motion.over_limit,
                result.drag_gaps.count, result.drag_gaps.worst_us, result.drag_gaps.over_limit,
                result.busy_drag_gaps.count, result.busy_drag_gaps.worst_us, result.busy_drag_gaps.over_limit);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, body);
}
#endif

template <size_t Size>
bool read_preference(nvs_handle_t storage, const char *key, std::array<uint8_t, Size> &payload) {
  size_t size = payload.size();
  return nvs_get_blob(storage, key, payload.data(), &size) == ESP_OK && size == payload.size();
}

template <class T>
bool read_optional_preference(nvs_handle_t storage, const char *key, T &value) {
  size_t size = sizeof(value);
  const auto result = nvs_get_blob(storage, key, &value, &size);
  return result == ESP_ERR_NVS_NOT_FOUND || (result == ESP_OK && size == sizeof(value));
}

bool read_trial_scope(nvs_handle_t storage, Settings &settings) {
  // ESPHome 2026.11 entity preference keys for the stock V1 build.
  // Fail closed if an active custom filter can't be represented by the trial.
  constexpr const char *unsupported[] = {
      "28127010", "850477587", "3732038753",  // Albums, people, and tags.
      "1146962669", "2437057981", "4116482327"  // Favourites, rating, and location.
  };
  for (const auto *key : unsupported) {
    uint8_t enabled{};
    if (!read_optional_preference(storage, key, enabled) || enabled)
      return false;
  }
  TrialScope scope;
  if (!read_optional_preference(storage, "3905368779", scope.source) ||
      !read_optional_preference(storage, "521046154", scope.orientation) ||
      !read_optional_preference(storage, "612465969", scope.rotation) ||
      !read_optional_preference(storage, "1252659087", scope.pairs_only) ||
      !read_optional_preference(storage, "3141050965", scope.date_enabled))
    return false;
  std::array<uint8_t, 11> from{}, to{};
  if (scope.date_enabled) {
    if (!read_optional_preference(storage, "291203409", scope.date_mode) ||
        !read_optional_preference(storage, "71777625", scope.date_unit) ||
        !read_optional_preference(storage, "674973983", scope.date_amount) ||
        !read_optional_preference(storage, "2159838271", from) ||
        !read_optional_preference(storage, "3290519044", to))
      return false;
    scope.date_from = read_trial_preference(from);
    scope.date_to = read_trial_preference(to);
    if ((from[0] && scope.date_from.empty()) || (to[0] && scope.date_to.empty()))
      return false;
  }
  return scope.apply(settings);
}

esp_err_t status(httpd_req_t *request) {
  const auto &network = *static_cast<const NativeNetwork *>(request->user_ctx);
  const auto prepared = prepared_slides.load(std::memory_order_acquire);
  const auto gestures = swipes.load(std::memory_order_acquire);
  char body[768]{};
  std::snprintf(body, sizeof(body),
                "{\"trial\":\"v1-live\",\"uptime_ms\":%" PRIu32 ",\"prepared_slides\":%" PRIu32
                ",\"first_photo_ms\":%" PRIu32 ",\"first_photo_preview_requests\":%" PRIu32
                ",\"swipes\":%" PRIu32 ",\"last_motion_us\":%" PRIu32
                ",\"clock_ready\":%s,\"photo_connection\":%s,\"prepare_attempts\":%" PRIu32
                ",\"candidate_retries\":%" PRIu32 ",\"scripted_gestures\":%s"
                ",\"pixel_pauses\":%" PRIu32 ",\"pixel_pause_ms\":%" PRIu32
                ",\"http_open_errors\":%" PRIu32 ",\"http_last_open_error\":%d"
                ",\"http_header_timeouts\":%" PRIu32 ",\"http_body_timeouts\":%" PRIu32
                ",\"search_http\":%d,\"photo_candidates\":%" PRIu32 ",\"pairing_http\":%d,\"pair_candidates\":%" PRIu32
                ",\"preview_http\":%d,\"preview_requests\":%" PRIu32 "}",
                static_cast<uint32_t>(esp_timer_get_time() / 1000), prepared,
                first_photo_ms.load(std::memory_order_relaxed),
                first_photo_preview_requests.load(std::memory_order_relaxed),
                gestures, last_motion_us.load(std::memory_order_relaxed),
                clock_time_is_valid(std::time(nullptr)) ? "true" : "false",
                network.photo_connection() ? "true" : "false", prepare_attempts.load(std::memory_order_relaxed),
                candidate_retries.load(std::memory_order_relaxed), SWIPE_TRIAL_BENCHMARK ? "true" : "false",
                pixel_pauses.load(std::memory_order_relaxed), pixel_pause_ms.load(std::memory_order_relaxed),
                http_open_errors.load(std::memory_order_relaxed), http_last_open_error.load(std::memory_order_relaxed),
                http_header_timeouts.load(std::memory_order_relaxed), http_body_timeouts.load(std::memory_order_relaxed),
                search_http.load(std::memory_order_relaxed), photo_candidates.load(std::memory_order_relaxed),
                pairing_http.load(std::memory_order_relaxed), pair_candidates.load(std::memory_order_relaxed),
                preview_http.load(std::memory_order_relaxed), preview_requests.load(std::memory_order_relaxed));
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, body);
}

}  // namespace

bool read_live_trial_credentials(Settings &settings) {
  // Use the same partition as the stock app and open only its read handle.
  // Initialization failure aborts the trial without trying to erase it.
  nvs_handle_t storage;
  if (nvs_flash_init_partition("nvs") != ESP_OK ||
      nvs_open_from_partition("nvs", "esphome", NVS_READONLY, &storage) != ESP_OK)
    return false;
  std::array<uint8_t, esphome_station_bytes> station{};
  std::array<uint8_t, esphome_url_bytes> url{};
  std::array<uint8_t, esphome_api_key_bytes> key{};
  const bool found = read_preference(storage, esphome_station_key, station) &&
                     read_preference(storage, esphome_immich_url_key, url) &&
                     read_preference(storage, esphome_immich_api_key, key) &&
                     read_trial_scope(storage, settings);
  nvs_close(storage);
  const auto wifi = read_trial_station(station);
  const auto immich_url = read_trial_preference(url);
  const auto immich_key = read_trial_preference(key);
  if (!found || !wifi || immich_url.empty() || immich_key.empty())
    return false;
  settings.ssid = wifi->ssid.data();
  settings.password = wifi->password.data();
  settings.immich_url = immich_url;
  settings.immich_key = immich_key;
  return true;
}

bool start_live_trial_status(NativeNetwork &network) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.core_id = 0;
  config.max_uri_handlers = SWIPE_TRIAL_BENCHMARK ? 2 : 1;
  httpd_handle_t server{};
  if (httpd_start(&server, &config) != ESP_OK)
    return false;
  httpd_uri_t route{};
  route.uri = "/api/trial";
  route.method = HTTP_GET;
  route.handler = status;
  route.user_ctx = &network;
  auto result = httpd_register_uri_handler(server, &route);
#if SWIPE_TRIAL_BENCHMARK
  if (result == ESP_OK) {
    route.uri = "/api/trial/benchmark";
    route.handler = benchmark_status;
    route.user_ctx = nullptr;
    result = httpd_register_uri_handler(server, &route);
  }
#endif
  if (result == ESP_OK)
    return true;
  httpd_stop(server);
  return false;
}

void record_live_trial_prepare() { prepare_attempts.fetch_add(1, std::memory_order_relaxed); }

void record_live_trial_candidate_retry() { candidate_retries.fetch_add(1, std::memory_order_relaxed); }

void record_live_trial_pixel_pause(uint32_t pause_ms) {
  pixel_pause_ms.fetch_add(pause_ms, std::memory_order_relaxed);
  pixel_pauses.fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_http_open_error(int error) {
  http_last_open_error.store(error, std::memory_order_relaxed);
  http_open_errors.fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_http_read_timeout(bool headers) {
  (headers ? http_header_timeouts : http_body_timeouts).fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_search(bool pairing, int http_status, size_t candidates) {
  (pairing ? pairing_http : search_http).store(http_status, std::memory_order_relaxed);
  (pairing ? pair_candidates : photo_candidates).store(candidates, std::memory_order_relaxed);
}

void record_live_trial_preview(int http_status) {
  preview_http.store(http_status, std::memory_order_relaxed);
  preview_requests.fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_photo(uint32_t elapsed_ms) {
  if (prepared_slides.load(std::memory_order_relaxed) == 0) {
    first_photo_ms.store(elapsed_ms, std::memory_order_relaxed);
    first_photo_preview_requests.store(preview_requests.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }
  prepared_slides.fetch_add(1, std::memory_order_release);
}

void record_live_trial_gesture(uint32_t motion_us) {
  last_motion_us.store(motion_us, std::memory_order_relaxed);
  swipes.fetch_add(1, std::memory_order_release);
}

#if SWIPE_TRIAL_BENCHMARK
std::optional<TouchReport> poll_live_trial_benchmark(int64_t now, SlideIndex center, SlideRange ready, bool at_rest) {
  portENTER_CRITICAL(&benchmark_lock);
  const auto report = benchmark.poll(now, center, ready, at_rest);
  portEXIT_CRITICAL(&benchmark_lock);
  return report;
}

void record_live_trial_benchmark_gesture(uint32_t motion_us, const FirstMotionTrace &motion) {
  portENTER_CRITICAL(&benchmark_lock);
  benchmark.gesture(motion_us, static_cast<uint32_t>(motion.composed_us), motion.preparing,
                    motion.cache.result == CacheRead::Hit);
  portEXIT_CRITICAL(&benchmark_lock);
}

void record_live_trial_benchmark_frame(int64_t latched_us, MotionStamp stamp, uint32_t period_us) {
  portENTER_CRITICAL(&benchmark_lock);
  benchmark.frame(latched_us, stamp, period_us);
  portEXIT_CRITICAL(&benchmark_lock);
}

void record_live_trial_benchmark_blit(bool dma) {
  portENTER_CRITICAL(&benchmark_lock);
  benchmark.blit(dma);
  portEXIT_CRITICAL(&benchmark_lock);
}
#endif

}  // namespace espframe::native
