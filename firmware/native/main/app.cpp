#include "display.h"
#include "immich.h"
#include "slide_blit.h"
#include "slide_cache.h"
#include "swipe.h"
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
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <limits>

namespace espframe::native {
namespace {

constexpr const char *tag = "swipe-probe";
constexpr int fixture_count = 5;
constexpr int fixture_slides = 30;
constexpr int64_t touch_period_us = 5'000;

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
  };

  bool live() const { return this->live_; }
  int64_t slide_interval_us() const { return this->source_.slide_seconds() * 1'000'000LL; }

  void start() {
    this->live_ = this->source_.read_settings();
    this->requests_ = xQueueCreate(1, sizeof(SlideCache::Lease));
    this->completed_ = xQueueCreate(1, sizeof(Completion));
    ESP_ERROR_CHECK(this->requests_ && this->completed_ ? ESP_OK : ESP_ERR_NO_MEM);
    const auto result = xTaskCreatePinnedToCore(run_, "slide-prepare", 6144, this, 2, nullptr, 1);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  }

  void submit(const SlideCache::Lease &lease) {
    ESP_ERROR_CHECK(xQueueSend(this->requests_, &lease, 0) == pdPASS ? ESP_OK : ESP_ERR_INVALID_STATE);
  }

  bool poll(Completion &completion) {
    return xQueueReceive(this->completed_, &completion, 0) == pdPASS;
  }

 private:
  static void run_(void *context) {
    auto &loader = *static_cast<SlideLoader *>(context);
    jpeg_decoder_handle_t decoder{};
    while (true) {
      SlideCache::Lease lease{};
      if (xQueueReceive(loader.requests_, &lease, portMAX_DELAY) != pdPASS)
        continue;
      bool ready = true;
      if (loader.live_ && lease.slide > 0) {
        ready = loader.source_.prepare(lease.slide, lease.pixels);
      } else {
        const int fixture = lease.slide % fixture_count;
        if (!decode_photo(photos[fixture], lease.pixels, decoder))
          draw_test_slide(lease.pixels, fixture);
      }
      const Completion completion{lease, loader.source_.first_slide(), ready};
      xQueueSend(loader.completed_, &completion, portMAX_DELAY);
    }
  }

  ImmichSource source_;
  QueueHandle_t requests_{};
  QueueHandle_t completed_{};
  bool live_{};
};

void run() {
  SlideCache cache{allocate_slide_buffers()};
  static SlideLoader loader;
  loader.start();
  start_usb_setup();
  const bool live = loader.live();
  const SlideIndex slide_count = live ? std::numeric_limits<PixelPosition>::max() / slide_width : fixture_slides;
  static Display display;
  display.init();
  Touch touch;
  touch.init();
  Swipe swipe{slide_width, slide_count, live ? 0 : 2};

  bool touch_down = false;
  bool wait_for_release = false;
  unsigned touch_errors = 0;
  int64_t next_sample_us = 0;
  int64_t first_motion_us = 0;
  int64_t submitted_motion_us = 0;
  PixelPosition last_submitted = -1;
  bool lit = false;
  bool preparing = false;
  int64_t next_advance_us = 0;
  SlideIndex first_photo = 1;
  SlideIndex previous_center = swipe.settled();
  int direction = 1;
  struct Retry {
    SlideIndex slide{};
    int64_t until{};
  };
  std::array<Retry, SlideCache::capacity> retries{};

  while (true) {
    auto now = esp_timer_get_time();
    if (display.poll_presented()) {
      if (!lit) {
        display.enable_backlight();
        lit = true;
        ESP_LOGI(tag, "Player ready (background %s preparation)", live ? "Immich" : "fixture");
      }
      if (submitted_motion_us != 0) {
        ESP_LOGI(tag, "gesture_to_scanout_us=%" PRId64, now - submitted_motion_us);
        submitted_motion_us = 0;
      }
    }

    SlideLoader::Completion completion{};
    if (loader.poll(completion)) {
      ESP_ERROR_CHECK(cache.complete(completion.lease, completion.ready) ? ESP_OK : ESP_ERR_INVALID_STATE);
      const auto slide = completion.lease.slide;
      retries[slide % retries.size()] = {slide, completion.ready ? 0 : now + 2'000'000};
      first_photo = completion.first_photo;
      preparing = false;
    }
    const auto center = swipe.settled();
    if (center != previous_center) {
      direction = center > previous_center ? 1 : -1;
      previous_center = center;
    }
    // The boot scene may still be visible while the first photo's final frame
    // is pending. Keep its pixels available if the user interrupts that swap.
    const SlideIndex first = live && display.presented_position() >= slide_width ? first_photo : 0;
    const auto keep_first = std::max(first, center - (direction < 0 ? 3 : 2));
    const auto keep_last = std::min(slide_count - 1, center + (direction > 0 ? 3 : 2));
    auto ready_first = center;
    auto ready_last = center;
    while (ready_first > keep_first && cache.find(ready_first - 1) != nullptr)
      --ready_first;
    while (ready_last < keep_last && cache.find(ready_last + 1) != nullptr)
      ++ready_last;
    swipe.set_ready_range(ready_first, ready_last);

    if (!preparing) {
      for (const int offset : {0, 1, -1, 2, -2, 3}) {
        const auto candidate = center + direction * offset;
        if (candidate < keep_first || candidate > keep_last)
          continue;
        const auto &retry = retries[candidate % retries.size()];
        if (retry.slide == candidate && now < retry.until)
          continue;
        if (const auto lease = cache.reserve(candidate, keep_first, keep_last)) {
          loader.submit(*lease);
          preparing = true;
          break;
        }
      }
    }

    if (lit && now >= next_sample_us) {
      TouchSample sample;
      const esp_err_t status = touch.read(sample);
      now = esp_timer_get_time();
      next_sample_us = now + touch_period_us;
      if (status != ESP_OK) {
        if (++touch_errors >= 3 && touch_down) {
          swipe.cancel(now);
          touch_down = false;
          wait_for_release = true;
        }
      } else {
        touch_errors = 0;
        if (sample.contacts > 1) {
          swipe.cancel(now);
          touch_down = false;
          wait_for_release = true;
        } else if (sample.contacts == 0) {
          if (touch_down)
            swipe.release(now);
          touch_down = false;
          wait_for_release = false;
        } else if (!wait_for_release) {
          if (!touch_down) {
            swipe.press(sample.x, sample.y, display.presented_position(), now);
            first_motion_us = 0;
            touch_down = true;
          }
          next_advance_us = now + loader.slide_interval_us();
          const bool was_dragging = swipe.dragging();
          swipe.move(sample.x, sample.y, now);
          if (!was_dragging && swipe.dragging() && swipe.position() != display.presented_position())
            first_motion_us = now;
        }
      }
    }

    if (live && lit && !touch_down && !wait_for_release && now >= next_advance_us && swipe.advance(now))
      next_advance_us = now + loader.slide_interval_us();
    swipe.tick(now);
    const auto position = swipe.position();
    if (auto *output = display.writable_buffer(); output != nullptr && position != last_submitted) {
      const auto index = position / slide_width;
      const int offset = position % slide_width;
      const auto *left = cache.find(index);
      const auto *right = offset == 0 ? nullptr : cache.find(index + 1);
      if (left != nullptr && (offset == 0 || right != nullptr)) {
        blit_slides(output, left, right, panel_width, panel_height, offset);
        display.submit(position);
        submitted_motion_us = first_motion_us;
        first_motion_us = 0;
        last_submitted = position;
      }
    }
    vTaskDelay(1);
  }
}

}  // namespace
}  // namespace espframe::native

extern "C" void app_main() { espframe::native::run(); }
