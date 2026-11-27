#include "autoplay_fade.h"
#include "c6_firmware_update.h"
#include "clock_overlay.h"
#include "daylight.h"
#include "display.h"
#include "esphome_trial.h"
#include "firmware_update.h"
#include "native_release_updates.h"
#include "frame_blend.h"
#include "frame_blitter.h"
#include "immich.h"
#include "initial_photo_timeout.h"
#include "live_trial.h"
#include "native_network.h"
#include "photo_controls.h"
#include "photo_zoom_cache.h"
#include "playback_transition.h"
#include "preview_decoder.h"
#include "recovery_card.h"
#include "screen_control.h"
#include "slide_cache.h"
#include "slide_dwell.h"
#include "slide_prefetch.h"
#include "slideshow_interval.h"
#include "slideshow_progress_ring.h"
#include "slideshow_transition.h"
#include "swipe.h"
#include "swipe_input.h"
#include "touch.h"
#include "trace_logger.h"
#include "trial_replay.h"
#include "trial_swipe_landing.h"
#include "usb_setup.h"
#include "web_logs.h"

#include "driver/jpeg_decode.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace espframe::native {
namespace {

using esphome::espframe::SlideTransition;
using esphome::espframe::TransitionBlend;
using esphome::espframe::slide_transition_plan;

constexpr const char *tag = "swipe-probe";
constexpr int fixture_count = 5;
constexpr int fixture_slides = 30;
constexpr int player_core = 1;
constexpr int loader_core = 0;
constexpr UBaseType_t player_priority = 4;
constexpr bool trace_enabled = SWIPE_TRACE;
constexpr bool esphome_trial = SWIPE_ESPHOME_TRIAL;
constexpr bool trial_immich = SWIPE_TRIAL_IMMICH;
constexpr bool track_physical_drags = trial_immich && !SWIPE_TRIAL_BENCHMARK;
constexpr bool capture_motion = trace_enabled || trial_immich;
constexpr bool linked_scanout = SWIPE_LINKED_SCANOUT;
constexpr bool trial_fixtures = SWIPE_TRIAL_BENCHMARK_FIXTURES;
extern const uint8_t clock_atlas[] asm("_binary_clock_glyphs_bin_start");
extern const uint8_t caption_atlas[] asm("_binary_caption_glyphs_bin_start");
extern const uint8_t caption_atlas_end[] asm("_binary_caption_glyphs_bin_end");
extern const uint8_t date_atlas[] asm("_binary_date_glyphs_bin_start");
extern const uint8_t date_atlas_end[] asm("_binary_date_glyphs_bin_end");
extern const uint8_t recovery_atlas[] asm("_binary_recovery_glyphs_bin_start");
extern const uint8_t status_masks[] asm("_binary_status_masks_bin_start");
extern const uint8_t status_masks_end[] asm("_binary_status_masks_bin_end");
NativeNetwork network;

class PhotoControlsRenderer {
 public:
  explicit PhotoControlsRenderer(const PhotoControls &controls) : controls_(controls) {}

  void start() {
    done_ = xSemaphoreCreateBinaryStatic(&done_storage_);
    if (xTaskCreatePinnedToCore(run_, "photo-scale", 2048, this, player_priority, &worker_, loader_core) != pdPASS)
      worker_ = nullptr;
  }

  void draw() const {
    if (worker_) {
      // Each core writes whole rows. Join before painting the toolbar or submitting the frame.
      xTaskNotifyGive(worker_);
      controls_.draw_rows(controls_.split_row(), panel_height);
      xSemaphoreTake(done_, portMAX_DELAY);
    } else {
      controls_.draw_rows(0, panel_height);
    }
    controls_.draw_toolbar();
  }

 private:
  static void run_(void *context) {
    const auto &renderer = *static_cast<PhotoControlsRenderer *>(context);
    while (true) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      renderer.controls_.draw_rows(0, renderer.controls_.split_row());
      xSemaphoreGive(renderer.done_);
    }
  }

  const PhotoControls &controls_;
  StaticSemaphore_t done_storage_{};
  SemaphoreHandle_t done_{};
  TaskHandle_t worker_{};
};

struct GestureTiming {
  int64_t gesture_to_scanout_us{};
  int64_t compose_and_submit_us{};
};

struct EncodedPhoto {
  const uint8_t *begin{};
  const uint8_t *end{};
};

#define DECLARE_PHOTO(index)                                                          \
  extern const uint8_t photo_##index##_start[] asm("_binary_slide_" #index "_jpg_start"); \
  extern const uint8_t photo_##index##_end[] asm("_binary_slide_" #index "_jpg_end");

#ifdef SWIPE_PHOTO_0
DECLARE_PHOTO(0)
#endif
#ifdef SWIPE_PHOTO_1
DECLARE_PHOTO(1)
#endif
#ifdef SWIPE_PHOTO_2
DECLARE_PHOTO(2)
#endif
#ifdef SWIPE_PHOTO_3
DECLARE_PHOTO(3)
#endif
#ifdef SWIPE_PHOTO_4
DECLARE_PHOTO(4)
#endif

#undef DECLARE_PHOTO

constexpr EncodedPhoto photos[fixture_count]{
#ifdef SWIPE_PHOTO_0
    {photo_0_start, photo_0_end},
#else
    {},
#endif
#ifdef SWIPE_PHOTO_1
    {photo_1_start, photo_1_end},
#else
    {},
#endif
#ifdef SWIPE_PHOTO_2
    {photo_2_start, photo_2_end},
#else
    {},
#endif
#ifdef SWIPE_PHOTO_3
    {photo_3_start, photo_3_end},
#else
    {},
#endif
#ifdef SWIPE_PHOTO_4
    {photo_4_start, photo_4_end},
#else
    {},
#endif
};

uint16_t rgb565(int r, int g, int b) {
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

// Distinct horizons and one split scene make dropped frames, a wrong swipe
// direction, incorrect rotation, or a broken seam easy to spot without assets.
void draw_test_slide(uint16_t *destination, int slide, ScreenRotation rotation) {
  constexpr int sky[][3]{{224, 143, 99}, {64, 162, 190}, {101, 146, 220},
                         {199, 112, 143}, {77, 165, 146}};
  const Viewport viewport{rotation};
  for (int x = 0; x < viewport.width; ++x) {
    auto *column = destination + viewport.offset(x, 0);
    const int scene = slide == 3 && x >= viewport.width / 2 ? 4 : slide;
    const float phase = scene * 1.6f + x * 0.008f;
    const int ridge = static_cast<int>(485 + 90 * std::sin(phase) + 35 * std::sin(phase * 2.4f));
    const int shore = static_cast<int>(635 + 23 * std::sin(phase * 0.6f + 1.0f));
    const int sun_x = 260 + scene * 146;
    const int sun_y = 200 + scene * 21;
    for (int y = 0; y < viewport.height; ++y) {
      uint16_t color;
      if (slide == 3 && std::abs(x - viewport.width / 2) <= 2) {
        color = rgb565(22, 22, 22);
      } else if ((x - sun_x) * (x - sun_x) + (y - sun_y) * (y - sun_y) < 3'600) {
        color = rgb565(255, 237, 201);
      } else if (y < ridge) {
        color = rgb565(sky[scene][0] + y / 18, sky[scene][1] + y / 24, sky[scene][2] + y / 32);
      } else if (y < shore) {
        color = rgb565(30 + scene * 15 + y / 28, 52 + scene * 12, 76 + y / 16);
      } else {
        color = rgb565(20 + scene * 16, 89 + y / 20, 112 + scene * 12);
      }
      column[y * viewport.y_stride] = color;
    }
  }
}

bool decode_photo(const EncodedPhoto &photo, uint16_t *pixels, jpeg_decoder_handle_t &decoder) {
  if (photo.begin == nullptr)
    return false;

  const auto length = static_cast<uint32_t>(photo.end - photo.begin);
  jpeg_decode_picture_info_t info{};
  if (jpeg_decoder_get_info(photo.begin, length, &info) != ESP_OK ||
      info.width != panel_width || info.height != panel_height) {
    ESP_LOGW(tag, "Expected an 800 x 1280 baseline JPEG in native panel orientation");
    return false;
  }
  if (decoder == nullptr) {
    jpeg_decode_engine_cfg_t engine{};
    engine.timeout_ms = 100;
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&engine, &decoder));
  }

  jpeg_decode_cfg_t config{};
  config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t written = 0;
  constexpr size_t bytes = pixel_count * sizeof(*pixels);
  const esp_err_t status = jpeg_decoder_process(decoder, &config, photo.begin, length,
                                                reinterpret_cast<uint8_t *>(pixels), bytes, &written);
  if (status != ESP_OK || written != bytes) {
    ESP_LOGW(tag, "JPEG decode rejected: %s (%" PRIu32 " bytes)", esp_err_to_name(status), written);
    return false;
  }
  return true;
}

void allocate_slide_buffers(SlideCache::Buffers &buffers, size_t first, size_t last) {
  constexpr size_t bytes = pixel_count * sizeof(uint16_t);
  constexpr size_t decode_and_download_reserve = 10 * 1024 * 1024;
  constexpr auto caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  for (size_t slot = first; slot < last; ++slot) {
    const bool extra = slot >= SlideCache::minimum_capacity;
    if (extra && heap_caps_get_free_size(caps) < decode_and_download_reserve + bytes)
      break;
    buffers[slot] = static_cast<uint16_t *>(heap_caps_aligned_alloc(128, bytes, caps));
    if (buffers[slot] == nullptr && extra)
      break;
    ESP_ERROR_CHECK(buffers[slot] != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  }
}

class SlideLoader {
 public:
  using Preparation = ImmichSource::Preparation;
  using Result = ImmichSource::PrepareResult;

  struct RotationSource {
    const uint16_t *pixels{};
    ScreenRotation rotation{};
    bool paired{};
  };

  struct Completion {
    SlideCache::Lease lease;
    SlideIndex first_photo;
    SlideIndex next_photo;
    uint32_t connection;
    PhotoSourceStatus source_status;
    ImmichResponse response;
    Result result;
    bool cancelled;
    bool source_ready;
    Preparation preparation;
    ImmichSource::PendingPresentation presentation;
    bool rotation_fallback;
  };

  bool live() const { return this->live_; }
  int64_t slide_interval_us() const {
    const auto seconds = esphome_trial ? this->settings_.slide_seconds
                                      : active_slideshow_interval.load(std::memory_order_relaxed);
    return seconds * 1'000'000LL;
  }
  PlaybackTransition playback_transition() const {
    if constexpr (esphome_trial)
      return PlaybackTransition::from_settings(this->settings_.transition, this->settings_.transition_speed);
    return active_playback_transition.load(std::memory_order_relaxed);
  }
  uint8_t brightness() const { return this->settings_.brightness_day; }
  uint8_t night_brightness() const { return this->settings_.night_brightness(); }
  const ClockSettings &clock() const { return this->settings_.clock; }
  bool twelve_hour_clock() const {
    if constexpr (esphome_trial)
      return this->settings_.clock.twelve_hour();
    return active_clock_twelve_hour.load(std::memory_order_relaxed);
  }
  bool photos_need_time() const {
    return this->settings_.needs_local_date() || this->settings_.tone.needs_time();
  }
  const ScreenScheduleSettings &schedule() const { return this->settings_.schedule; }
  ScreenRotation rotation() const { return this->settings_.screen_rotation; }

  void start_network() const {
    network.start(this->settings_);
    if constexpr (!esphome_trial)
      start_c6_firmware_updates(network);
  }

  bool start() {
    if constexpr (trial_immich) {
      if (!this->settings_.read_esphome_trial()) {
        ESP_LOGW(tag, "Could not read compatible ESPHome Wi-Fi and Immich settings");
        end_esphome_trial();
        return false;
      }
      if constexpr (!trial_replay_origin.empty()) {
        this->settings_.immich_url = std::string(trial_replay_origin) + "/api";
        this->settings_.immich_key = replay_immich_key;
        this->settings_.photo_source = {};
        this->settings_.photo_orientation = PhotoOrientation::Any;
        this->settings_.capture_dates = {};
        this->settings_.portrait_pairing = this->settings_.portrait_pairs_only = true;
        this->settings_.portrait_pairing_range_index = 2;
        this->settings_.display_mode = DisplayMode::Fit;
        if constexpr (trial_replay_pair_fill)
          this->settings_.pair_framing = PairFraming::Fill;
        if constexpr (SWIPE_TRIAL_REPLAY_PREPARED) {
          this->settings_.prepared_slide_url = trial_replay_origin;
          this->settings_.prepared_slide_token = replay_slide_token;
        }
      }
      this->live_ = true;
    } else {
      this->live_ = !esphome_trial && this->settings_.read() && this->settings_.has_immich();
      if constexpr (!esphome_trial) {
        this->settings_.publish_live_presentation();
        active_photo_preset.store(this->settings_.photo_source.preset(), std::memory_order_relaxed);
        frame_status.configure(this->live_, this->settings_.photo_source.mode);
      }
    }
    this->requests_ = xQueueCreate(1, sizeof(Request));
    this->completed_ = xQueueCreate(1, sizeof(Completion));
    this->pixel_work_wake_ = xSemaphoreCreateBinaryStatic(&this->pixel_work_wake_storage_);
    if constexpr (!trace_enabled)
      this->timings_ = xQueueCreate(32, sizeof(GestureTiming));
    ESP_ERROR_CHECK(this->requests_ && this->completed_ && this->pixel_work_wake_ && (trace_enabled || this->timings_)
                        ? ESP_OK : ESP_ERR_NO_MEM);
    // Native playback also applies saved feed changes in this worker.
    constexpr uint32_t loader_stack_bytes = esphome_trial ? 6144 : 12288;
    const auto result = xTaskCreatePinnedToCore(run_, "slide-prepare", loader_stack_bytes, this, 2, &this->task_,
                                              loader_core);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    return true;
  }

  void submit(const SlideCache::Lease &lease, uint32_t source_revision, bool replace_source, Preparation preparation,
              ScreenRotation rotation, RotationSource cached) {
    this->cancelled_ = false;
    const Request request{lease, source_revision, replace_source, preparation, rotation, cached};
    ESP_ERROR_CHECK(xQueueSend(this->requests_, &request, 0) == pdPASS ? ESP_OK : ESP_ERR_INVALID_STATE);
    xTaskNotifyGive(this->task_);
  }

  bool poll(Completion &completion) {
    return xQueueReceive(this->completed_, &completion, 0) == pdPASS;
  }

  void yield_to_swipe() {
    if (!this->cancelled_.exchange(true))
      xSemaphoreGive(this->pixel_work_wake_);
  }
  bool yield_requested() const { return this->cancelled_.load(); }
  bool promote_preview() {
    if (!this->source_.claim_preview_promotion())
      return false;
    this->yield_to_swipe();
    return true;
  }
  void defer_pixel_work(bool defer) {
    if (this->pixel_work_deferred_.load(std::memory_order_relaxed) == defer)
      return;
    this->pixel_work_deferred_.store(defer, std::memory_order_release);
    if (!defer)
      xSemaphoreGive(this->pixel_work_wake_);
  }

  void report_gesture(const GestureTiming &timing) {
    // USB output can block. Leave it to the worker and drop a diagnostic
    // sample if downloads keep the worker occupied for too long.
    if (xQueueSend(this->timings_, &timing, 0) == pdPASS)
      xTaskNotifyGive(this->task_);
  }

 private:
  bool wait_for_pixel_work() const {
    if (!this->pixel_work_deferred_.load(std::memory_order_acquire) || this->cancelled_)
      return !this->cancelled_;

    const auto started = esp_timer_get_time();
    // A give before the wait is retained. Recheck after waking because a
    // subsequent frame may already need the bandwidth, or the lease may end.
    while (this->pixel_work_deferred_.load(std::memory_order_acquire) && !this->cancelled_)
      xSemaphoreTake(this->pixel_work_wake_, portMAX_DELAY);
    if constexpr (trial_immich)
      record_live_trial_pixel_pause(static_cast<uint32_t>(esp_timer_get_time() - started));
    return !this->cancelled_;
  }

  struct Request {
    SlideCache::Lease lease;
    uint32_t source_revision;
    bool replace_source;
    Preparation preparation;
    ScreenRotation rotation;
    RotationSource cached;
  };

  bool apply_photo_settings_(const Request &request) {
    auto feed = Settings::take_live_photo_feed(request.source_revision);
    if (!feed)
      return false;
    if (feed->needs_local_date() || feed->tone.needs_time())
      network.request_time_sync();
    if (feed->pending_connection)
      network.use_photo_connection(*feed->pending_connection);
    frame_status.configure(true, feed->photo_source.mode);
    this->source_.apply_photo_settings(std::move(*feed), request.replace_source ? std::optional{request.lease.slide}
                                                                                : std::nullopt);
    return true;
  }

  static void run_(void *context) {
    auto &loader = *static_cast<SlideLoader *>(context);
    jpeg_decoder_handle_t decoder{};
    uint32_t source_revision{};
    constexpr int64_t tone_check_interval_us = 60'000'000;
    int64_t next_tone_check = esp_timer_get_time() + tone_check_interval_us;
    const bool maintain_tone = !esphome_trial && loader.live_;
    Daylight tone_daylight{nullptr};
    while (true) {
      if (maintain_tone) {
        const auto remaining_ms = (next_tone_check - esp_timer_get_time() + 999) / 1000;
        ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(std::max<int64_t>(0, remaining_ms))));
        const auto now = esp_timer_get_time();
        if (now >= next_tone_check) {
          next_tone_check = now + tone_check_interval_us;
          if (active_warm_tone_override.load(std::memory_order_relaxed) &&
              !loader.pixel_work_deferred_.load(std::memory_order_relaxed)) {
            const auto local = local_clock_reading(std::time(nullptr));
            if (!local.time)
              network.request_time_sync();
            else if (local.zone) {
              tone_daylight.set_zone(local.zone);
              if (tone_daylight.is_day(local.time))
                loader.source_.expire_warm_tone_override(*local.zone);
            }
          }
        }
      } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      }
      if constexpr (!trace_enabled) {
        GestureTiming timing;
        while (xQueueReceive(loader.timings_, &timing, 0) == pdPASS)
          ESP_LOGI(tag, "gesture_to_scanout_us=%" PRId64 " compose_and_submit_us=%" PRId64,
                   timing.gesture_to_scanout_us, timing.compose_and_submit_us);
      }
      Request request{};
      if (xQueueReceive(loader.requests_, &request, 0) != pdPASS)
        continue;
      const auto &lease = request.lease;
      auto connection = network.photo_connection();
      auto result = Result::Ready;
      bool source_ready = true;
      bool rotation_fallback = false;
      ImmichSource::PendingPresentation presentation{};
      if (request.preparation == Preparation::RotateCached) {
        source_ready = false;
        if (loader.live_ && lease.slide > 0 && !trial_fixtures) {
          result = loader.source_.prepare(lease.slide, lease.pixels, true, presentation, request.preparation,
                                          request.rotation);
        } else {
          draw_test_slide(lease.pixels, lease.slide % fixture_count, request.rotation);
        }
        if (result != Result::Ready && !loader.cancelled_) {
          rotation_fallback = reframe_cached_photo(lease.pixels, request.cached.pixels, request.cached.rotation,
                                                    request.rotation, request.cached.paired, loader.source_.display_mode(),
                                                    {&loader, [](void *context) {
                                                      return static_cast<SlideLoader *>(context)->wait_for_pixel_work();
                                                    }});
          if (rotation_fallback) {
            presentation = loader.source_.describe_cached_slide(lease.slide, request.rotation);
            presentation.paired = request.cached.paired && !portrait_screen(request.rotation);
            result = Result::Ready;
          }
        }
        loader.source_.end_preview_promotion();
      } else if ((!esphome_trial || trial_immich) && !trial_fixtures && loader.live_ && lease.slide > 0) {
        if constexpr (trial_immich)
          record_live_trial_prepare();
        if constexpr (!esphome_trial) {
          if (request.source_revision != source_revision) {
            source_ready = loader.apply_photo_settings_(request);
            if (source_ready) {
              source_revision = request.source_revision;
              connection = network.photo_connection();
            }
          }
        }
        result = source_ready ? loader.source_.prepare(lease.slide, lease.pixels, lease.replacement,
                                                       presentation, request.preparation, request.rotation) : Result::Retry;
        loader.source_.end_preview_promotion();
      } else {
        const int fixture = lease.slide % fixture_count;
        const auto &photo = photos[fixture];
        const auto started_us = trial_fixtures && photo.begin ? esp_timer_get_time() : 0;
        const bool decoded = !portrait_screen(request.rotation) && decode_photo(photo, lease.pixels, decoder);
        if constexpr (trial_fixtures) {
          if (photo.begin)
            record_live_trial_preparation_time(decoded ? TrialPreparationStage::HardwareJpeg
                                                       : TrialPreparationStage::RejectedPreview,
                                               static_cast<uint32_t>(esp_timer_get_time() - started_us),
                                               static_cast<size_t>(photo.end - photo.begin));
        }
        if (!decoded)
          draw_test_slide(lease.pixels, fixture, request.rotation);
        else if (request.rotation == ScreenRotation::UpsideDown)
          std::reverse(lease.pixels, lease.pixels + pixel_count);
      }
      // The worker owns every pixel until it publishes the lease. Sync it
      // here so scanout can borrow immutable rows without a per-frame flush.
      if constexpr (linked_scanout) {
        if (result == Result::Ready &&
            esp_cache_msync(lease.pixels, pixel_count * sizeof(*lease.pixels), ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
          result = Result::Retry;
      }
      const Completion completion{lease, loader.source_.first_slide(), loader.source_.next_slide(), connection,
                                  loader.source_.source_status(), loader.source_.response(), result,
                                  result != Result::Ready && loader.cancelled_.load(), source_ready,
                                  request.preparation, presentation, rotation_fallback};
      xQueueSend(loader.completed_, &completion, portMAX_DELAY);
      if constexpr (!esphome_trial) {
        // Check which filters the server supports after handing off a ready slide.
        if (result == Result::Ready && loader.live_ && lease.slide > 0 && uxQueueMessagesWaiting(loader.requests_) == 0)
          loader.source_.probe_filter_support();
      }
    }
  }

  std::atomic<bool> cancelled_{};
  std::atomic<bool> pixel_work_deferred_{};
  StaticSemaphore_t pixel_work_wake_storage_{};
  SemaphoreHandle_t pixel_work_wake_{};
  Settings settings_;
  ImmichSource source_{settings_, network, cancelled_, {this, [](void *context) {
    return static_cast<const SlideLoader *>(context)->wait_for_pixel_work();
  }}};
  QueueHandle_t requests_{};
  QueueHandle_t completed_{};
  QueueHandle_t timings_{};
  TaskHandle_t task_{};
  bool live_{};
};

void run(void *) {
  if constexpr (!esphome_trial) {
    start_firmware_boot_check();
#if NATIVE_LEGACY_MIGRATION
    if (!Settings::import_esphome_migration()) {
      ESP_LOGE(tag, "Legacy V1 settings import failed");
      reject_pending_firmware_boot();
      vTaskDelete(nullptr);
      return;
    }
#endif
    start_native_release_updates(network);
  }
  static SlideLoader loader;
  if (!loader.start()) {
    vTaskDelete(nullptr);
    return;
  }
  SlideCache::Buffers buffers{};
  allocate_slide_buffers(buffers, 0, SlideCache::minimum_capacity);
  const bool live = loader.live();
  auto rotation = loader.rotation();
  Viewport viewport{rotation};
  const SlideIndex slide_count = live && !trial_fixtures ? std::numeric_limits<PixelPosition>::max() / slide_width
                                                        : fixture_slides;
  FrameBlend blend;
  bool can_fade = live && blend.init();
  static Display display;
  display.init(can_fade);
  FrameBlitter blitter;
  blitter.init();
  ClockOverlay clock;
  ScreenBrightness brightness_levels{loader.brightness(), loader.night_brightness()};
  const bool solar_brightness = brightness_levels.day != brightness_levels.night;
  const bool use_local_time = loader.clock().show || loader.schedule().enabled || solar_brightness;
  const auto *clock_zone = find_clock_zone(loader.clock().timezone);
  const bool show_solar_times = !esphome_trial && clock_zone != nullptr;
  bool local_time_configured = (use_local_time || loader.photos_need_time() || (!esphome_trial && live) || show_solar_times) &&
                               configure_local_time(clock_zone);
  Daylight daylight{solar_brightness || show_solar_times ? clock_zone : nullptr};
  std::optional<SolarTimes> published_solar_times;
  if (show_solar_times && local_time_configured)
    network.request_time_sync();
  bool daytime = true;
  uint8_t brightness = brightness_levels.day;
  std::optional<uint8_t> manual_brightness;
  uint8_t applied_brightness = 0;
  const bool can_layer_fade = clock.init(loader.clock(), clock_atlas, can_fade && display.can_pin_frame(), rotation);
  if (can_fade && (!display.can_pin_frame() || !can_layer_fade)) {
    blend.release();
    can_fade = false;
  }
  if (live && !can_fade)
    ESP_LOGI(tag, "Slideshow compositor unavailable; using the glide for animated transitions");
  if (live && !trial_fixtures)
    allocate_slide_buffers(buffers, SlideCache::minimum_capacity, buffers.size());
  SlideCache cache{buffers};
  // Caption layout travels with the cache lease, separate from scanout pixels.
  static std::array<PhotoCaption, SlideCache::capacity> captions;
  std::array<bool, SlideCache::capacity> paired_slots{};
  const std::span<const uint8_t> caption_glyphs{caption_atlas, static_cast<size_t>(caption_atlas_end - caption_atlas)};
  const std::span<const uint8_t> date_glyphs{date_atlas, static_cast<size_t>(date_atlas_end - date_atlas)};
  PhotoCaptionFade caption_fade;
  PhotoCaptionSample last_caption;
  const auto cache_slots = static_cast<unsigned>(cache.size());
  ESP_LOGI(tag, "Decoded photo cache: %u slides", cache_slots);
  if constexpr (trial_immich)
    record_live_trial_cache(cache_slots, 0);
  TouchReader touch;
  touch.start();
  Swipe swipe{viewport.width, slide_count, live ? 0 : 2};
  SwipeInput input;
  static PhotoControls controls;
  static PhotoControlsRenderer controls_renderer{controls};
  static PhotoZoomCache zoom_cache;
  struct PreparedZoomFrame {
    uint16_t *pixels{};
    const uint16_t *source{};
    int progress{};
    ScreenRotation rotation{};
  } prepared_zoom;
  int last_control_progress = 0;
  uint8_t last_text_opacity = PhotoControls::opaque;
  ScreenSchedule schedule{loader.schedule()};
  SlideDwell dwell;
  int last_ring_progress = -1;
  SlidePrefetch prefetch{swipe.settled(), cache_slots, viewport.width};
  AutoplayFade fade;
  auto previous_transition = SlideTransition::NONE;
  auto effect = slide_transition_plan(SlideTransition::CROSS_FADE);
  const uint16_t *snapshot = nullptr;
  const auto pin_fade = [&] {
    snapshot = display.pin_handoff();
    clock.pin_underlay();
  };
  const auto suspend_fade = [&] {
    if (fade.running() && fade.interrupt(snapshot != nullptr))
      pin_fade();
    if (fade.held())
      fade.note_drag();
  };
  uint8_t pending_opacity = 0;
  int64_t next_clock_poll = 0;
  bool clock_dirty = false;
  bool local_time_ready = false;
  bool recovery_painted = false;
  std::optional<uint8_t> setup_language;
  uint32_t setup_ipv4{};
  int64_t setup_dim_at{};
  PhotoSourceStatus source_status = PhotoSourceStatus::None;
  PhotoSourceStatus painted_source_status = PhotoSourceStatus::None;
  InitialPhotoTimeout initial_photo_timeout;
  ImmichResponse painted_connection_notice = ImmichResponse::Pending;
  uint8_t painted_notice_language{};
  std::optional<int> local_hour;

  int64_t submitted_motion_us = 0;
  int64_t motion_render_us = 0;
  PixelPosition last_submitted = -1;
  PixelPosition last_inside = -1;
  bool backlight_ready = false;
  bool showed_photo = false;
  bool user_motion = false;
  [[maybe_unused]] bool linked_scanout_ok = linked_scanout;
  // Slide zero is the startup scene, so only negative indices mean no job.
  SlideIndex preparing = -1;
  static TraceLogger trace;
  MotionEpoch motion_epoch;
  MotionStamp submitted_stamp;
  DragFrameDemand drag_demand;
  int64_t submitted_drag_demand_us{};
  FirstMotionTrace first_motion;
  TrialSwipeLanding physical_landing;
  const auto record_landing = [&] {
    if (const auto outcome = physical_landing.presented(swipe.settled()))
      record_live_trial_drag_outcome(*outcome);
  };
  int64_t refill_started_us{};
  struct DetailHandoff {
    SlideIndex slide;
    int64_t requested_us;
  };
  constexpr int64_t detail_idle_us = 500'000;
  int64_t last_detail_motion_us{};
  int64_t detail_request_us{};
  std::optional<DetailHandoff> detail_to_submit;
  std::optional<DetailHandoff> detail_submitted;
  const int64_t refresh_period_us = Display::nominal_frame_period_us();
  if constexpr (trace_enabled)
    trace.start(refresh_period_us);
  if constexpr (!esphome_trial || trial_immich)
    loader.start_network();
  if constexpr (!SWIPE_TRIAL_BENCHMARK) {
    controls_renderer.start();
    if (!esphome_trial && live && !trial_fixtures && display.can_pin_frame() &&
        PhotoControls::supports_cached_steps(refresh_period_us))
      zoom_cache.start(player_core, loader_core);
  }
  const auto submit_frame = [&](PixelPosition position, bool moving = true, bool resisted = false,
                                const ScanoutFrame *frame = nullptr, std::optional<SlideRange> sources = {},
                                const uint16_t *prepared = nullptr) {
    if (!display.submit(position, frame, sources, prepared))
      return false;
    if constexpr (trial_detail_metrics) {
      if (detail_to_submit && position == detail_to_submit->slide * viewport.width)
        detail_submitted = detail_to_submit;
      detail_to_submit.reset();
    }
    if constexpr (capture_motion) {
      submitted_stamp = moving ? motion_epoch.stamp(preparing >= 0, resisted) : MotionStamp{};
      submitted_stamp.landing = submitted_stamp.phase == MotionPhase::Settle &&
                                position == swipe.settle_destination() * viewport.width;
    }
    if constexpr (trial_immich)
      submitted_drag_demand_us = submitted_stamp.phase == MotionPhase::Drag ? drag_demand.take() : 0;
    return true;
  };
  const auto submit_autoplay_frame = [&](uint16_t *output, uint8_t progress) {
    // A transition can be pinned and dragged later, so keep it clear of these overlays.
    last_caption = {};
    last_ring_progress = -1;
    clock.draw(output, true);
    clock_dirty = false;
    if constexpr (capture_motion)
      motion_epoch.update(MotionPhase::Crossfade);
    submit_frame(fade.anchor() * viewport.width);
    recovery_painted = false;
    painted_source_status = PhotoSourceStatus::None;
    painted_connection_notice = ImmichResponse::Pending;
    fade.submitted(progress);
  };
  SlideIndex first_photo = 1;
  SlideIndex next_photo = 1;
  uint32_t source_revision{};
  uint32_t selection_revision{};
  SlideIndex source_anchor = -1;
  bool preparing_refresh = false;
  std::optional<ScreenRotation> preparing_rotation;
  bool awaiting_rotation_frame = false;
  using Preparation = SlideLoader::Preparation;
  constexpr bool stage_at_edge = !esphome_trial || live_trial_edge_detail || SWIPE_TRIAL_REPLAY_EDGE_DETAIL;
  constexpr bool stage_all_pairs = trial_replay_staged_detail && !SWIPE_TRIAL_REPLAY_EDGE_DETAIL;
  unsigned reported_ahead = 0;
  std::array<SlideRetry, SlideCache::capacity> retries{};
  const auto drag_moving_window_us = 3 * refresh_period_us;
  const auto drag_start_guard_us = 6 * refresh_period_us;
  const auto edge_drag_guard_us = 4 * refresh_period_us;

  const auto update_backlight = [&](int64_t now, bool gesturing, bool dim_setup) {
    if (backlight_ready) {
      const bool commanded = !esphome_trial && screen_control.apply_pending(schedule, now, local_hour, manual_brightness);
      const bool awake = schedule.update(local_hour, now, gesturing);
      if (awake != input.awake()) {
        if (awake) {
          input.wake();
        } else {
          manual_brightness.reset();
          input.sleep(swipe);
          suspend_fade();
        }
      }
      const auto scheduled = dim_setup ? std::min<uint8_t>(brightness, 50) : brightness;
      const uint8_t target = awake ? manual_brightness.value_or(scheduled) : 0;
      if (target != applied_brightness) {
        display.set_backlight(target);
        if constexpr (!esphome_trial) {
          screen_control.publish_backlight(target);
          if ((target == 0) != (applied_brightness == 0))
            frame_status.set_screen(target != 0);
        }
        applied_brightness = target;
      }
      if (commanded)
        screen_control.completed();
    }
  };

  while (true) {
    auto now = esp_timer_get_time();
    const auto requested_revision = esphome_trial ? 0 : photo_source_revision.load(std::memory_order_acquire);
    if constexpr (!esphome_trial) {
      const auto levels = active_screen_brightness.load(std::memory_order_relaxed);
      if (levels != brightness_levels) {
        brightness_levels = levels;
        manual_brightness.reset();
      }
      schedule.set_settings(ScreenScheduleSettings::unpack(active_screen_schedule.load(std::memory_order_relaxed)));
      clock_dirty |= clock.set_shown(active_clock_visible.load(std::memory_order_relaxed));
      if (const auto *next_zone = requested_clock_zone.load(std::memory_order_relaxed);
          next_zone != clock_zone && configure_local_time(next_zone)) {
        clock_zone = next_zone;
        local_time_configured = true;
        daylight.set_zone(clock_zone);
        next_clock_poll = 0;
        manual_brightness.reset();
      }
    }
    bool local_time_became_ready = false;
    if ((use_local_time || !esphome_trial) && local_time_configured && now >= next_clock_poll) {
      const auto local = local_clock_time(std::time(nullptr));
      local_time_became_ready = local && !local_time_ready;
      local_time_ready = local.has_value();
      if (show_solar_times) {
        const auto times = daylight.times(local);
        if (times != published_solar_times) {
          frame_status.set_solar_times(times);
          published_solar_times = times;
        }
      }
      clock.set_twelve_hour(loader.twelve_hour_clock());
      clock_dirty |= clock.update(local);
      local_hour = local ? std::optional<int>{local->tm_hour} : std::nullopt;
      daytime = daylight.is_day(local);
      next_clock_poll = now + 250'000;
    }
    const uint8_t scheduled_brightness = daytime ? brightness_levels.day : brightness_levels.night;
    if (scheduled_brightness != brightness) {
      manual_brightness.reset();
      brightness = scheduled_brightness;
    }
    if (const auto scanout = display.poll_presented()) {
      controls.presented(*scanout);
      awaiting_rotation_frame = false;
      if constexpr (trial_replay_touch) {
        const auto presented = display.presented_position();
        if (presented % viewport.width == 0)
          record_live_trial_touch_slide(presented / viewport.width);
      }
      if constexpr (trial_detail_metrics) {
        if (detail_submitted && display.presented_position() == detail_submitted->slide * viewport.width) {
          record_live_trial_detail_presented(detail_submitted->slide,
                                             static_cast<uint32_t>((*scanout - detail_submitted->requested_us) / 1000));
        }
        detail_submitted.reset();
      }
      if constexpr (trial_immich)
        record_live_trial_frame(*scanout, submitted_stamp, Display::nominal_frame_period_us(),
                                static_cast<uint32_t>(esp_timer_get_time() - *scanout), submitted_drag_demand_us);
      if (!backlight_ready) {
        if (!schedule.update(local_hour, now, false))
          input.sleep(swipe);
        applied_brightness = input.awake() ? brightness : 0;
        display.init_backlight(applied_brightness);
        if constexpr (!esphome_trial) {
          screen_control.publish_backlight(applied_brightness);
          frame_status.set_screen(applied_brightness != 0);
        }
        backlight_ready = true;
        ESP_LOGI(tag, "Player ready (background %s preparation)", live && !trial_fixtures ? "Immich" : "fixture");
      }
      if constexpr (!esphome_trial) {
        if (live && !showed_photo && display.presented_position() >= viewport.width) {
          frame_status.presented_photo();
          showed_photo = true;
        }
      }
      if (submitted_motion_us != 0) {
        const auto motion_us = *scanout - submitted_motion_us;
        if constexpr (trial_immich)
          record_live_trial_gesture(static_cast<uint32_t>(motion_us), first_motion);
        if constexpr (trace_enabled)
          trace.gesture(*scanout, first_motion);
        else
          loader.report_gesture({motion_us, motion_render_us});
        submitted_motion_us = 0;
      }
      if constexpr (track_physical_drags) {
        if (input.awake() && !input.engaged() && swipe.idle() && swipe.position() == display.presented_position())
          record_landing();
      }
      if constexpr (trace_enabled)
        trace.frame(*scanout, submitted_stamp);
    }

    if constexpr (!esphome_trial) {
      const bool first_run = network.first_run_recovery();
      const auto address = first_run && network.busy() ? 0 : network.address().ipv4;
      const auto page = setup_page(first_run, live, address);
      if (page != SetupPage::None) {
        constexpr int64_t dim_delay_us = 120'000'000;
        if (!setup_language || setup_ipv4 != address)
          setup_dim_at = now + dim_delay_us;
        TouchReport report;
        while (touch.poll(report, rotation)) {
          if (report.valid) {
            confirm_firmware_boot();
            if (report.contacts) {
              setup_dim_at = report.sampled_us + dim_delay_us;
              if (!input.awake())
                schedule.wake(report.sampled_us);
              input.wait_for_new_contact();
            }
          }
        }
        update_backlight(now, false, now >= setup_dim_at);
        const auto language = active_device_language.load(std::memory_order_relaxed);
        if (setup_language != language || setup_ipv4 != address) {
          if (auto *output = display.writable_buffer()) {
            const StatusText text{{status_masks, static_cast<size_t>(status_masks_end - status_masks)}, language};
            if (page == SetupPage::Immich) {
              char url[sizeof("http://255.255.255.255")]{};
              const esp_ip4_addr_t ip{address};
              std::snprintf(url, sizeof(url), "http://" IPSTR, IP2STR(&ip));
              draw_immich_setup(output, text, url, rotation);
            } else {
              draw_wifi_setup(output, text, network.recovery_network(), rotation);
            }
            if (submit_frame(swipe.settled() * viewport.width, false)) {
              if (page == SetupPage::Immich && (!setup_language || setup_ipv4 != address))
                ESP_LOGI(tag, "Immich browser setup instructions displayed");
              setup_language = language;
              setup_ipv4 = address;
            }
          }
        }
        display.wait_for_frame_or_tick();
        continue;
      }
      if (setup_language) {
        setup_language.reset();
        last_submitted = -1;
      }
    }

    const auto center = swipe.settled();
    const auto wanted_rotation = esphome_trial ? rotation : requested_screen_rotation.load(std::memory_order_relaxed);
    if (wanted_rotation != rotation && controls.target_open() && !input.engaged())
      controls.animate(false, now);
    bool at_source_handoff = !awaiting_rotation_frame && swipe.idle() && !input.engaged() && !fade.running() && !fade.held() &&
                                   !controls.target_open() && last_control_progress == 0 &&
                                   !blend.pending() && !display.borrowing() &&
                                   display.presented_position() == center * viewport.width;
    const bool discard_refresh = preparing_refresh &&
        (loader.yield_requested() || (preparing_rotation ? (*preparing_rotation != wanted_rotation || preparing != center)
                                                        : requested_revision != source_revision));
    const bool can_accept_refresh = at_source_handoff && (!preparing_rotation || display.writable_buffer() != nullptr);
    bool refresh_waiting_captions = local_time_became_ready;
    SlideLoader::Completion completion{};
    // Leave a completed replacement in the queue while a gesture can still
    // see the old crop. A superseded replacement can be released immediately.
    if ((!preparing_refresh || can_accept_refresh || discard_refresh) && loader.poll(completion)) {
      const bool ready = completion.result == SlideLoader::Result::Ready && !discard_refresh;
      const bool next_candidate = completion.result == SlideLoader::Result::NextCandidate;
      const bool caption_only = completion.preparation == Preparation::CaptionOnly;
      const bool refining = completion.preparation == Preparation::RefineThumbnail;
      const bool waiting_caption = ready && completion.presentation.caption_time;
      ESP_ERROR_CHECK(cache.complete(completion.lease, ready, waiting_caption, completion.presentation.caption_details,
                                    completion.presentation.thumbnail) ? ESP_OK : ESP_ERR_INVALID_STATE);
      refresh_waiting_captions |= local_time_ready && waiting_caption;
      const auto slide = completion.lease.slide;
      if constexpr (trace_enabled) {
        if (!caption_only) {
          const auto outcome = ready ? RefillOutcome::Ready
                               : completion.cancelled || discard_refresh ? RefillOutcome::Cancelled : RefillOutcome::Retry;
          trace.refill(esp_timer_get_time(), refill_started_us, slide, outcome);
        }
      }
      const bool time_available = local_time_ready && completion.source_status == PhotoSourceStatus::WaitingForTime;
      retries[slide % retries.size()] = ready || completion.cancelled || discard_refresh || time_available
                                           ? SlideRetry{}
                                           : SlideRetry::after(slide, now, completion.connection, next_candidate && !refining);
      if constexpr (trial_immich) {
        if (next_candidate && !completion.cancelled)
          record_live_trial_candidate_retry();
      }
      if (ready) {
        captions[completion.lease.slot] = completion.presentation.caption;
        paired_slots[completion.lease.slot] = completion.presentation.paired;
        if constexpr (trial_detail_metrics) {
          if (completion.presentation.thumbnail)
            record_live_trial_thumbnail_ready(completion.presentation.thumbnail_pair);
          if (detail_request_us && slide == center) {
            last_submitted = -1;
            detail_to_submit = DetailHandoff{slide, detail_request_us};
          }
        }
        prefetch.prepared(slide);
        if constexpr (trial_immich) {
          if (slide > 0)
            record_live_trial_photo(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        }
      }
      const bool completed_source_work = completion.source_ready && !discard_refresh && !caption_only && !refining;
      if (completed_source_work) {
        first_photo = completion.first_photo;
        next_photo = completion.next_photo;
        source_status = completion.source_status;
      }
      if constexpr (!esphome_trial) {
        if (ready && preparing_rotation) {
          // No scanout borrows these slots here, and the old owned frame
          // stays visible until the first frame in the new orientation latches.
          rotation = *preparing_rotation;
          viewport = Viewport{rotation};
          swipe = Swipe{viewport.width, slide_count, center};
          clock.set_rotation(rotation);
          caption_fade = {};
          last_caption = {};
          input.wait_for_new_contact();
          cache.discard_other_ready_slides(center);
          if (completion.rotation_fallback && center > 0 && live)
            cache.mark_ready_for_refresh();
          prefetch = SlidePrefetch(center, cache_slots, viewport.width);
          retries.fill({});
          awaiting_rotation_frame = true;
          at_source_handoff = false;
        }
        if (live && slide > 0 && completed_source_work && !completion.cancelled) {
          frame_status.finish_preparation(ready ? PhotoSourceStatus::None : source_status, completion.response);
          if (!showed_photo || source_anchor >= 0)
            initial_photo_timeout.observe(now, ready, completion.response);
        }
        if (completion.lease.replacement && !discard_refresh) {
          if (next_candidate && !refining) {
            if (caption_only)
              cache.stop_caption_details(slide);
            else
              cache.stop_refresh(slide);
          }
          if (ready && slide == center) {
            last_submitted = -1;
            if (!caption_only && !refining)
              dwell.reset();
          }
        }
      }
      preparing = -1;
      preparing_refresh = false;
      preparing_rotation.reset();
      detail_request_us = 0;
    }
    if constexpr (!esphome_trial) {
      if (refresh_waiting_captions)
        cache.mark_waiting_captions_for_refresh();
      if (local_time_became_ready && source_status == PhotoSourceStatus::WaitingForTime)
        retries.fill({});
      if (source_anchor >= 0 && center != source_anchor && display.presented_position() == center * viewport.width)
        source_anchor = -1;
      if (wanted_rotation != rotation && at_source_handoff) {
        if (const auto slot = cache.ready_slot(center)) {
          if (preparing >= 0) {
            if (!preparing_rotation || *preparing_rotation != wanted_rotation)
              loader.yield_to_swipe();
          } else if (const auto lease = cache.reserve(center, center, center, true)) {
            zoom_cache.reclaim_for_photo_load(center);
            loader.submit(*lease, source_revision, false, Preparation::RotateCached, wanted_rotation,
                          {cache.find(center), rotation, paired_slots[*slot]});
            preparing = center;
            preparing_refresh = true;
            preparing_rotation = wanted_rotation;
          }
        }
      }
      if (live && wanted_rotation == rotation && requested_revision != source_revision &&
          at_source_handoff && cache.find(center)) {
        if (preparing >= 0) {
          loader.yield_to_swipe();
        } else {
          const auto requested_selection = photo_selection_revision.load(std::memory_order_relaxed);
          const bool same_selection = center > 0 && source_anchor < 0 && requested_selection == selection_revision;
          source_revision = requested_revision;
          selection_revision = requested_selection;
          if (same_selection) {
            cache.mark_ready_for_refresh();
          } else {
            // Keep the displayed slide alive as a bridge to the new feed.
            // Its neighbours belong to the old source and can be released now.
            cache.discard_other_ready_slides(center);
            source_anchor = center;
            first_photo = next_photo = center + 1;
            prefetch = SlidePrefetch(center, cache_slots, viewport.width);
          }
          source_status = PhotoSourceStatus::None;
          initial_photo_timeout = {};
          retries.fill({});
          dwell.reset();
        }
      }
    }
    prefetch.moved_to(center);
    // The boot scene may still be visible while the first photo's final frame
    // is pending. Keep its pixels available if the user interrupts that swap.
    const SlideIndex first = source_anchor >= 0 ? source_anchor
                            : live && display.presented_position() >= viewport.width ? first_photo : 0;
    const SlideRange available{first, slide_count - 1};
    const auto [ready_first, ready_last] = cache.ready_range(center, available);
    if constexpr (trial_immich) {
      const auto ahead = static_cast<unsigned>(ready_last - center);
      if (ahead != reported_ahead) {
        record_live_trial_cache(cache_slots, ahead);
        reported_ahead = ahead;
      }
    }
    swipe.set_ready_range(ready_first, ready_last);

    TouchReport report;
    const auto next_report = [&] {
#if SWIPE_TRIAL_BENCHMARK
      // Keep the touch reader running at its normal cadence during this test.
      while (touch.poll(report, rotation)) {
      }
      const bool at_rest = backlight_ready && input.awake() && !input.engaged() && swipe.idle() &&
                           !fade.running() && !fade.held() && display.presented_position() == center * viewport.width;
      if (const auto sample =
              poll_live_trial_benchmark(network, esp_timer_get_time(), center, {ready_first, ready_last}, at_rest)) {
        report = *sample;
        return true;
      }
      return false;
#else
      return touch.poll(report, rotation);
#endif
    };
    while (next_report()) {
      if (!backlight_ready || awaiting_rotation_frame)
        continue;
      if (report.valid) {
        if constexpr (!esphome_trial) {
#if NATIVE_LEGACY_MIGRATION
          if (showed_photo)
#endif
            confirm_firmware_boot();
        }
        if (report.contacts != 0 && fade.running()) {
          if (fade.interrupt(snapshot != nullptr))
            pin_fade();
          if constexpr (capture_motion)
            motion_epoch.update(MotionPhase::Still);
        }
      }
      const bool was_dragging = swipe.dragging();
      const auto previous_contact = input.contact_started_us();
      SwipeInput::DragEnd drag_end{};
      const auto effect = input.update(swipe, report, display.handoff_position(),
                                       track_physical_drags ? &drag_end : nullptr);
      if constexpr (stage_at_edge) {
        if (swipe.dragging())
          prefetch.note_drag(swipe.position() - swipe.origin_slide() * viewport.width, report.sampled_us);
      }
      if constexpr (track_physical_drags) {
        if (previous_contact != input.contact_started_us() && physical_landing.interrupt())
          record_live_trial_drag_outcome(TrialDragOutcome::Interrupted);
        if (drag_end == SwipeInput::DragEnd::Released) {
          physical_landing.released(swipe.origin_slide());
          if (swipe.idle() && swipe.position() == display.presented_position())
            record_landing();
        } else if (drag_end == SwipeInput::DragEnd::Cancelled) {
          record_live_trial_drag_outcome(TrialDragOutcome::Cancelled);
        }
      }
      if constexpr (capture_motion) {
        if (was_dragging && !swipe.dragging())
          motion_epoch.update(MotionPhase::Still);
      }
      if constexpr (trial_immich)
        drag_demand.observe(esp_timer_get_time(), swipe.dragging(), swipe.position(), last_submitted);
      if (effect == SwipeInput::Effect::Sleep) {
        manual_brightness.reset();
        schedule.sleep();
        suspend_fade();
      } else if (effect == SwipeInput::Effect::Wake) {
        manual_brightness.reset();
        schedule.wake(report.sampled_us);
      }
      if constexpr (!SWIPE_TRIAL_BENCHMARK) {
        if (effect == SwipeInput::Effect::Tap && swipe.idle() && swipe.settled() > 0 &&
            swipe.position() == display.handoff_position() && !fade.running() && !fade.held() && !blend.pending() &&
            network.recovery_status() == NativeNetwork::RecoveryStatus::Off && !recovery_painted &&
            cache.find(swipe.settled())) {
          const bool opening = !controls.target_open();
          if (opening || last_control_progress < PhotoControls::open || input.contact_y() >= PhotoControls::toolbar_bottom)
            controls.animate(opening, esp_timer_get_time(), clock.visible() || last_caption.opacity != 0);
        }
        if (swipe.dragging() && !was_dragging && (controls.target_open() || last_control_progress != 0))
          controls.animate(false, esp_timer_get_time());
      }
      if (swipe.dragging() && fade.held())
        fade.note_drag();
    }

    now = esp_timer_get_time();
    update_backlight(now, input.engaged() || !swipe.idle(), false);
    if ((controls.target_open() || last_control_progress != 0) &&
        (!input.awake() || network.recovery_status() != NativeNetwork::RecoveryStatus::Off))
      controls.reset();
    // A busy or interrupted zoom worker must return its spare framebuffer
    // even if the normal controls rendering branch is skipped by a fade.
    zoom_cache.service(display, cache, !input.awake() || input.engaged() || !swipe.idle() || fade.running() ||
                       fade.held() || controls.animating() || awaiting_rotation_frame || prefetch.requested().has_value());
    const bool cached_resize = swipe.idle() && zoom_cache.available_for(cache, swipe.settled(), rotation);
    const auto control_frame = controls.next_frame(now, refresh_period_us, cached_resize);
    const int control_progress = control_frame.progress;
    const uint8_t text_opacity = controls.text_opacity(now);

    if (blend.poll_complete() && fade.running()) {
      // Touch is handled before this block so a superseded blend can never
      // replace the frame the finger just took over.
      auto *output = display.writable_buffer();
      if (snapshot != nullptr)
        clock.restore_blend(output, cache.find(fade.target()), pending_opacity);
      submit_autoplay_frame(output, pending_opacity);
    }

    const SlideRange ready{ready_first, ready_last};
    const auto edge_position = swipe.position();
    if (swipe.dragging() || (swipe.settling() && missing_neighbor_at_edge(edge_position, ready, available, viewport.width)))
      prefetch.request_at_edge(edge_position, ready, available);
    // Keep a cache miss urgent through the snap-back and into idle.
    if (const auto wanted = prefetch.requested(); wanted && (cache.find(*wanted) || !available.contains(*wanted)))
      prefetch.prepared(*wanted);
    const auto keep = prefetch.window(available);
    const auto active_connection = network.photo_connection();
    const auto eligible = [&](SlideIndex candidate) {
      // Live photos have to be assigned in order. Trying a later index would
      // give it a retry delay even though no request for it has gone to Immich.
      // A gesture taking over a transition can reach beyond the speculative window.
      const bool wanted = keep.contains(candidate) || candidate == prefetch.requested();
      return wanted && available.contains(candidate) && (!live || trial_fixtures || candidate <= next_photo) &&
             retries[candidate % retries.size()].due(candidate, now, active_connection);
    };
    // Let a stationary contact give time back to the worker. Protect the
    // start of a swipe even when only its landing is ready.
    const bool contact_engaged = input.engaged();
    if constexpr (stage_at_edge || stage_all_pairs) {
      if (!at_source_handoff || contact_engaged)
        last_detail_motion_us = now;
    }
    user_motion = input.awake() && (contact_engaged ||
                  (user_motion && (!swipe.idle() || swipe.position() != display.presented_position())));
    bool pixel_work_deferred = false;
    bool protect_cached_copy = false;
    if (live && !trial_fixtures) {
      const auto drag = swipe.drag_activity(now, drag_moving_window_us, drag_start_guard_us);
      const bool first_motion_pending = input.first_motion_pending() || submitted_motion_us != 0;
      const auto origin = swipe.origin_slide();
      const bool can_defer = drag != Swipe::DragActivity::Still
                                 ? prefetch.can_defer_drag(ready, origin, swipe.position(),
                                                           drag == Swipe::DragActivity::Starting, first_motion_pending)
                                 : !contact_engaged && prefetch.can_defer_settle(ready, swipe.settle_destination());
      const bool scaling_at_rest = controls.animating() && !swipe.dragging() && !swipe.settling();
      pixel_work_deferred = (user_motion && can_defer) || scaling_at_rest;
      protect_cached_copy = drag == Swipe::DragActivity::Starting && !first_motion_pending && !can_defer &&
                            !prefetch.requested() && ready.contains(origin) && ready.last == origin + 1 &&
                            swipe.position() > origin * viewport.width;
      loader.defer_pixel_work(pixel_work_deferred);
    }
    if (preparing >= 0) {
      if (const auto wanted = prefetch.requested(); wanted && eligible(*wanted)) {
        if ((!preparing_rotation || contact_engaged) && prefetch.can_yield(preparing, *wanted, preparing_refresh)) {
          loader.yield_to_swipe();
        } else if constexpr (stage_at_edge) {
          if (preparing == *wanted && source_anchor < 0 && requested_revision == source_revision &&
              prefetch.can_promote(preparing) && loader.promote_preview()) {
            prefetch.promoted(preparing);
            if constexpr (trial_detail_metrics)
              record_live_trial_thumbnail_promotion();
          }
        }
      }
    } else if (!awaiting_rotation_frame && !blend.pending() && (esphome_trial || !backlight_ready || input.awake())) {
      // Until PPA finishes, its input slides must stay readable even if a
      // touch has moved the cache window in the other direction.
      const auto retained_during_motion = swipe.retain_for_motion(keep, display.handoff_position());
      const auto submit = [&](SlideIndex candidate, SlideRange retained, bool refresh,
                              Preparation preparation = Preparation::FullPreview) {
        retained = display.retain_sources(retained);
        const auto lease = cache.reserve(candidate, retained.first, retained.last, refresh);
        if (!lease)
          return false;
        if constexpr (trace_enabled)
          refill_started_us = esp_timer_get_time();
        const auto zoom_landing = swipe.settling() ? swipe.settle_destination() : center;
        zoom_cache.reclaim_for_photo_load(zoom_landing);
        loader.submit(*lease, source_revision, source_anchor >= 0, preparation, rotation, {});
        preparing = candidate;
        preparing_refresh = refresh;
        return true;
      };
      if constexpr (stage_at_edge || stage_all_pairs) {
        // Leave a forward swipe ready before spending the pause sharpening
        // the current slide. A new edge drag can cancel the replacement.
        if (center > 0 && source_anchor < 0 && requested_revision == source_revision && active_connection &&
            at_source_handoff && !contact_engaged && cache.has_thumbnail(center) &&
            now - last_detail_motion_us >= detail_idle_us && eligible(center) &&
            (ready.last > center || !eligible(center + 1)) &&
            submit(center, prefetch.nearby_window(), true, Preparation::RefineThumbnail)) {
          if constexpr (trial_detail_metrics)
            detail_request_us = now;
        }
      }
      for (const auto candidate : prefetch.candidates(keep)) {
        if (preparing >= 0)
          break;
        if (!eligible(candidate))
          continue;
        const bool refresh = !esphome_trial && cache.needs_refresh(candidate);
        if (refresh && (!at_source_handoff || contact_engaged))
          continue;
        // Apply a visible change promptly, but keep upcoming swipes ready
        // before redrawing offscreen slides that already have pixels.
        if (refresh && candidate != center && prefetch.has_nearby_refill(cache, eligible))
          continue;
        const auto retained = refresh ? prefetch.nearby_window() : retained_during_motion;
        const bool staged = !refresh && (stage_all_pairs ||
                            (stage_at_edge && source_anchor < 0 && requested_revision == source_revision &&
                             prefetch.should_stage_forward(candidate, ready, now)));
        if (submit(candidate, retained, refresh, staged ? Preparation::AllowThumbnail : Preparation::FullPreview))
          break;
      }
      if constexpr (!esphome_trial) {
        if (preparing < 0 && at_source_handoff && !contact_engaged && active_connection && eligible(center) &&
            !cache.has_thumbnail(center) && cache.needs_caption_details(center) &&
            !prefetch.has_nearby_refill(cache, eligible)) {
          // A detail lookup may paint a richer caption once nearby photos
          // are ready. Initial playback never waits for this request.
          submit(center, prefetch.nearby_window(), true, Preparation::CaptionOnly);
        }
      }
    }
    const bool was_settling = swipe.settling();
    swipe.tick(now);
    auto position = swipe.position();
    const bool photo_at_rest = backlight_ready && input.awake() && !display.borrowing() &&
                               position == display.presented_position();
    const bool visible_and_idle = photo_at_rest && swipe.idle() && !input.engaged();
    if constexpr (track_physical_drags) {
      if (visible_and_idle)
        record_landing();
      else if (!input.awake() && physical_landing.interrupt())
        record_live_trial_drag_outcome(TrialDragOutcome::Interrupted);
    }
    if (fade.held() && !blend.pending() && visible_and_idle) {
      if (can_fade && swipe.settled() == fade.anchor()) {
        fade.resume(now);
        // The handoff is already a composite. Blend from its exact pixels
        // so taking a finger off never restarts the original animation.
        effect = slide_transition_plan(SlideTransition::CROSS_FADE);
      } else {
        if (swipe.settled() == fade.anchor())
          last_submitted = -1;  // Restore the clean anchor if the PPA failed.
        fade.finish();
        display.unpin();
        snapshot = nullptr;
      }
    }
    int ring_progress = -1;
    if (live) {
      const bool first_photo = swipe.settled() == 0;
      const bool source_changed = source_anchor == swipe.settled();
      const bool dwelling = wanted_rotation == rotation && visible_and_idle && !fade.running() && !fade.held() &&
                            !controls.target_open() && control_progress == 0 && last_control_progress == 0 &&
                            text_opacity == PhotoControls::opaque;
      // Keep the settled photo available for inspection during the touch replay.
      constexpr bool hold_photo = SWIPE_TRIAL_REPLAY_HISTORY || SWIPE_TRIAL_WIFI_OUTAGE || trial_replay_touch;
      const auto duration = first_photo || source_changed ? 0 : loader.slide_interval_us();
      const bool advance_due = (!hold_photo || first_photo) && dwell.elapsed(now, dwelling, duration);
      if (dwelling && duration > 0 && !hold_photo &&
          active_slideshow_progress_ring.load(std::memory_order_relaxed))
        ring_progress = dwell.progress_tenths_of_a_degree(now, duration);
      if (advance_due && zoom_cache.release_before_transition()) {
        const auto anchor = swipe.settled();
        if (first_photo) {
          swipe.advance(now, Swipe::Transition::Quick);
        } else if (anchor < ready_last) {
          const auto transition = loader.playback_transition();
          previous_transition = transition.random
                                    ? esphome::espframe::pick_random_slide_transition(esp_random(), previous_transition)
                                    : transition.effect;
          effect = slide_transition_plan(previous_transition);
          if (effect.blend == TransitionBlend::NONE) {
            if (display.writable_buffer() != nullptr) {
              swipe.show(anchor + 1);
              position = swipe.position();
              dwell.reset();
              ring_progress = -1;
            }
          } else if (previous_transition == SlideTransition::SLIDE_LEFT) {
            // The automatic move follows the same slide order as a swipe.
            // Keep its real position so touch can grab the visible seam.
            swipe.advance(now, Swipe::Transition::Glide, transition.duration_ms * 1000LL);
          } else if (can_fade) {
            fade.start(anchor, anchor + 1, now, transition.duration_ms * 1000LL);
          } else {
            swipe.advance(now, Swipe::Transition::Glide);
          }
        }
      }
    }

    if constexpr (capture_motion) {
      const bool final_settle = motion_epoch.phase() == MotionPhase::Settle &&
                                !input.engaged() && position != last_submitted;
      MotionPhase phase = MotionPhase::Still;
      if (fade.running())
        phase = MotionPhase::Crossfade;
      else if (swipe.dragging())
        phase = MotionPhase::Drag;
      else if (swipe.settling() || was_settling || final_settle)
        phase = MotionPhase::Settle;
      motion_epoch.update(input.awake() ? phase : MotionPhase::Still);
    }
    if constexpr (trial_immich)
      drag_demand.observe(esp_timer_get_time(), motion_epoch.phase() == MotionPhase::Drag, position, last_submitted);

    // A tap on the displayed photo is still rest. An active drag, a pending
    // neighbour, or a photo grabbed midway through a transition is not.
    std::optional<SlideIndex> settled_caption;
    if (photo_at_rest && !swipe.dragging() && !swipe.settling() &&
        !fade.running() && !fade.held() && swipe.settled() > 0 &&
        position == swipe.settled() * viewport.width && position == display.presented_position())
      settled_caption = swipe.settled();
    auto caption_sample = caption_fade.update(now, settled_caption);
    caption_sample.opacity = (caption_sample.opacity * text_opacity + 127) / 255;
    if (blend.pending()) {
      display.wait_for_frame_or_tick();
      continue;
    }
    if (fade.running()) {
      const auto opacity = fade.opacity(now);
      auto *output = display.writable_buffer();
      if (output != nullptr && opacity > fade.submitted_opacity()) {
        const auto *target = cache.find(fade.target());
        const auto *from = snapshot != nullptr ? snapshot : cache.find(fade.anchor());
        if (target && from) {
          if (opacity == AutoplayFade::opaque) {
            blitter.slides(output, {target, nullptr, 0});
            last_caption = {};
            last_ring_progress = -1;
            clock.draw(output);
            clock_dirty = false;
            swipe.show(fade.target());
            last_submitted = last_inside = swipe.position();
            submit_frame(last_submitted);
            recovery_painted = false;
            painted_source_status = PhotoSourceStatus::None;
            painted_connection_notice = ImmichResponse::Pending;
            fade.finish();
            display.unpin();
            snapshot = nullptr;
          } else if (effect.blend == TransitionBlend::MOVE) {
            blitter.push(output, from, target, effect.dx, effect.dy, opacity, rotation);
            submit_autoplay_frame(output, opacity);
          } else {
            bool started;
            if (effect.blend == TransitionBlend::FADE_THROUGH) {
              const int phase = 2 * opacity;
              const bool incoming = phase >= AutoplayFade::opaque;
              started = blend.start(incoming ? target : from, incoming ? from : target, output,
                                    incoming ? 2 * AutoplayFade::opaque - phase : phase, effect.color_rgb888);
            } else {
              started = blend.start(from, target, output, opacity);
            }
            if (started) {
              pending_opacity = opacity;
            } else {
              ESP_LOGW(tag, "Could not start the frame blend; using the slideshow glide");
              const bool advance = fade.target() != fade.anchor();
              if (fade.interrupt(snapshot != nullptr))
                pin_fade();
              blend.release();
              can_fade = false;
              if (advance)
                swipe.advance(now, Swipe::Transition::Glide);
            }
          }
        }
      }
      display.wait_for_frame_or_tick();
      continue;
    }

    const auto inside = std::clamp(position, ready_first * viewport.width, ready_last * viewport.width);
    const bool use_edge = inside != position;
    const bool show_clock = live && swipe.settled() != 0;
    const bool show_notice = backlight_ready && input.awake() && !input.engaged() && swipe.idle() && !fade.held();
    const bool show_recovery = show_notice && network.recovery_status() != NativeNetwork::RecoveryStatus::Off;
    const bool show_source_notice = show_notice && !show_recovery && ready_last == center;
    const auto connection_notice = !esphome_trial && live && show_source_notice && (!showed_photo || source_anchor >= 0)
                                       ? initial_photo_timeout.notice(now, active_connection_timeout.load(std::memory_order_relaxed))
                                       : ImmichResponse::Pending;
    const auto source_notice = show_source_notice && connection_notice == ImmichResponse::Pending
                                   ? source_status : PhotoSourceStatus::None;
    const bool paints_notice = show_recovery || connection_notice != ImmichResponse::Pending ||
                               source_notice != PhotoSourceStatus::None;
    const auto notice_language = paints_notice ? active_device_language.load(std::memory_order_relaxed) : uint8_t{0};
    const bool sliding = swipe.dragging() || swipe.settling();
    if (sliding || paints_notice)
      ring_progress = -1;
    const bool zoom_source_valid = !sliding && !use_edge && position == center * viewport.width && !fade.held();
    // This worker has lower priority than photo preparation. Start on the
    // visible slide while downloads proceed, and yield to urgent edge refill.
    const bool zoom_can_prepare = zoom_source_valid && visible_and_idle && center > 0 && !prefetch.requested() &&
                                  !awaiting_rotation_frame && !paints_notice && !controls.animating();
    zoom_cache.update(display, cache, center, rotation, zoom_can_prepare, preparing < 0 && ready_last > center);
    const auto *zoom_source = zoom_source_valid ? cache.find(center) : nullptr;
    const bool zoom_ready = zoom_source && zoom_cache.available_for(cache, center, rotation);
    if (!zoom_ready || prepared_zoom.source != zoom_source || prepared_zoom.progress != control_progress ||
        prepared_zoom.rotation != rotation)
      prepared_zoom = {};
    if (const int step = PhotoControls::cached_step(control_progress);
        step && control_progress != last_control_progress && zoom_ready && !prepared_zoom.pixels) {
      if (auto *scratch = display.preparation_buffer(); scratch && zoom_cache.decode_for(cache, center, rotation, step, scratch))
        prepared_zoom = {scratch, zoom_source, control_progress, rotation};
    }
    const bool clock_changed = show_clock && (clock_dirty || (clock.visible() && text_opacity != last_text_opacity));
    const auto caption_slot = caption_sample.opacity && !show_recovery ? cache.ready_slot(caption_sample.slide)
                                                                      : std::nullopt;
    const auto *caption = caption_slot && captions[*caption_slot].band_rows() &&
                                  (!clock.visible() || captions[*caption_slot].fits_beside_clock(rotation))
                              ? &captions[*caption_slot]
                              : nullptr;
    if (!caption)
      caption_sample = {};
    if (auto *output = display.writable_buffer(); output != nullptr &&
        (position != last_submitted || inside != last_inside || control_progress != last_control_progress ||
         (control_progress == 0 &&
          (show_recovery != recovery_painted || source_notice != painted_source_status ||
           connection_notice != painted_connection_notice || notice_language != painted_notice_language ||
           (display.borrowing() && !sliding) || clock_changed || caption_sample != last_caption ||
           ring_progress != last_ring_progress)))) {
      const auto index = inside / viewport.width;
      const int offset = inside % viewport.width;
      const auto slide_pixels = [&](SlideIndex slide) {
        return control_progress == 0 && snapshot != nullptr && slide == fade.anchor() ? snapshot : cache.find(slide);
      };
      const auto *left = slide_pixels(index);
      const auto *right = offset == 0 ? nullptr : slide_pixels(index + 1);
      if (left != nullptr && (offset == 0 || right != nullptr)) {
        const auto motion = input.take_first_motion();
        const auto started_render = motion != 0 ? esp_timer_get_time() : 0;
        const auto limit = PixelPosition{viewport.width - 1};
        const int pull = static_cast<int>(std::clamp(position - inside, -limit, limit)) * axis_direction(rotation);
        const auto span = orient_slide_span(left, right, viewport.width, offset, rotation);
        const auto finish_render = [&](int64_t started_submit) {
          submitted_motion_us = motion;
          motion_render_us = motion != 0 ? esp_timer_get_time() - started_render : 0;
          if constexpr (capture_motion) {
            if (motion != 0)
              first_motion = {motion, input.contact_started_us(), motion_render_us,
                              ready_at_motion(position, {ready_first, ready_last}, available, viewport.width), preparing >= 0,
                              started_submit != 0 ? started_render + motion_render_us - started_submit : 0};
          }
          last_submitted = position;
          last_inside = inside;
          last_control_progress = control_progress;
          controls.submitted(control_frame);
          last_text_opacity = text_opacity;
        };
        if (control_progress != 0) {
          const auto *prepared = prepared_zoom.pixels;
          if (!prepared) {
            controls.prepare(output, span, pull, rotation, control_progress);
            controls_renderer.draw();
          }
          const auto started_submit = trial_immich && motion != 0 ? esp_timer_get_time() : 0;
          if (!submit_frame(position, position != last_submitted, use_edge, nullptr, {}, prepared)) {
            prepared_zoom = {};
            display.wait_for_frame_or_tick();
            continue;
          }
          last_ring_progress = -1;
          finish_render(started_submit);
          prepared_zoom = {};
          display.wait_for_frame_or_tick();
          continue;
        }
        const auto render_owned = [&] {
          if (use_edge) {
            blitter.edge(output, left, pull, viewport.portrait);
            if (left == snapshot)
              clock.restore_slide_slice(output, std::max(0, pull), std::max(0, -pull), viewport.width - std::abs(pull));
          } else {
            blitter.slides(output, span, viewport.portrait);
            if (span.first == snapshot)
              clock.restore_slide_slice(output, span.offset, 0, viewport.width - span.offset);
            if (span.offset != 0 && span.second == snapshot)
              clock.restore_slide_slice(output, 0, viewport.width - span.offset, span.offset);
          }
        };
        const auto paint_caption = [&] {
          if (caption)
            paint_photo_caption(output, caption_glyphs, date_glyphs, *caption, rotation, caption_sample.opacity);
        };
        // Protect the first frame of a resisted drag, but let the urgent
        // refill run between copies and after the initial touch window.
        const bool protect_edge_copy = use_edge && live && !trial_fixtures && !pixel_work_deferred &&
                                       swipe.drag_activity(now, drag_moving_window_us, edge_drag_guard_us) ==
                                           Swipe::DragActivity::Starting;
        const bool pause_for_copy = preparing >= 0 && position != last_submitted &&
                                    (use_edge ? protect_edge_copy : protect_cached_copy);
        if (pause_for_copy)
          loader.defer_pixel_work(true);
#if SWIPE_LINKED_SCANOUT
        std::optional<ScanoutFrame> segmented;
        int64_t segment_started{};
        // Present the destination promptly. Once it is stationary, the
        // ordinary owned submission releases the source after its handoff.
        const bool landing = !use_edge && offset == 0 && position != last_submitted &&
                             position == swipe.settle_destination() * viewport.width &&
                             (swipe.settling() || (swipe.idle() && display.borrowing()));
        if (linked_scanout_ok && !viewport.portrait && input.awake() && snapshot == nullptr && !fade.held() && !paints_notice &&
            ring_progress < 0 && ((sliding && (use_edge || offset != 0)) || landing)) {
          segment_started = esp_timer_get_time();
          const auto copy_rows = [&](auto *to, const auto *from, int rows) { blitter.copy_panel_rows(to, from, rows); };
          const bool paint_clock = show_clock && clock.visible() && text_opacity != 0;
          const int caption_rows = caption ? caption->band_rows() : 0;
          if (use_edge)
            segmented = prepare_edge_scanout(output, left, pull, paint_clock, rotation, copy_rows, caption_rows);
          else
            segmented = prepare_scanout(output, span, paint_clock, rotation, copy_rows, caption_rows);
        }
        if (!segmented)
#endif
          render_owned();
        if (pause_for_copy)
          loader.defer_pixel_work(false);
        if (show_clock) {
          clock.draw(output, false, text_opacity);
          clock_dirty = false;
        }
        paint_caption();
        const StatusText status_text{{status_masks, static_cast<size_t>(status_masks_end - status_masks)}, notice_language};
        if (show_recovery)
          draw_recovery_card(output, recovery_atlas, status_text, network.recovery_network(), rotation);
        else if (connection_notice != ImmichResponse::Pending)
          draw_connection_notice(output, status_text, connection_notice, rotation);
        else if (source_notice != PhotoSourceStatus::None)
          draw_source_status(output, status_text, source_notice, rotation);
        recovery_painted = show_recovery;
        painted_source_status = source_notice;
        painted_connection_notice = connection_notice;
        painted_notice_language = notice_language;
        const auto started_submit = trial_immich && motion != 0 ? esp_timer_get_time() : 0;
        bool submitted = false;
#if SWIPE_LINKED_SCANOUT
        if (segmented) {
          segmented->prepare_us = static_cast<uint32_t>(esp_timer_get_time() - segment_started);
          submitted = submit_frame(position, position != last_submitted, use_edge, &*segmented,
                                   SlideRange{index, (use_edge || offset == 0) ? index : index + 1});
          if (!submitted) {
            linked_scanout_ok = false;
            render_owned();
            if (show_clock)
              clock.draw(output, false, text_opacity);
            paint_caption();
          }
        }
#endif
        if (!submitted) {
          if (ring_progress >= 0)
            paint_slideshow_progress_ring(output, viewport, ring_progress);
          submit_frame(position, position != last_submitted, use_edge);
        }
        last_ring_progress = ring_progress;
        last_caption = caption_sample;
        finish_render(started_submit);
      }
    }
    display.wait_for_frame_or_tick();
  }
}

}  // namespace

void start_player() {
  if constexpr (esphome_trial) {
    constexpr bool short_trial = trial_fixtures || (synthetic_trial_replay && !trial_replay_touch);
    constexpr unsigned trial_seconds = short_trial ? 120 : trial_immich ? 300 : 180;
    if (!start_esphome_trial(trial_seconds))
      return;
  } else {
    start_web_logs();
    ESP_LOGI(tag, "Reset reason: %s", last_reset_reason().data());
    start_usb_setup(network);
  }
  // Legacy startup checks its migrated document before the player loads it.
  // Parsing the full ESPHome settings snapshot needs more stack than native setup.
#if NATIVE_LEGACY_MIGRATION
  constexpr uint32_t player_stack_bytes = 16384;
#else
  constexpr uint32_t player_stack_bytes = !esphome_trial ? 8192 : trial_immich ? 6144 : 4096;
#endif
  const auto result = xTaskCreatePinnedToCore(run, "photo-player", player_stack_bytes, nullptr, player_priority, nullptr,
                                            player_core);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

}  // namespace espframe::native

extern "C" void app_main() { espframe::native::start_player(); }
