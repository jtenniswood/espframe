#include "autoplay_fade.h"
#include "clock_overlay.h"
#include "daylight.h"
#include "display.h"
#include "esphome_trial.h"
#include "firmware_update.h"
#include "frame_blend.h"
#include "immich.h"
#include "live_trial.h"
#include "native_network.h"
#include "recovery_card.h"
#include "screen_schedule.h"
#include "slide_blit.h"
#include "slide_cache.h"
#include "slide_dwell.h"
#include "slide_prefetch.h"
#include "swipe.h"
#include "swipe_input.h"
#include "touch.h"
#include "trace_logger.h"
#include "usb_setup.h"

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <limits>

namespace espframe::native {
namespace {

constexpr const char *tag = "swipe-probe";
constexpr int fixture_count = 5;
constexpr int fixture_slides = 30;
constexpr int player_core = 1;
constexpr int loader_core = 0;
constexpr UBaseType_t player_priority = 4;
constexpr bool trace_enabled = SWIPE_TRACE;
constexpr bool capture_motion = SWIPE_TRACE || SWIPE_TRIAL_BENCHMARK;
constexpr bool esphome_trial = SWIPE_ESPHOME_TRIAL;
constexpr bool trial_immich = SWIPE_TRIAL_IMMICH;
constexpr bool trial_fixtures = SWIPE_TRIAL_BENCHMARK_FIXTURES;
extern const uint8_t clock_atlas[] asm("_binary_clock_glyphs_bin_start");
extern const uint8_t recovery_atlas[] asm("_binary_recovery_glyphs_bin_start");
NativeNetwork network;

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
  const int column_step = -axis_direction(rotation);
  for (int x = 0; x < slide_width; ++x) {
    auto *column = destination + panel_offset(x, panel_width - 1, rotation);
    const int scene = slide == 3 && x >= slide_width / 2 ? 4 : slide;
    const float phase = scene * 1.6f + x * 0.008f;
    const int ridge = static_cast<int>(485 + 90 * std::sin(phase) + 35 * std::sin(phase * 2.4f));
    const int shore = static_cast<int>(635 + 23 * std::sin(phase * 0.6f + 1.0f));
    const int sun_x = 260 + scene * 146;
    const int sun_y = 200 + scene * 21;
    for (int y = 0; y < slide_height; ++y) {
      uint16_t color;
      if (slide == 3 && std::abs(x - slide_width / 2) <= 2) {
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
      column[y * column_step] = color;
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

SlideCache::Buffers allocate_slide_buffers() {
  SlideCache::Buffers buffers{};
  for (auto &pixels : buffers) {
    constexpr size_t bytes = pixel_count * sizeof(uint16_t);
    pixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(pixels != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  }
  return buffers;
}

class SlideLoader {
 public:
  using Result = ImmichSource::PrepareResult;

  struct Completion {
    SlideCache::Lease lease;
    SlideIndex first_photo;
    SlideIndex next_photo;
    uint32_t connection;
    MemoriesStatus memories_status;
    Result result;
    bool cancelled;
  };

  bool live() const { return this->live_; }
  int64_t slide_interval_us() const { return this->settings_.slide_seconds * 1'000'000LL; }
  uint8_t brightness() const { return this->settings_.brightness_day; }
  uint8_t night_brightness() const { return this->settings_.night_brightness(); }
  const ClockSettings &clock() const { return this->settings_.clock; }
  bool photos_need_time() const {
    return this->settings_.metadata.needs_time() || this->settings_.tone.night_enabled ||
           this->settings_.photo_source.mode == PhotoSourceMode::Memories || this->settings_.capture_dates.needs_time();
  }
  const ScreenScheduleSettings &schedule() const { return this->settings_.schedule; }
  ScreenRotation rotation() const { return this->settings_.screen_rotation; }

  void start_network() const {
    network.start(this->settings_);
  }

  bool start() {
    if constexpr (trial_immich) {
      if (!this->settings_.read_esphome_trial()) {
        ESP_LOGW(tag, "Could not read compatible ESPHome Wi-Fi and Immich settings");
        end_esphome_trial();
        return false;
      }
      this->live_ = true;
    } else {
      this->live_ = !esphome_trial && this->settings_.read() && this->settings_.has_immich();
    }
    this->requests_ = xQueueCreate(1, sizeof(SlideCache::Lease));
    this->completed_ = xQueueCreate(1, sizeof(Completion));
    if constexpr (!trace_enabled)
      this->timings_ = xQueueCreate(32, sizeof(GestureTiming));
    ESP_ERROR_CHECK(this->requests_ && this->completed_ && (trace_enabled || this->timings_) ? ESP_OK : ESP_ERR_NO_MEM);
    const auto result = xTaskCreatePinnedToCore(run_, "slide-prepare", 6144, this, 2, &this->task_, loader_core);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    return true;
  }

  void submit(const SlideCache::Lease &lease) {
    this->cancelled_ = false;
    ESP_ERROR_CHECK(xQueueSend(this->requests_, &lease, 0) == pdPASS ? ESP_OK : ESP_ERR_INVALID_STATE);
    xTaskNotifyGive(this->task_);
  }

  bool poll(Completion &completion) {
    return xQueueReceive(this->completed_, &completion, 0) == pdPASS;
  }

  void yield_to_swipe() { this->cancelled_ = true; }

  void report_gesture(const GestureTiming &timing) {
    // USB output can block. Leave it to the worker and drop a diagnostic
    // sample if downloads keep the worker occupied for too long.
    if (xQueueSend(this->timings_, &timing, 0) == pdPASS)
      xTaskNotifyGive(this->task_);
  }

 private:
  static void run_(void *context) {
    auto &loader = *static_cast<SlideLoader *>(context);
    jpeg_decoder_handle_t decoder{};
    while (true) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if constexpr (!trace_enabled) {
        GestureTiming timing;
        while (xQueueReceive(loader.timings_, &timing, 0) == pdPASS)
          ESP_LOGI(tag, "gesture_to_scanout_us=%" PRId64 " compose_and_submit_us=%" PRId64,
                   timing.gesture_to_scanout_us, timing.compose_and_submit_us);
      }
      SlideCache::Lease lease{};
      if (xQueueReceive(loader.requests_, &lease, 0) != pdPASS)
        continue;
      const auto connection = network.photo_connection();
      auto result = Result::Ready;
      if ((!esphome_trial || trial_immich) && !trial_fixtures && loader.live_ && lease.slide > 0) {
        if constexpr (trial_immich)
          record_live_trial_prepare();
        result = loader.source_.prepare(lease.slide, lease.pixels);
      } else {
        const int fixture = lease.slide % fixture_count;
        if (!decode_photo(photos[fixture], lease.pixels, decoder))
          draw_test_slide(lease.pixels, fixture, loader.rotation());
        else if (loader.rotation() == ScreenRotation::UpsideDown)
          std::reverse(lease.pixels, lease.pixels + pixel_count);
      }
      const Completion completion{lease, loader.source_.first_slide(), loader.source_.next_slide(), connection,
                                  loader.source_.memories_status(), result,
                                  result != Result::Ready && loader.cancelled_.load()};
      xQueueSend(loader.completed_, &completion, portMAX_DELAY);
    }
  }

  std::atomic<bool> cancelled_{};
  Settings settings_;
  ImmichSource source_{settings_, network, cancelled_};
  QueueHandle_t requests_{};
  QueueHandle_t completed_{};
  QueueHandle_t timings_{};
  TaskHandle_t task_{};
  bool live_{};
};

void run(void *) {
  if constexpr (!esphome_trial)
    start_firmware_boot_check();
  static SlideLoader loader;
  if (!loader.start()) {
    vTaskDelete(nullptr);
    return;
  }
  SlideCache cache{allocate_slide_buffers()};
  const bool live = loader.live();
  const auto rotation = loader.rotation();
  const SlideIndex slide_count = live && !trial_fixtures ? std::numeric_limits<PixelPosition>::max() / slide_width
                                                        : fixture_slides;
  FrameBlend blend;
  bool can_fade = live && blend.init();
  static Display display;
  display.init(can_fade);
  ClockOverlay clock;
  const auto day_level = loader.brightness();
  const auto night_level = loader.night_brightness();
  const bool solar_brightness = day_level != night_level;
  const bool use_local_time = loader.clock().show || loader.schedule().enabled || solar_brightness;
  const bool local_time_configured = (use_local_time || loader.photos_need_time()) &&
                                     configure_local_time(loader.clock().timezone);
  Daylight daylight{solar_brightness ? find_clock_zone(loader.clock().timezone) : nullptr};
  uint8_t brightness = day_level;
  uint8_t applied_brightness = 0;
  const bool can_layer_fade = clock.init(loader.clock(), clock_atlas, can_fade && display.can_pin_frame(), rotation);
  if (can_fade && (!display.can_pin_frame() || !can_layer_fade)) {
    blend.release();
    can_fade = false;
  }
  if (live && !can_fade)
    ESP_LOGI(tag, "Crossfade unavailable; using the slideshow glide");
  TouchReader touch;
  touch.start(rotation);
  Swipe swipe{slide_width, slide_count, live ? 0 : 2};
  SwipeInput input;
  ScreenSchedule schedule{loader.schedule()};
  SlideDwell dwell;
  SlidePrefetch prefetch{swipe.settled()};
  AutoplayFade fade;
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
  bool recovery_painted = false;
  MemoriesStatus memories_status = MemoriesStatus::None;
  MemoriesStatus painted_memories_status = MemoriesStatus::None;
  std::optional<int> local_hour;

  int64_t submitted_motion_us = 0;
  int64_t motion_render_us = 0;
  PixelPosition last_submitted = -1;
  PixelPosition last_inside = -1;
  bool backlight_ready = false;
  std::optional<SlideIndex> preparing;
  static TraceLogger trace;
  MotionEpoch motion_epoch;
  MotionStamp submitted_stamp;
  FirstMotionTrace first_motion;
  int64_t refill_started_us{};
  if constexpr (trace_enabled)
    trace.start(Display::nominal_frame_period_us());
  if constexpr (!esphome_trial || trial_immich)
    loader.start_network();
  const auto submit_frame = [&](PixelPosition position, bool moving = true) {
    display.submit(position);
    if constexpr (capture_motion)
      submitted_stamp = moving ? motion_epoch.stamp(preparing.has_value()) : MotionStamp{};
  };
  SlideIndex first_photo = 1;
  SlideIndex next_photo = 1;
  std::array<SlideRetry, SlideCache::capacity> retries{};

  while (true) {
    auto now = esp_timer_get_time();
    if (use_local_time && local_time_configured && now >= next_clock_poll) {
      const auto local = local_clock_time(std::time(nullptr));
      clock_dirty |= clock.update(local);
      local_hour = local ? std::optional<int>{local->tm_hour} : std::nullopt;
      brightness = daylight.is_day(local) ? day_level : night_level;
      next_clock_poll = now + 250'000;
    }
    if (const auto scanout = display.poll_presented()) {
      if (!backlight_ready) {
        if (!schedule.update(local_hour, now, false))
          input.sleep(swipe);
        applied_brightness = input.awake() ? brightness : 0;
        display.init_backlight(applied_brightness);
        backlight_ready = true;
        ESP_LOGI(tag, "Player ready (background %s preparation)", live && !trial_fixtures ? "Immich" : "fixture");
      }
      if (submitted_motion_us != 0) {
        const auto motion_us = *scanout - submitted_motion_us;
        if constexpr (trial_immich)
          record_live_trial_gesture(static_cast<uint32_t>(motion_us));
#if SWIPE_TRIAL_BENCHMARK
        record_live_trial_benchmark_gesture(static_cast<uint32_t>(motion_us), first_motion);
#endif
        if constexpr (trace_enabled)
          trace.gesture(*scanout, first_motion);
        else
          loader.report_gesture({motion_us, motion_render_us});
        submitted_motion_us = 0;
      }
#if SWIPE_TRIAL_BENCHMARK
      record_live_trial_benchmark_frame(*scanout, submitted_stamp, Display::nominal_frame_period_us());
#endif
      if constexpr (trace_enabled)
        trace.frame(*scanout, submitted_stamp);
    }

    SlideLoader::Completion completion{};
    if (loader.poll(completion)) {
      const bool ready = completion.result == SlideLoader::Result::Ready;
      const bool next_candidate = completion.result == SlideLoader::Result::NextCandidate;
      ESP_ERROR_CHECK(cache.complete(completion.lease, ready) ? ESP_OK : ESP_ERR_INVALID_STATE);
      const auto slide = completion.lease.slide;
      if constexpr (trace_enabled) {
        const auto outcome = ready ? RefillOutcome::Ready
                             : completion.cancelled ? RefillOutcome::Cancelled : RefillOutcome::Retry;
        trace.refill(esp_timer_get_time(), refill_started_us, slide, outcome);
      }
      retries[slide % retries.size()] = ready || completion.cancelled
                                           ? SlideRetry{}
                                           : SlideRetry::after(slide, now, completion.connection, next_candidate);
      if constexpr (trial_immich) {
        if (next_candidate && !completion.cancelled)
          record_live_trial_candidate_retry();
      }
      if (ready) {
        prefetch.prepared(slide);
        if constexpr (trial_immich) {
          if (slide > 0)
            record_live_trial_photo(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        }
      }
      first_photo = completion.first_photo;
      next_photo = completion.next_photo;
      memories_status = completion.memories_status;
      preparing.reset();
    }
    const auto center = swipe.settled();
    prefetch.moved_to(center);
    // The boot scene may still be visible while the first photo's final frame
    // is pending. Keep its pixels available if the user interrupts that swap.
    const SlideIndex first = live && display.presented_position() >= slide_width ? first_photo : 0;
    const SlideRange available{first, slide_count - 1};
    const auto window = prefetch.window(available);
    auto ready_first = center;
    auto ready_last = center;
    while (ready_first > window.first && cache.find(ready_first - 1) != nullptr)
      --ready_first;
    while (ready_last < window.last && cache.find(ready_last + 1) != nullptr)
      ++ready_last;
    swipe.set_ready_range(ready_first, ready_last);

    TouchReport report;
    const auto next_report = [&] {
#if SWIPE_TRIAL_BENCHMARK
      // Keep the touch reader running at its normal cadence during this test.
      while (touch.poll(report)) {
      }
      const bool at_rest = backlight_ready && input.awake() && !input.engaged() && swipe.idle() &&
                           !fade.running() && !fade.held() && display.presented_position() == center * slide_width;
      if (const auto sample = poll_live_trial_benchmark(esp_timer_get_time(), center, {ready_first, ready_last}, at_rest)) {
        report = *sample;
        return true;
      }
      return false;
#else
      return touch.poll(report);
#endif
    };
    while (next_report()) {
      if (!backlight_ready)
        continue;
      if (report.valid) {
        if constexpr (!esphome_trial)
          confirm_firmware_boot();
        if (report.contacts != 0 && fade.running()) {
          if (fade.interrupt(snapshot != nullptr))
            pin_fade();
          if constexpr (capture_motion)
            motion_epoch.update(MotionPhase::Still);
        }
      }
      const bool was_dragging = swipe.dragging();
      const auto effect = input.update(swipe, report, display.handoff_position());
      if constexpr (capture_motion) {
        if (was_dragging && !swipe.dragging())
          motion_epoch.update(MotionPhase::Still);
      }
      if (effect == SwipeInput::Effect::Sleep) {
        schedule.sleep();
        suspend_fade();
      } else if (effect == SwipeInput::Effect::Wake) {
        schedule.wake(report.sampled_us);
      }
      if (swipe.dragging()) {
        if (fade.held())
          fade.note_drag();
        prefetch.request_at_edge(swipe.position(), {ready_first, ready_last}, available);
      }
    }

    now = esp_timer_get_time();
    if (backlight_ready) {
      const bool awake = schedule.update(local_hour, now, input.engaged() || !swipe.idle());
      if (awake != input.awake()) {
        if (awake) {
          input.wake();
        } else {
          input.sleep(swipe);
          suspend_fade();
        }
      }
      const uint8_t target = awake ? brightness : 0;
      if (target != applied_brightness) {
        display.set_backlight(target);
        applied_brightness = target;
      }
    }

    if (blend.poll_complete() && fade.running()) {
      // Touch is handled before this block so a superseded blend can never
      // replace the frame the finger just took over.
      auto *output = display.writable_buffer();
      if (snapshot != nullptr)
        clock.restore_blend(output, cache.find(fade.target()), pending_opacity);
      clock.draw(output, true);
      clock_dirty = false;
      if constexpr (capture_motion)
        motion_epoch.update(MotionPhase::Crossfade);
      submit_frame(fade.anchor() * slide_width);
      recovery_painted = false;
      painted_memories_status = MemoriesStatus::None;
      fade.submitted(pending_opacity);
    }

    const auto keep = prefetch.window(available);
    const auto active_connection = network.photo_connection();
    const auto eligible = [&](SlideIndex candidate) {
      // Live photos have to be assigned in order. Trying a later index would
      // give it a retry delay even though no request for it has gone to Immich.
      return keep.contains(candidate) && (!live || trial_fixtures || candidate <= next_photo) &&
             retries[candidate % retries.size()].due(candidate, now, active_connection);
    };
    if (const auto wanted = prefetch.requested(); wanted && cache.find(*wanted))
      prefetch.prepared(*wanted);
    if (preparing) {
      if (const auto wanted = prefetch.requested(); wanted && eligible(*wanted) && prefetch.can_yield(*preparing, *wanted))
        loader.yield_to_swipe();
    } else if (!blend.pending()) {
      // Until PPA finishes, its input slides must stay readable even if a
      // touch has moved the cache window in the other direction.
      for (const auto candidate : prefetch.candidates()) {
        if (!eligible(candidate))
          continue;
        if (const auto lease = cache.reserve(candidate, keep.first, keep.last)) {
          if constexpr (trace_enabled)
            refill_started_us = esp_timer_get_time();
          loader.submit(*lease);
          preparing = candidate;
          break;
        }
      }
    }
    const bool was_settling = swipe.settling();
    swipe.tick(now);
    const auto position = swipe.position();
    const bool visible_and_idle = backlight_ready && input.awake() && !input.engaged() && swipe.idle() &&
                                  position == display.presented_position();
    if (fade.held() && !blend.pending() && visible_and_idle) {
      if (can_fade && swipe.settled() == fade.anchor()) {
        fade.resume(now);
      } else {
        if (swipe.settled() == fade.anchor())
          last_submitted = -1;  // Restore the clean anchor if the PPA failed.
        fade.finish();
        display.unpin();
        snapshot = nullptr;
      }
    }
    if (live) {
      const bool first_photo = swipe.settled() == 0;
      const bool dwelling = visible_and_idle && !fade.running() && !fade.held();
      if (dwell.elapsed(now, dwelling, first_photo ? 0 : loader.slide_interval_us())) {
        if (can_fade && !first_photo && swipe.settled() < ready_last)
          fade.start(swipe.settled(), swipe.settled() + 1, now);
        else
          swipe.advance(now, first_photo ? Swipe::Transition::Quick : Swipe::Transition::Glide);
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

    if (blend.pending()) {
      vTaskDelay(1);
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
            blit_slides(output, target, nullptr, panel_width, panel_height, 0);
            clock.draw(output);
            clock_dirty = false;
            swipe.show(fade.target());
            last_submitted = last_inside = swipe.position();
            submit_frame(last_submitted);
            recovery_painted = false;
            painted_memories_status = MemoriesStatus::None;
            fade.finish();
            display.unpin();
            snapshot = nullptr;
          } else if (blend.start(from, target, output, opacity)) {
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
      vTaskDelay(1);
      continue;
    }

    const auto inside = std::clamp(position, ready_first * slide_width, ready_last * slide_width);
    const bool use_edge = inside != position;
    const bool show_clock = live && swipe.settled() != 0;
    const bool show_notice = backlight_ready && input.awake() && !input.engaged() && swipe.idle() && !fade.held();
    const bool show_recovery = show_notice && network.recovery_status() != NativeNetwork::RecoveryStatus::Off;
    const auto memory_notice = show_notice && !show_recovery && ready_last == center ? memories_status : MemoriesStatus::None;
    if (auto *output = display.writable_buffer(); output != nullptr &&
        (position != last_submitted || inside != last_inside || show_recovery != recovery_painted ||
         memory_notice != painted_memories_status || (show_clock && clock_dirty))) {
      const auto index = inside / slide_width;
      const int offset = inside % slide_width;
      const auto slide_pixels = [&](SlideIndex slide) {
        return snapshot != nullptr && slide == fade.anchor() ? snapshot : cache.find(slide);
      };
      const auto *left = slide_pixels(index);
      const auto *right = offset == 0 ? nullptr : slide_pixels(index + 1);
      if (left != nullptr && (offset == 0 || right != nullptr)) {
        const auto motion = input.take_first_motion();
        const auto started_render = motion != 0 ? esp_timer_get_time() : 0;
        if (use_edge) {
          constexpr auto limit = PixelPosition{slide_width - 1};
          const int logical_pull = static_cast<int>(std::clamp(position - inside, -limit, limit));
          const int pull = logical_pull * axis_direction(rotation);
          blit_slide_edge(output, left, panel_width, panel_height, pull);
          if (left == snapshot)
            clock.restore_rows(output, std::max(0, pull), std::max(0, -pull), panel_height - std::abs(pull));
        } else {
          const auto span = orient_slide_span(left, right, panel_height, offset, rotation);
          blit_slides(output, span.first, span.second, panel_width, panel_height, span.offset);
          if (span.first == snapshot)
            clock.restore_rows(output, span.offset, 0, panel_height - span.offset);
          if (span.offset != 0 && span.second == snapshot)
            clock.restore_rows(output, 0, panel_height - span.offset, span.offset);
        }
        if (show_clock) {
          clock.draw(output);
          clock_dirty = false;
        }
        if (show_recovery)
          draw_recovery_card(output, recovery_atlas, network.recovery_network(), rotation);
        else if (memory_notice != MemoriesStatus::None)
          draw_memories_status(output, recovery_atlas, memory_notice, rotation);
        recovery_painted = show_recovery;
        painted_memories_status = memory_notice;
        submit_frame(position, position != last_submitted);
        submitted_motion_us = motion;
        motion_render_us = motion != 0 ? esp_timer_get_time() - started_render : 0;
        if constexpr (capture_motion) {
          if (motion != 0)
            first_motion = {motion, input.contact_started_us(), motion_render_us,
                            ready_at_motion(position, {ready_first, ready_last}, available), preparing.has_value()};
        }
        last_submitted = position;
        last_inside = inside;
      }
    }
    vTaskDelay(1);
  }
}

}  // namespace

void start_player() {
  if constexpr (esphome_trial) {
    constexpr unsigned trial_seconds = trial_fixtures ? 120 : trial_immich ? 300 : 180;
    if (!start_esphome_trial(trial_seconds))
      return;
  } else {
    start_usb_setup(network);
  }
  const auto result = xTaskCreatePinnedToCore(run, "photo-player", 4096, nullptr, player_priority, nullptr, player_core);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

}  // namespace espframe::native

extern "C" void app_main() { espframe::native::start_player(); }
