#pragma once

#include "display.h"
#include "photo_controls.h"
#include "photo_zoom_packets.h"
#include "slide_cache.h"

#include "driver/jpeg_decode.h"
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <memory>
#include <optional>

namespace espframe::native {

// The display task owns job and clip lifetimes. The worker gets exclusive
// access to the packet allocation between submission and publication.
class PhotoZoomCache {
 public:
  void start(int core) {
    if (xTaskCreatePinnedToCore(run_, "zoom-prepare", 4096, this, 1, &task_, core) != pdPASS)
      task_ = nullptr;
  }

  void service(Display &display, SlideCache &cache, bool urgent) {
    if (phase_.load(std::memory_order_acquire) == Phase::Working) {
      if (urgent)
        cancel_.store(true, std::memory_order_relaxed);
      return;
    }
    if (lease_) {
      display.release_preparation_buffer(raw_);
      cache.release(*lease_);
      raw_ = nullptr;
      lease_.reset();
    }
  }

  void update(Display &display, SlideCache &cache, SlideIndex slide, ScreenRotation rotation, bool idle) {
    const auto phase = phase_.load(std::memory_order_acquire);
    if (source_ && (phase == Phase::Cancelled || rotation_ != rotation || !cache.is_current(slide, *source_))) {
      cancel_.store(true, std::memory_order_relaxed);
      if (phase != Phase::Working) {
        source_.reset();
        packets_.reset();
        bytes_.reset();
        phase_.store(Phase::Empty, std::memory_order_relaxed);
      }
    }
    if (!idle || !task_ || source_ || lease_ || phase == Phase::Working || !has_memory())
      return;
    auto *raw = display.reserve_preparation_buffer();
    if (!raw)
      return;
    source_ = cache.pin_ready(slide);
    if (!source_) {
      display.release_preparation_buffer(raw);
      return;
    }
    raw_ = raw;
    lease_ = source_;
    rotation_ = rotation;
    cancel_.store(false, std::memory_order_relaxed);
    phase_.store(Phase::Working, std::memory_order_release);
    xTaskNotifyGive(task_);
  }

  bool release_before_transition() {
    if (!lease_)
      return true;
    cancel_.store(true, std::memory_order_relaxed);
    return false;
  }

  bool available_for(const SlideCache &cache, SlideIndex slide, ScreenRotation rotation) const {
    return available() && source_ && rotation_ == rotation && cache.is_current(slide, *source_);
  }

  bool decode(int step, uint16_t *output) {
    if (!available() || step <= 0 || step > static_cast<int>(PhotoZoomPackets::frame_count))
      return false;
    if (!decoder_) {
      jpeg_decode_engine_cfg_t config{};
      config.timeout_ms = 85;
      if (const auto err = jpeg_new_decoder_engine(&config, &decoder_); err != ESP_OK)
        return decode_failed_(err);
    }
    const auto packet = packets_->frame(static_cast<size_t>(step - 1));
    jpeg_decode_cfg_t config{};
    config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t written{};
    const auto err = jpeg_decoder_process(decoder_, &config, packet.data(), packet.size(),
                                         reinterpret_cast<uint8_t *>(output), panel_bytes, &written);
    if (err != ESP_OK || written != panel_bytes)
      return decode_failed_(err == ESP_OK ? ESP_ERR_INVALID_SIZE : err);
    return true;
  }

 private:
  bool available() const { return phase_.load(std::memory_order_acquire) == Phase::Ready; }

  static constexpr const char *tag = "swipe-probe";
  static constexpr size_t panel_bytes = pixel_count * sizeof(uint16_t);
  static constexpr size_t free_after_work = 5 * 1024 * 1024;
  static constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  enum class Phase { Empty, Working, Ready, Failed, Cancelled };
  using Buffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;

  bool decode_failed_(esp_err_t error) {
    ESP_LOGW(tag, "Zoom frame decode failed: %s", esp_err_to_name(error));
    if (decoder_) {
      jpeg_del_decoder_engine(decoder_);
      decoder_ = nullptr;
    }
    packets_.reset();
    bytes_.reset();
    phase_.store(Phase::Failed, std::memory_order_relaxed);
    return false;
  }

  static void run_(void *context) {
    auto &clip = *static_cast<PhotoZoomCache *>(context);
    while (true) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if (clip.phase_.load(std::memory_order_acquire) != Phase::Working)
        continue;
      const bool built = clip.build_();
      const bool was_cancelled = clip.cancel_.load(std::memory_order_relaxed);
      const bool ready = built && !was_cancelled;
      if (!ready) {
        clip.packets_.reset();
        clip.bytes_.reset();
      }
      clip.phase_.store(was_cancelled ? Phase::Cancelled : ready ? Phase::Ready : Phase::Failed, std::memory_order_release);
    }
  }

  static bool has_memory() {
    return heap_caps_get_free_size(caps) >= free_after_work + PhotoZoomPackets::byte_limit &&
           heap_caps_get_largest_free_block(caps) >= PhotoZoomPackets::byte_limit;
  }

  bool build_() {
    if (!has_memory())
      return false;
    bytes_.reset(static_cast<uint8_t *>(heap_caps_aligned_alloc(PhotoZoomPackets::alignment,
                                                               PhotoZoomPackets::byte_limit, caps)));
    if (!bytes_)
      return false;
    packets_.emplace(std::span{bytes_.get(), PhotoZoomPackets::byte_limit});
    jpeg_encode_engine_cfg_t engine_config{};
    engine_config.timeout_ms = 100;
    jpeg_encoder_handle_t encoder{};
    if (const auto err = jpeg_new_encoder_engine(&engine_config, &encoder); err != ESP_OK) {
      ESP_LOGW(tag, "Zoom frame encoder unavailable: %s", esp_err_to_name(err));
      return false;
    }
    const bool ready = encode_frames_(encoder, reinterpret_cast<uint8_t *>(raw_));
    jpeg_del_encoder_engine(encoder);
    return ready;
  }

  bool encode_frames_(jpeg_encoder_handle_t encoder, uint8_t *raw) {
    jpeg_encode_cfg_t config{};
    config.width = panel_width;
    config.height = panel_height;
    config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    config.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
    config.image_quality = 60;
    auto *pixels = reinterpret_cast<uint16_t *>(raw);
    static_assert(PhotoZoomPackets::frame_count == PhotoControls::cached_steps);
    for (int step = 1; step <= PhotoControls::cached_steps; ++step) {
      painter_.prepare(pixels, {source_->pixels, nullptr, 0}, 0, rotation_, PhotoControls::cached_progress(step));
      for (int row = 0; row < panel_height; row += 64) {
        if (cancel_.load(std::memory_order_relaxed))
          return false;
        painter_.draw_rows(row, std::min(row + 64, panel_height));
      }
      painter_.draw_toolbar();
      if (cancel_.load(std::memory_order_relaxed))
        return false;
      auto destination = packets_->destination();
      if (destination.empty())
        return false;
      uint32_t size{};
      const auto err = jpeg_encoder_process(encoder, &config, raw, panel_bytes, destination.data(), destination.size(), &size);
      if (err != ESP_OK || !packets_->finish_frame(size)) {
        if (err != ESP_OK)
          ESP_LOGW(tag, "Zoom frame encode failed at frame %d: %s", step, esp_err_to_name(err));
        return false;
      }
      // Give other work a scheduling opportunity between JPEG transactions.
      vTaskDelay(1);
    }
    return !cancel_.load(std::memory_order_relaxed);
  }

  PhotoControls painter_;
  std::optional<SlideCache::ReadLease> source_;
  std::optional<SlideCache::ReadLease> lease_;
  std::optional<PhotoZoomPackets> packets_;
  Buffer bytes_{nullptr, heap_caps_free};
  uint16_t *raw_{};
  TaskHandle_t task_{};
  ScreenRotation rotation_{};
  jpeg_decoder_handle_t decoder_{};
  std::atomic<Phase> phase_{Phase::Empty};
  std::atomic<bool> cancel_{};
};

}  // namespace espframe::native
