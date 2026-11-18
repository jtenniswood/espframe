#include "autoplay_fade.h"
#include "display.h"
#include "firmware_update.h"
#include "frame_blend.h"
#include "immich.h"
#include "slide_blit.h"
#include "slide_cache.h"
#include "slide_dwell.h"
#include "slide_prefetch.h"
#include "swipe.h"
#include "swipe_input.h"
#include "touch.h"
#include "usb_setup.h"

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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
void draw_test_slide(uint16_t *destination, int slide) {
  constexpr int sky[][3]{{224, 143, 99}, {64, 162, 190}, {101, 146, 220},
                         {199, 112, 143}, {77, 165, 146}};
  for (int x = 0; x < slide_width; ++x) {
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
      destination[x * panel_width + panel_width - 1 - y] = color;
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
  struct Completion {
    SlideCache::Lease lease;
    SlideIndex first_photo;
    bool ready;
    bool cancelled;
  };

  bool live() const { return this->live_; }
  int64_t slide_interval_us() const { return this->source_.slide_seconds() * 1'000'000LL; }

  void start() {
    this->live_ = this->source_.read_settings();
    this->requests_ = xQueueCreate(1, sizeof(SlideCache::Lease));
    this->completed_ = xQueueCreate(1, sizeof(Completion));
    this->timings_ = xQueueCreate(32, sizeof(GestureTiming));
    ESP_ERROR_CHECK(this->requests_ && this->completed_ && this->timings_ ? ESP_OK : ESP_ERR_NO_MEM);
    const auto result = xTaskCreatePinnedToCore(run_, "slide-prepare", 6144, this, 2, &this->task_, loader_core);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
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
      GestureTiming timing;
      while (xQueueReceive(loader.timings_, &timing, 0) == pdPASS)
        ESP_LOGI(tag, "gesture_to_scanout_us=%" PRId64 " compose_and_submit_us=%" PRId64,
                 timing.gesture_to_scanout_us, timing.compose_and_submit_us);
      SlideCache::Lease lease{};
      if (xQueueReceive(loader.requests_, &lease, 0) != pdPASS)
        continue;
      bool ready = true;
      if (loader.live_ && lease.slide > 0) {
        ready = loader.source_.prepare(lease.slide, lease.pixels);
      } else {
        const int fixture = lease.slide % fixture_count;
        if (!decode_photo(photos[fixture], lease.pixels, decoder))
          draw_test_slide(lease.pixels, fixture);
      }
      const Completion completion{lease, loader.source_.first_slide(), ready, !ready && loader.cancelled_.load()};
      xQueueSend(loader.completed_, &completion, portMAX_DELAY);
    }
  }

  std::atomic<bool> cancelled_{};
  ImmichSource source_{cancelled_};
  QueueHandle_t requests_{};
  QueueHandle_t completed_{};
  QueueHandle_t timings_{};
  TaskHandle_t task_{};
  bool live_{};
};

void run(void *) {
  start_firmware_boot_check();
  SlideCache cache{allocate_slide_buffers()};
  static SlideLoader loader;
  loader.start();
  const bool live = loader.live();
  const SlideIndex slide_count = live ? std::numeric_limits<PixelPosition>::max() / slide_width : fixture_slides;
  FrameBlend blend;
  bool can_fade = live && blend.init();
  static Display display;
  display.init(can_fade);
  if (can_fade && !display.can_pin_frame()) {
    blend.release();
    can_fade = false;
  }
  if (live && !can_fade)
    ESP_LOGI(tag, "Crossfade unavailable; using the slideshow glide");
  TouchReader touch;
  touch.start();
  Swipe swipe{slide_width, slide_count, live ? 0 : 2};
  SwipeInput input;
  SlideDwell dwell;
  SlidePrefetch prefetch{swipe.settled()};
  AutoplayFade fade;
  const uint16_t *snapshot = nullptr;
  uint8_t pending_opacity = 0;

  int64_t submitted_motion_us = 0;
  int64_t motion_render_us = 0;
  PixelPosition last_submitted = -1;
  PixelPosition last_inside = -1;
  bool lit = false;
  std::optional<SlideIndex> preparing;
  SlideIndex first_photo = 1;
  struct Retry {
    SlideIndex slide{};
    int64_t until{};
  };
  std::array<Retry, SlideCache::capacity> retries{};

  while (true) {
    auto now = esp_timer_get_time();
    if (const auto scanout = display.poll_presented()) {
      if (!lit) {
        display.enable_backlight();
        lit = true;
        ESP_LOGI(tag, "Player ready (background %s preparation)", live ? "Immich" : "fixture");
      }
      if (submitted_motion_us != 0) {
        loader.report_gesture({*scanout - submitted_motion_us, motion_render_us});
        submitted_motion_us = 0;
      }
    }

    SlideLoader::Completion completion{};
    if (loader.poll(completion)) {
      ESP_ERROR_CHECK(cache.complete(completion.lease, completion.ready) ? ESP_OK : ESP_ERR_INVALID_STATE);
      const auto slide = completion.lease.slide;
      retries[slide % retries.size()] = {slide, completion.ready || completion.cancelled ? 0 : now + 2'000'000};
      if (completion.ready)
        prefetch.prepared(slide);
      first_photo = completion.first_photo;
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
    while (touch.poll(report)) {
      if (!lit)
        continue;
      if (report.valid) {
        confirm_firmware_boot();
        if (report.contacts != 0 && fade.running() && fade.interrupt(snapshot != nullptr))
          snapshot = display.pin_handoff();
      }
      input.update(swipe, report, display.handoff_position());
      if (swipe.dragging()) {
        if (fade.held())
          fade.note_drag();
        prefetch.request_at_edge(swipe.position(), {ready_first, ready_last}, available);
      }
    }

    if (blend.poll_complete() && fade.running()) {
      // Touch is handled before this block so a superseded blend can never
      // replace the frame the finger just took over.
      display.submit(fade.anchor() * slide_width);
      fade.submitted(pending_opacity);
    }

    now = esp_timer_get_time();
    const auto keep = prefetch.window(available);
    const auto eligible = [&](SlideIndex candidate) {
      if (!keep.contains(candidate))
        return false;
      const auto &retry = retries[candidate % retries.size()];
      return retry.slide != candidate || now >= retry.until;
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
          loader.submit(*lease);
          preparing = candidate;
          break;
        }
      }
    }
    swipe.tick(now);
    const auto position = swipe.position();
    const bool visible_and_idle = lit && !input.engaged() && swipe.idle() && position == display.presented_position();
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
            swipe.show(fade.target());
            last_submitted = last_inside = swipe.position();
            display.submit(last_submitted);
            fade.finish();
            display.unpin();
            snapshot = nullptr;
          } else if (blend.start(from, target, output, opacity)) {
            pending_opacity = opacity;
          } else {
            ESP_LOGW(tag, "Could not start the frame blend; using the slideshow glide");
            const bool advance = fade.target() != fade.anchor();
            if (fade.interrupt(snapshot != nullptr))
              snapshot = display.pin_handoff();
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
    if (auto *output = display.writable_buffer(); output != nullptr &&
        (position != last_submitted || inside != last_inside)) {
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
          const int pull = static_cast<int>(std::clamp(position - inside, -limit, limit));
          blit_slide_edge(output, left, panel_width, panel_height, pull);
        } else {
          blit_slides(output, left, right, panel_width, panel_height, offset);
        }
        display.submit(position);
        submitted_motion_us = motion;
        motion_render_us = motion != 0 ? esp_timer_get_time() - started_render : 0;
        last_submitted = position;
        last_inside = inside;
      }
    }
    vTaskDelay(1);
  }
}

}  // namespace

void start_player() {
  start_usb_setup();
  const auto result = xTaskCreatePinnedToCore(run, "photo-player", 4096, nullptr, player_priority, nullptr, player_core);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

}  // namespace espframe::native

extern "C" void app_main() { espframe::native::start_player(); }
