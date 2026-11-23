#include "live_trial.h"
#if SWIPE_LINKED_SCANOUT
#include "linked_dpi.h"
#endif
#include "stock_frame_identity.h"
#include "display.h"
#include "frame_blitter.h"
#include "live_trial_credentials.h"
#include "native_network.h"
#include "settings.h"
#include "trial_swipe_benchmark.h"
#include "trial_swipe_landing.h"
#include "trial_replay.h"

#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <memory>
#include <new>

namespace espframe::native {
namespace {

std::atomic<uint32_t> prepared_slides{};
std::atomic<uint32_t> thumbnail_landscapes{};
std::atomic<uint32_t> thumbnail_pairs{};
std::atomic<uint32_t> thumbnail_promotions{};
std::atomic<uint32_t> detail_presentations{};
std::atomic<int32_t> last_detail_slide{-1};
std::atomic<uint32_t> last_detail_latency_ms{};
std::atomic<int32_t> last_aligned_touch_slide{-1};
std::atomic<unsigned> cache_slots{};
std::atomic<unsigned> ready_ahead{};
std::atomic<uint32_t> first_photo_ms{};
std::atomic<uint32_t> first_photo_preview_requests{};
std::atomic<uint32_t> swipes{};
std::atomic<uint32_t> last_motion_us{};
std::atomic<uint32_t> prepare_attempts{};
std::atomic<uint32_t> candidate_retries{};
std::atomic<uint32_t> pixel_pauses{};
std::atomic<uint32_t> pixel_pause_us{};
std::atomic<uint32_t> http_open_errors{};
std::atomic<int> http_last_open_error{};
std::atomic<uint32_t> http_header_timeouts{};
std::atomic<uint32_t> http_body_timeouts{};
std::atomic<uint32_t> photo_candidates{};
std::atomic<uint32_t> pair_candidates{};
std::atomic<unsigned> pairing_range_days{};
std::atomic<uint32_t> preview_requests{};
std::atomic<int> search_http{};
std::atomic<int> pairing_http{};
std::atomic<int> preview_http{};
std::atomic<int> wifi_power_save{-1};
std::atomic<int> wifi_rssi_dbm{-128};
std::atomic<unsigned> wifi_primary_channel{};

struct PreparationTiming {
  uint32_t count{};
  uint32_t total_us{};
  uint32_t worst_us{};
  size_t bytes{};
  uint32_t checkpoint_us{};
};

constexpr std::array preparation_stages{"attempt", "selection", "transfer", "hardware_jpeg", "software_jpeg",
                                        "webp", "rejected_preview", "composition", "prepared_transfer", "prepared_decode",
                                        "prepared_cache_decode", "prefetch_transfer", "prefetch_wait", "prepared_warm",
                                        "progressive_jpeg_input", "progressive_jpeg_output", "thumbnail_transfer",
                                        "thumbnail_prefetch_transfer", "thumbnail_prefetch_wait"};
static_assert(preparation_stages.size() == static_cast<size_t>(TrialPreparationStage::Count));
std::array<PreparationTiming, preparation_stages.size()> preparation_timings{};
struct ReplaySlides {
  uint32_t paired{};
  uint32_t single{};
  uint32_t pair_fill_edge_failures{};
};
ReplaySlides replay_slides;
portMUX_TYPE preparation_lock = portMUX_INITIALIZER_UNLOCKED;
bool preparation_finished{};

#if SWIPE_TRIAL_BENCHMARK
constexpr auto benchmark_scenario = SWIPE_TRIAL_WIFI_OUTAGE ? TrialSwipeBenchmark::Scenario::WifiOutage
                                  : SWIPE_TRIAL_REPLAY_HISTORY ? TrialSwipeBenchmark::Scenario::History
                                  : SWIPE_TRIAL_BENCHMARK_RAPID ? TrialSwipeBenchmark::Scenario::Rapid
                                                               : TrialSwipeBenchmark::Scenario::Normal;
TrialSwipeBenchmark benchmark{benchmark_scenario};
std::optional<TrialSwipeMetrics::Results> completed_replay_metrics;
#endif
TrialSwipeMetrics metrics;
portMUX_TYPE swipe_lock = portMUX_INITIALIZER_UNLOCKED;

#if !SWIPE_TRIAL_BENCHMARK
struct PhysicalDragCounts {
  uint32_t forward{};
  uint32_t backward{};
  uint32_t returned{};
  uint32_t cancelled{};
  uint32_t interrupted{};
};
PhysicalDragCounts physical_drags;
#endif

esp_err_t benchmark_status(httpd_req_t *request) {
  portENTER_CRITICAL(&swipe_lock);
#if SWIPE_TRIAL_BENCHMARK
  const auto result = completed_replay_metrics.value_or(metrics.results());
  const auto script = benchmark.results();
#else
  const auto result = metrics.results();
  const auto drags = physical_drags;
#endif
  portEXIT_CRITICAL(&swipe_lock);

  char gesture_counts[440]{};
#if SWIPE_TRIAL_BENCHMARK
  std::snprintf(gesture_counts, sizeof(gesture_counts),
                ",\"target\":%u,\"started\":%" PRIu32 ",\"settled\":%" PRIu32
                ",\"settled_slide\":%" PRId64 ",\"wrong_slide\":%" PRIu32,
                benchmark.target(), script.started, script.settled, script.settled_slide, script.wrong_slide);
#else
  std::snprintf(gesture_counts, sizeof(gesture_counts),
                ",\"physical_drags\":{\"forward\":%" PRIu32 ",\"backward\":%" PRIu32 ",\"returned\":%" PRIu32
                ",\"cancelled\":%" PRIu32 ",\"interrupted\":%" PRIu32 "}",
                drags.forward, drags.backward, drags.returned, drags.cancelled, drags.interrupted);
#endif
#if SWIPE_TRIAL_BENCHMARK_RAPID
  const auto used = std::strlen(gesture_counts);
  std::snprintf(gesture_counts + used, sizeof(gesture_counts) - used,
                ",\"scenario\":\"%s\",\"cold_starts\":%" PRIu32 ",\"cold_releases_unready\":%" PRIu32
                ",\"cold_release_advances\":%" PRIu32 ",\"cold_returns\":%" PRIu32 ",\"elapsed_us\":%" PRIu32
                ",\"refill_limit_us\":%" PRIu32
                ",\"refill\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}",
                SWIPE_TRIAL_REPLAY_HISTORY ? "history" : "rapid",
                script.cold_starts, script.cold_releases_unready, script.cold_release_advances, script.cold_returns,
                script.elapsed_us, TrialSwipeBenchmark::refill_limit_us,
                script.refill.count, script.refill.worst_us, script.refill.over_limit);
#endif
#if SWIPE_TRIAL_WIFI_OUTAGE
  const auto link = static_cast<const NativeNetwork *>(request->user_ctx)->trial_link();
  constexpr std::array link_label{"ready", "requested", "disconnecting", "offline", "reconnecting", "reconnected", "failed"};
  const auto used = std::strlen(gesture_counts);
  std::snprintf(gesture_counts + used, sizeof(gesture_counts) - used,
                ",\"scenario\":\"wifi-outage\",\"wifi_phase\":\"%s\",\"offline_settled\":%" PRIu32
                ",\"reconnected_settled\":%" PRIu32 ",\"offline_ready_last\":%" PRId64,
                link_label[static_cast<size_t>(link)], script.offline_settled, script.reconnected_settled,
                script.offline_ready_last);
#endif
  const auto period = Display::nominal_frame_period_us();
  char replay[240]{};
  if constexpr (direct_trial_replay) {
    std::snprintf(replay, sizeof(replay),
                  ",\"direct_replay_lookahead\":%s,\"direct_replay_companion_prefetch\":%s,\"replay_pair_fill\":%s"
                  ",\"replay_origin\":\"%s\"%s%s%s",
                  SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD ? "false" : "true",
                  prefetch_portrait_companion ? "true" : "false", trial_replay_pair_fill ? "true" : "false",
                  trial_replay_origin.data(), trial_replay_staged_detail ? ",\"replay_staged_detail\":true" : "",
                  SWIPE_TRIAL_REPLAY_EDGE_DETAIL ? ",\"replay_edge_detail\":true" : "",
                  trial_replay_touch ? ",\"replay_touch\":true" : "");
  } else if constexpr (synthetic_trial_replay) {
    std::snprintf(replay, sizeof(replay), ",\"replay_prepared\":true,\"replay_pair_fill\":%s,\"replay_origin\":\"%s\"%s",
                  trial_replay_pair_fill ? "true" : "false", trial_replay_origin.data(),
                  SWIPE_TRIAL_REPLAY_EDGE_DETAIL ? ",\"replay_edge_detail\":true" : "");
  } else if constexpr (live_trial_edge_detail) {
    std::snprintf(replay, sizeof(replay), ",\"live_edge_detail\":true");
  }
  // Keep the full diagnostics response off the small HTTP task stack.
#if SWIPE_LINKED_SCANOUT
  espframe_dpi_report_t dpi{};
  espframe_dpi_read_report(&dpi);
  char linked_report[320]{};
  std::snprintf(linked_report, sizeof(linked_report),
                ",\"dpi_refreshes\":%" PRIu32 ",\"dpi_underruns\":%" PRIu32
                ",\"borrowed_submits\":%" PRIu32 ",\"borrowed_worst_us\":%" PRIu32
                ",\"borrowed_total_us\":%" PRIu32 ",\"borrowed_band_bytes\":%" PRIu32
                ",\"borrowed_edge_submits\":%" PRIu32 ",\"borrowed_edge_total_us\":%" PRIu32
                ",\"borrowed_edge_worst_us\":%" PRIu32 ",\"borrowed_edge_max_bytes\":%" PRIu32,
                dpi.refreshes, dpi.underruns, dpi.submits, dpi.worst_us, dpi.total_us, dpi.painted_bytes,
                dpi.edge_submits, dpi.edge_total_us, dpi.edge_worst_us, dpi.edge_max_bytes);
#else
  constexpr char linked_report[] = "";
#endif
  constexpr size_t report_capacity = 3200;
  const std::unique_ptr<char[]> body{new (std::nothrow) char[report_capacity]};
  if (!body)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not allocate swipe report");
  const auto length = std::snprintf(body.get(), report_capacity,
                "{\"scripted\":%s,\"source\":\"%s\"%s%s,\"cache_hits\":%" PRIu32
                ",\"dma_burst_bytes\":%u,\"dma_copies\":%" PRIu32 ",\"cpu_copies\":%" PRIu32
                ",\"split_blits\":{\"count\":%" PRIu32 ",\"batched\":%" PRIu32 ",\"total_us\":%" PRIu64
                ",\"worst_us\":%" PRIu32 "}"
                "%s,\"frame_period_us\":%u,\"drag_gap_limit_us\":%u"
                ",\"first_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"cache_miss_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"quiet_cache_hit_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"busy_cache_hit_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"compose_and_submit_worst_us\":%" PRIu32
                ",\"before_submit_worst_us\":%" PRIu32 ",\"submit_worst_us\":%" PRIu32
                ",\"busy_first_motion\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_50_ms\":%" PRIu32 "}"
                ",\"drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"quiet_drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"busy_drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"in_range_drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"resisted_drag_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"settle_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"in_range_settle_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"resisted_settle_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"landing_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"approach_gaps\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"in_range_drag_updates\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"resisted_drag_updates\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_limit\":%" PRIu32 "}"
                ",\"drag_handoff_delay\":{\"count\":%" PRIu32 ",\"worst_us\":%" PRIu32 ",\"over_250_us\":%" PRIu32 "}}",
                SWIPE_TRIAL_BENCHMARK ? "true" : "false", SWIPE_TRIAL_BENCHMARK_FIXTURES ? "fixtures" : "immich",
                gesture_counts, replay, result.cache_hits,
                FrameBlitter::burst_bytes, result.dma_copies, result.cpu_copies,
                result.split_blits, result.batched_split_blits, result.total_split_blit_us,
                result.worst_split_blit_us,
                linked_report, period, period + period / 2,
                result.first_motion.count, result.first_motion.worst_us, result.first_motion.over_limit,
                result.cache_miss_motion.count, result.cache_miss_motion.worst_us, result.cache_miss_motion.over_limit,
                result.quiet_cache_hit_motion.count, result.quiet_cache_hit_motion.worst_us,
                result.quiet_cache_hit_motion.over_limit,
                result.busy_cache_hit_motion.count, result.busy_cache_hit_motion.worst_us,
                result.busy_cache_hit_motion.over_limit,
                result.worst_composition_us, result.worst_before_submit_us, result.worst_submit_us,
                result.busy_first_motion.count, result.busy_first_motion.worst_us, result.busy_first_motion.over_limit,
                result.drag_gaps.count, result.drag_gaps.worst_us, result.drag_gaps.over_limit,
                result.quiet_drag_gaps.count, result.quiet_drag_gaps.worst_us, result.quiet_drag_gaps.over_limit,
                result.busy_drag_gaps.count, result.busy_drag_gaps.worst_us, result.busy_drag_gaps.over_limit,
                result.in_range_drag_gaps.count, result.in_range_drag_gaps.worst_us, result.in_range_drag_gaps.over_limit,
                result.resisted_drag_gaps.count, result.resisted_drag_gaps.worst_us, result.resisted_drag_gaps.over_limit,
                result.settle_gaps.count, result.settle_gaps.worst_us, result.settle_gaps.over_limit,
                result.in_range_settle_gaps.count, result.in_range_settle_gaps.worst_us, result.in_range_settle_gaps.over_limit,
                result.resisted_settle_gaps.count, result.resisted_settle_gaps.worst_us, result.resisted_settle_gaps.over_limit,
                result.landing_gaps.count, result.landing_gaps.worst_us, result.landing_gaps.over_limit,
                result.approach_gaps.count, result.approach_gaps.worst_us, result.approach_gaps.over_limit,
                result.in_range_drag_updates.count, result.in_range_drag_updates.worst_us, result.in_range_drag_updates.over_limit,
                result.resisted_drag_updates.count, result.resisted_drag_updates.worst_us, result.resisted_drag_updates.over_limit,
                result.drag_handoff_delay.count, result.drag_handoff_delay.worst_us, result.drag_handoff_delay.over_limit);
  if (length < 0 || static_cast<size_t>(length) >= report_capacity)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not format swipe timings");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, body.get(), length);
}

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
      !read_optional_preference(storage, "136111996", scope.pairing_enabled) ||
      !read_optional_preference(storage, "1252659087", scope.pairs_only) ||
      !read_optional_preference(storage, "1684562378", scope.pairing_range) ||
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
    scope.date_from = read_stock_preference_text(from);
    scope.date_to = read_stock_preference_text(to);
    if ((from[0] && scope.date_from.empty()) || (to[0] && scope.date_to.empty()))
      return false;
  }
  if (!scope.apply(settings))
    return false;
  pairing_range_days.store(settings.portrait_pairing_days, std::memory_order_relaxed);
  return true;
}

template <class T>
T read_presentation_preference(nvs_handle_t storage, const char *key, T fallback) {
  auto value = fallback;
  return read_optional_preference(storage, key, value) ? value : fallback;
}

void read_trial_presentation(nvs_handle_t storage, Settings &settings) {
  // Read the display entities from the ESPHome 2026.11 V1 build.
  settings.clock.show = read_presentation_preference<uint8_t>(storage, "2773437584", 1) != 0;
  settings.clock.format = read_presentation_preference<uint32_t>(storage, "350654562", 0) == 1 ? "12 Hour" : "24 Hour";
  const auto timezone = read_presentation_preference<uint32_t>(storage, "3832611266", 43);
  const auto zones = clock_zones();
  if (timezone < zones.size())
    settings.clock.timezone = zones[timezone].option;
  settings.metadata.show_date = read_presentation_preference<uint8_t>(storage, "2745824589", 1) != 0;
  settings.metadata.show_location = read_presentation_preference<uint8_t>(storage, "3081973308", 1) != 0;
  settings.metadata.relative = read_presentation_preference<uint32_t>(storage, "3825872665", 1) == 0;
  settings.metadata.month_before_day = read_presentation_preference<uint32_t>(storage, "1950842049", 0) == 1;
  settings.display_mode = read_presentation_preference<uint32_t>(storage, "3596795592", 0) == 1 ? DisplayMode::Fit
                                                                                                : DisplayMode::Fill;
  settings.pair_framing = PairFraming::Fill;
}

constexpr char trial_page_html[] = R"html(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Espframe test build</title>
<style>
  :root { color-scheme: light dark; font: 1rem/1.55 system-ui, sans-serif; }
  main { max-width: 34rem; margin: 10vh auto; padding: 0 1rem; }
  h1 { font-size: 1.75rem; line-height: 1.2; }
  dt { font-weight: 600; }
  dd { margin: 0 0 0.75rem; }
</style>
<main>
  <h1>Espframe test build</h1>
  <dl>
    <dt>Immich connection</dt><dd>%s</dd>
    <dt>Photos ready ahead</dt><dd>%u</dd>
  </dl>
  <p>Restart the frame to return to ESPHome and open its settings.</p>
  <p><a href="/api/trial">Trial diagnostics</a></p>
</main>
</html>)html";

esp_err_t trial_page(httpd_req_t *request) {
  const auto &network = *static_cast<const NativeNetwork *>(request->user_ctx);
  char body[sizeof(trial_page_html) + 32]{};
  const auto size = std::snprintf(body, sizeof(body), trial_page_html,
                                  network.photo_connection() ? "Connected" : "Waiting for Immich",
                                  ready_ahead.load(std::memory_order_relaxed));
  if (size < 0 || static_cast<size_t>(size) >= sizeof(body))
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not format trial page");
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'");
  return httpd_resp_send(request, body, size);
}

esp_err_t status(httpd_req_t *request) {
  const auto &network = *static_cast<const NativeNetwork *>(request->user_ctx);
  const auto prepared = prepared_slides.load(std::memory_order_acquire);
  const auto gestures = swipes.load(std::memory_order_acquire);
  char presentation[240]{};
  if constexpr (trial_detail_metrics) {
    const auto presentations = detail_presentations.load(std::memory_order_acquire);
    const auto pairs = thumbnail_pairs.load(std::memory_order_relaxed);
    const auto landscapes = thumbnail_landscapes.load(std::memory_order_relaxed);
    std::snprintf(presentation, sizeof(presentation),
                  ",\"thumbnail_slides\":%" PRIu32 ",\"thumbnail_pairs\":%" PRIu32
                  ",\"thumbnail_promotions\":%" PRIu32 ",\"detail_presentations\":%" PRIu32
                  ",\"last_detail_slide\":%" PRId32 ",\"last_detail_latency_ms\":%" PRIu32,
                  pairs + landscapes, pairs, thumbnail_promotions.load(std::memory_order_relaxed), presentations,
                  last_detail_slide.load(std::memory_order_relaxed), last_detail_latency_ms.load(std::memory_order_relaxed));
  }
  if constexpr (trial_replay_touch) {
    const auto used = std::strlen(presentation);
    std::snprintf(presentation + used, sizeof(presentation) - used,
                  ",\"last_aligned_slide\":%" PRId32, last_aligned_touch_slide.load(std::memory_order_relaxed));
  }
  char body[1152]{};
  const auto length = std::snprintf(body, sizeof(body),
                "{\"trial\":\"v1-live\",\"uptime_ms\":%" PRIu32 ",\"prepared_slides\":%" PRIu32
                ",\"cache_slots\":%u,\"ready_ahead\":%u"
                ",\"first_photo_ms\":%" PRIu32 ",\"first_photo_preview_requests\":%" PRIu32
                ",\"swipes\":%" PRIu32 ",\"last_motion_us\":%" PRIu32
                ",\"clock_ready\":%s,\"photo_connection\":%s,\"prepare_attempts\":%" PRIu32
                ",\"wifi_power_save\":%d,\"wifi_rssi_dbm\":%d,\"wifi_primary_channel\":%u"
                ",\"candidate_retries\":%" PRIu32 ",\"scripted_gestures\":%s"
                ",\"pixel_pauses\":%" PRIu32 ",\"pixel_pause_ms\":%" PRIu32
                ",\"http_open_errors\":%" PRIu32 ",\"http_last_open_error\":%d"
                ",\"http_header_timeouts\":%" PRIu32 ",\"http_body_timeouts\":%" PRIu32
                ",\"search_http\":%d,\"photo_candidates\":%" PRIu32 ",\"pairing_http\":%d,\"pair_candidates\":%" PRIu32
                ",\"pairing_range_days\":%u"
                ",\"preview_http\":%d,\"preview_requests\":%" PRIu32 "%s}",
                static_cast<uint32_t>(esp_timer_get_time() / 1000), prepared,
                cache_slots.load(std::memory_order_relaxed), ready_ahead.load(std::memory_order_relaxed),
                first_photo_ms.load(std::memory_order_relaxed),
                first_photo_preview_requests.load(std::memory_order_relaxed),
                gestures, last_motion_us.load(std::memory_order_relaxed),
                clock_time_is_valid(std::time(nullptr)) ? "true" : "false",
                network.photo_connection() ? "true" : "false", prepare_attempts.load(std::memory_order_relaxed),
                wifi_power_save.load(std::memory_order_relaxed), wifi_rssi_dbm.load(std::memory_order_relaxed),
                wifi_primary_channel.load(std::memory_order_relaxed),
                candidate_retries.load(std::memory_order_relaxed), SWIPE_TRIAL_BENCHMARK ? "true" : "false",
                pixel_pauses.load(std::memory_order_relaxed), live_trial_pixel_pause_us() / 1000,
                http_open_errors.load(std::memory_order_relaxed), http_last_open_error.load(std::memory_order_relaxed),
                http_header_timeouts.load(std::memory_order_relaxed), http_body_timeouts.load(std::memory_order_relaxed),
                search_http.load(std::memory_order_relaxed), photo_candidates.load(std::memory_order_relaxed),
                pairing_http.load(std::memory_order_relaxed), pair_candidates.load(std::memory_order_relaxed),
                pairing_range_days.load(std::memory_order_relaxed),
                preview_http.load(std::memory_order_relaxed), preview_requests.load(std::memory_order_relaxed), presentation);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(body))
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not format trial status");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, body);
}

esp_err_t preparation_status(httpd_req_t *request) {
  portENTER_CRITICAL(&preparation_lock);
  const auto timings = preparation_timings;
  [[maybe_unused]] const auto slides = replay_slides;
  portEXIT_CRITICAL(&preparation_lock);

  // Keep the expanded timing response off the HTTP task stack.
  constexpr size_t report_capacity = 2400;
  const std::unique_ptr<char[]> body{new (std::nothrow) char[report_capacity]};
  if (!body)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not allocate preparation report");
  body[0] = '{';
  size_t used = 1;
  if constexpr (synthetic_trial_replay) {
    const auto remaining = report_capacity - used;
    const auto written = std::snprintf(body.get() + used, remaining,
                                       "\"new_slides\":{\"paired\":%" PRIu32 ",\"single\":%" PRIu32
                                       ",\"pair_fill_edge_failures\":%" PRIu32 "},",
                                       slides.paired, slides.single, slides.pair_fill_edge_failures);
    if (written < 0 || static_cast<size_t>(written) >= remaining - 1)
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Preparation report too large");
    used += written;
  }
  for (size_t i = 0; i < timings.size(); ++i) {
    const auto &timing = timings[i];
    char pause_field[40]{};
    if (i == static_cast<size_t>(TrialPreparationStage::ProgressiveJpegInput) ||
        i == static_cast<size_t>(TrialPreparationStage::ProgressiveJpegOutput))
      std::snprintf(pause_field, sizeof(pause_field), ",\"checkpoint_us\":%" PRIu32, timing.checkpoint_us);
    const auto remaining = report_capacity - used;
    const auto written = std::snprintf(body.get() + used, remaining,
                                      "%s\"%s\":{\"count\":%" PRIu32 ",\"total_us\":%" PRIu32
                                      ",\"worst_us\":%" PRIu32 ",\"bytes\":%zu%s}",
                                      i == 0 ? "" : ",", preparation_stages[i], timing.count, timing.total_us,
                                      timing.worst_us, timing.bytes, pause_field);
    if (written < 0 || static_cast<size_t>(written) >= remaining - 1)
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Preparation report too large");
    used += written;
  }
  body[used++] = '}';
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, body.get(), used);
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
  if constexpr (!SWIPE_TRIAL_BENCHMARK) {
    if (found)
      read_trial_presentation(storage, settings);
  }
  nvs_close(storage);
  const auto wifi = read_stock_station(station);
  const auto immich_url = read_stock_preference_text(url);
  const auto immich_key = read_stock_preference_text(key);
  if (!found || !wifi || immich_url.empty() || immich_key.empty())
    return false;
  settings.ssid = wifi->ssid.data();
  settings.password = wifi->password.data();
  settings.immich_url = immich_url;
  settings.immich_key = immich_key;
  if constexpr (!SWIPE_TRIAL_BENCHMARK)
    settings.frame_name = read_stock_frame_name("nvs").value_or(std::string{});
  return true;
}

bool start_live_trial_status(NativeNetwork &network) {
  const httpd_uri_t routes[]{
      {.uri = "/", .method = HTTP_GET, .handler = trial_page, .user_ctx = &network},
      {.uri = "/api/trial", .method = HTTP_GET, .handler = status, .user_ctx = &network},
      {.uri = "/api/trial/benchmark", .method = HTTP_GET, .handler = benchmark_status, .user_ctx = &network},
      {.uri = "/api/trial/prepare", .method = HTTP_GET, .handler = preparation_status, .user_ctx = &network},
  };
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.core_id = 0;
  config.max_uri_handlers = std::size(routes);
  httpd_handle_t server{};
  if (httpd_start(&server, &config) != ESP_OK)
    return false;
  for (const auto &route : routes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server);
      return false;
    }
  }
  return true;
}

void record_live_trial_wifi_link(int power_save, int rssi, unsigned channel) {
  wifi_power_save.store(power_save, std::memory_order_relaxed);
  wifi_rssi_dbm.store(rssi, std::memory_order_relaxed);
  wifi_primary_channel.store(channel, std::memory_order_relaxed);
}

void record_live_trial_prepare() { prepare_attempts.fetch_add(1, std::memory_order_relaxed); }

void record_live_trial_candidate_retry() { candidate_retries.fetch_add(1, std::memory_order_relaxed); }

void record_live_trial_pixel_pause(uint32_t pause_us) {
  pixel_pause_us.fetch_add(pause_us, std::memory_order_relaxed);
  pixel_pauses.fetch_add(1, std::memory_order_relaxed);
}

uint32_t live_trial_pixel_pause_us() {
  return pixel_pause_us.load(std::memory_order_relaxed);
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

static void add_preparation_time(TrialPreparationStage stage, uint32_t elapsed_us, size_t bytes, uint32_t checkpoint_us) {
  auto &timing = preparation_timings[static_cast<size_t>(stage)];
  ++timing.count;
  timing.total_us += elapsed_us;
  timing.worst_us = std::max(timing.worst_us, elapsed_us);
  timing.bytes += bytes;
  timing.checkpoint_us += checkpoint_us;
}

void record_live_trial_preparation_time(TrialPreparationStage stage, uint32_t elapsed_us, size_t bytes) {
  portENTER_CRITICAL(&preparation_lock);
  if (!synthetic_trial_replay || !preparation_finished)
    add_preparation_time(stage, elapsed_us, bytes, 0);
  portEXIT_CRITICAL(&preparation_lock);
}

void record_live_trial_progressive_jpeg(TrialJpegPhaseTiming input, TrialJpegPhaseTiming output) {
  portENTER_CRITICAL(&preparation_lock);
  if (!synthetic_trial_replay || !preparation_finished) {
    add_preparation_time(TrialPreparationStage::ProgressiveJpegInput, input.elapsed_us, 0, input.checkpoint_us);
    add_preparation_time(TrialPreparationStage::ProgressiveJpegOutput, output.elapsed_us, 0, output.checkpoint_us);
  }
  portEXIT_CRITICAL(&preparation_lock);
}

void record_live_trial_replay_slide(bool paired, bool fill_edges_match) {
  portENTER_CRITICAL(&preparation_lock);
  if (!preparation_finished) {
    ++(paired ? replay_slides.paired : replay_slides.single);
    replay_slides.pair_fill_edge_failures += !fill_edges_match;
  }
  portEXIT_CRITICAL(&preparation_lock);
}

void record_live_trial_thumbnail_ready(bool paired) {
  (paired ? thumbnail_pairs : thumbnail_landscapes).fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_thumbnail_promotion() {
  thumbnail_promotions.fetch_add(1, std::memory_order_relaxed);
}

void record_live_trial_detail_presented(int64_t slide, uint32_t request_to_panel_ms) {
  last_detail_slide.store(static_cast<int32_t>(slide), std::memory_order_relaxed);
  last_detail_latency_ms.store(request_to_panel_ms, std::memory_order_relaxed);
  detail_presentations.fetch_add(1, std::memory_order_release);
}

void record_live_trial_touch_slide(int64_t slide) {
  last_aligned_touch_slide.store(static_cast<int32_t>(slide), std::memory_order_relaxed);
}

void record_live_trial_photo(uint32_t elapsed_ms) {
  if (prepared_slides.load(std::memory_order_relaxed) == 0) {
    first_photo_ms.store(elapsed_ms, std::memory_order_relaxed);
    first_photo_preview_requests.store(preview_requests.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }
  prepared_slides.fetch_add(1, std::memory_order_release);
}

void record_live_trial_cache(unsigned slots, unsigned ahead) {
  cache_slots.store(slots, std::memory_order_relaxed);
  ready_ahead.store(ahead, std::memory_order_relaxed);
}

void record_live_trial_gesture(uint32_t motion_us, const FirstMotionTrace &motion) {
  portENTER_CRITICAL(&swipe_lock);
  metrics.gesture(motion_us, motion);
  portEXIT_CRITICAL(&swipe_lock);
  last_motion_us.store(motion_us, std::memory_order_relaxed);
  swipes.fetch_add(1, std::memory_order_release);
}

#if !SWIPE_TRIAL_BENCHMARK
void record_live_trial_drag_outcome(TrialDragOutcome outcome) {
  portENTER_CRITICAL(&swipe_lock);
  switch (outcome) {
    case TrialDragOutcome::Forward: ++physical_drags.forward; break;
    case TrialDragOutcome::Backward: ++physical_drags.backward; break;
    case TrialDragOutcome::Returned: ++physical_drags.returned; break;
    case TrialDragOutcome::Cancelled: ++physical_drags.cancelled; break;
    case TrialDragOutcome::Interrupted: ++physical_drags.interrupted; break;
  }
  portEXIT_CRITICAL(&swipe_lock);
}
#endif

void record_live_trial_frame(int64_t latched_us, MotionStamp stamp, uint32_t period_us, uint32_t handoff_delay_us,
                             int64_t demand_us) {
  portENTER_CRITICAL(&swipe_lock);
  metrics.frame(latched_us, stamp, period_us, handoff_delay_us, demand_us);
  portEXIT_CRITICAL(&swipe_lock);
}

void record_live_trial_blit(bool dma) {
  portENTER_CRITICAL(&swipe_lock);
  metrics.blit(dma);
  portEXIT_CRITICAL(&swipe_lock);
}

void record_live_trial_split_blit(uint32_t elapsed_us, bool batched) {
  portENTER_CRITICAL(&swipe_lock);
  metrics.split_blit(elapsed_us, batched);
  portEXIT_CRITICAL(&swipe_lock);
}

#if SWIPE_TRIAL_BENCHMARK
std::optional<TouchReport> poll_live_trial_benchmark([[maybe_unused]] NativeNetwork &network, int64_t now,
                                                    SlideIndex center, SlideRange ready, bool at_rest) {
  auto wifi = TrialSwipeBenchmark::WifiWindow::Waiting;
#if SWIPE_TRIAL_WIFI_OUTAGE
  const auto link = network.trial_link();
  if (link == NativeNetwork::TrialLink::Ready && at_rest && center == 1 && ready.last - center >= 3)
    network.start_trial_wifi_outage();
  if (link == NativeNetwork::TrialLink::Offline && !network.station_lease())
    wifi = TrialSwipeBenchmark::WifiWindow::Offline;
  if (link == NativeNetwork::TrialLink::Reconnected && network.photo_connection())
    wifi = TrialSwipeBenchmark::WifiWindow::Reconnected;
#endif
  portENTER_CRITICAL(&swipe_lock);
  const auto report = benchmark.poll(now, center, ready, at_rest, wifi);
  if constexpr (synthetic_trial_replay || SWIPE_TRIAL_WIFI_OUTAGE) {
    if (benchmark.finished() && !completed_replay_metrics) {
      completed_replay_metrics = metrics.results();
      // Freeze both reports before publishing the final settle. A download
      // finishing after the script must not change a captured comparison.
      portENTER_CRITICAL(&preparation_lock);
      preparation_finished = true;
      portEXIT_CRITICAL(&preparation_lock);
    }
  }
  portEXIT_CRITICAL(&swipe_lock);
  return report;
}
#endif

}  // namespace espframe::native
