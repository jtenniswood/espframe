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
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <memory>
#include <optional>

namespace espframe::native {

// The display task owns job and clip lifetimes. The worker gets exclusive
// access to the packet allocation between submission and publication.
class PhotoZoomCache {
 public:
  void start(int core, int helper_core) {
    if (xTaskCreatePinnedToCore(run_, "zoom-prepare", 4096, this, worker_priority, &task_, core) != pdPASS) {
      task_ = nullptr;
      return;
    }
    rows_done_ = xSemaphoreCreateBinaryStatic(&rows_done_storage_);
    if (xTaskCreatePinnedToCore(run_rows_, "zoom-rows", 2048, this, worker_priority, &helper_, helper_core) != pdPASS)
      helper_ = nullptr;
  }

  void service(Display &display, SlideCache &cache, bool urgent) {
    const auto phase = phase_.load(std::memory_order_acquire);
    if (phase == Phase::Working) {
      if (urgent)
        pause_requested_.store(true, std::memory_order_relaxed);
      return;
    }
    if (lease_) {
      display.release_preparation_buffer(raw_);
      cache.release(*lease_);
      raw_ = nullptr;
      lease_.reset();
    }
    if (source_ && phase == Phase::Ready) {
      if (!discard_job_ && cache.is_current(job_slide_, *source_)) {
        for (auto &entry : ready_) {
          if (!entry) {
            entry.emplace(ReadyClip{*source_, job_slide_, rotation_, std::move(packets_)});
            break;
          }
        }
      }
      discard_finished_job_();
    } else if (source_ && discard_job_) {
      discard_finished_job_();
    }
  }

  void reclaim_for_photo_load(SlideIndex visible) {
    if (heap_caps_get_free_size(caps) < free_after_work) {
      for (auto &entry : ready_) {
        if (entry && entry->slide != visible)
          entry.reset();
      }
    }
    if (source_ && job_slide_ != visible) {
      discard_job_ = true;
      pause_requested_.store(true, std::memory_order_relaxed);
    }
  }

  void update(Display &display, SlideCache &cache, SlideIndex visible, ScreenRotation rotation, bool idle,
              bool can_look_ahead) {
    for (auto &entry : ready_) {
      if (entry && (entry->rotation != rotation || std::abs(entry->slide - visible) > 1 ||
                    !cache.is_current(entry->slide, entry->source)))
        entry.reset();
    }
    const bool has_current = available_for(cache, visible, rotation);
    const auto target = has_current ? visible + 1 : visible;
    const bool looking_ahead = target != visible;
    const bool can_start = idle && (!looking_ahead || can_look_ahead);
    const auto phase = phase_.load(std::memory_order_acquire);
    if (source_ && (job_slide_ != target || rotation_ != rotation || !cache.is_current(job_slide_, *source_))) {
      discard_job_ = true;
      pause_requested_.store(true, std::memory_order_relaxed);
      return;
    }
    if (!can_start || !task_ || lease_ || discard_job_ || (source_ && phase != Phase::Paused) ||
        available_for(cache, target, rotation))
      return;
    if (looking_ahead) {
      for (auto &entry : ready_) {
        if (entry && entry->slide != visible)
          entry.reset();
      }
    }
    if (ready_[0] && ready_[1])
      ready_[0].reset();
    reserve_floor_ = looking_ahead ? idle_memory_floor : free_after_work;
    if (!has_memory())
      return;
    auto *raw = display.reserve_preparation_buffer();
    if (!raw)
      return;
    const auto lease = cache.pin_ready(target);
    if (!lease) {
      display.release_preparation_buffer(raw);
      return;
    }
    raw_ = raw;
    lease_ = lease;
    source_ = lease;
    job_slide_ = target;
    rotation_ = rotation;
    pause_requested_.store(false, std::memory_order_relaxed);
    phase_.store(Phase::Working, std::memory_order_release);
    xTaskNotifyGive(task_);
  }

  bool release_before_transition() {
    if (!lease_)
      return true;
    pause_requested_.store(true, std::memory_order_relaxed);
    return false;
  }

  bool available_for(const SlideCache &cache, SlideIndex slide, ScreenRotation rotation) const {
    return find_(cache, slide, rotation) != ready_.size();
  }

  bool decode_for(const SlideCache &cache, SlideIndex slide, ScreenRotation rotation, int step, uint16_t *output) {
    const auto index = find_(cache, slide, rotation);
    if (index == ready_.size() || step <= 0 || step > static_cast<int>(PhotoZoomPackets::frame_count))
      return false;
    if (!decoder_) {
      jpeg_decode_engine_cfg_t config{};
      config.timeout_ms = 85;
      if (const auto err = jpeg_new_decoder_engine(&config, &decoder_); err != ESP_OK)
        return decode_failed_(index, err);
    }
    const auto packet = ready_[index]->packets.frame(static_cast<size_t>(step - 1));
    jpeg_decode_cfg_t config{};
    config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t written{};
    const auto err = jpeg_decoder_process(decoder_, &config, packet.data(), packet.size(),
                                         reinterpret_cast<uint8_t *>(output), panel_bytes, &written);
    if (err != ESP_OK || written != panel_bytes)
      return decode_failed_(index, err == ESP_OK ? ESP_ERR_INVALID_SIZE : err);
    return true;
  }

 private:
  struct ReadyClip {
    SlideCache::ReadLease source;
    SlideIndex slide;
    ScreenRotation rotation;
    PhotoZoomPackets packets;
  };

  size_t find_(const SlideCache &cache, SlideIndex slide, ScreenRotation rotation) const {
    for (size_t i = 0; i < ready_.size(); ++i) {
      const auto &entry = ready_[i];
      if (entry && entry->slide == slide && entry->rotation == rotation && cache.is_current(slide, entry->source))
        return i;
    }
    return ready_.size();
  }

  void discard_finished_job_() {
    source_.reset();
    packets_ = {};
    wait_for_bytes_ = minimum_packet_space;
    discard_job_ = false;
    phase_.store(Phase::Empty, std::memory_order_relaxed);
  }

  static constexpr const char *tag = "swipe-probe";
  static constexpr UBaseType_t worker_priority = 2;
  static constexpr size_t panel_bytes = pixel_count * sizeof(uint16_t);
  static constexpr size_t free_after_work = 5 * 1024 * 1024;
  // The adjacent clip can borrow more memory only while photo loading is idle.
  static constexpr size_t idle_memory_floor = 4 * 1024 * 1024;
  static constexpr size_t minimum_packet_space = 64 * 1024;
  static constexpr int rows_per_chunk = 64;
  static constexpr int chunk_count = (panel_height + rows_per_chunk - 1) / rows_per_chunk;
  static constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  enum class Phase { Empty, Working, Paused, Ready, Failed };
  using Buffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;

  bool decode_failed_(size_t index, esp_err_t error) {
    ESP_LOGW(tag, "Zoom frame decode failed: %s", esp_err_to_name(error));
    if (decoder_) {
      jpeg_del_decoder_engine(decoder_);
      decoder_ = nullptr;
    }
    ready_[index].reset();
    return false;
  }

  static void run_(void *context) {
    auto &clip = *static_cast<PhotoZoomCache *>(context);
    while (true) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if (clip.phase_.load(std::memory_order_acquire) != Phase::Working)
        continue;
      const auto next = clip.build_();
      if (next == Phase::Failed)
        clip.packets_ = {};
      clip.phase_.store(next, std::memory_order_release);
    }
  }

  bool has_memory() const {
    return heap_caps_get_free_size(caps) >= reserve_floor_ + PhotoZoomPackets::packet_limit + wait_for_bytes_ &&
           heap_caps_get_largest_free_block(caps) >= PhotoZoomPackets::packet_limit;
  }

  Phase build_() {
    if (pause_requested_.load(std::memory_order_relaxed) || !has_memory())
      return Phase::Paused;
    Buffer staging{static_cast<uint8_t *>(heap_caps_aligned_alloc(PhotoZoomPackets::alignment,
                                                                  PhotoZoomPackets::packet_limit, caps)), heap_caps_free};
    if (!staging)
      return Phase::Failed;
    jpeg_encode_engine_cfg_t engine_config{};
    engine_config.timeout_ms = 100;
    jpeg_encoder_handle_t encoder{};
    if (const auto err = jpeg_new_encoder_engine(&engine_config, &encoder); err != ESP_OK) {
      ESP_LOGW(tag, "Zoom frame encoder unavailable: %s", esp_err_to_name(err));
      return Phase::Failed;
    }
    const auto result = encode_frames_(encoder, reinterpret_cast<uint8_t *>(raw_), staging.get());
    jpeg_del_encoder_engine(encoder);
    return result;
  }

  static void run_rows_(void *context) {
    auto &clip = *static_cast<PhotoZoomCache *>(context);
    while (true) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      clip.draw_rows_();
    }
  }

  void draw_rows_() {
    while (true) {
      const int first = next_row_.fetch_add(rows_per_chunk, std::memory_order_acquire);
      if (first >= panel_height)
        return;
      if (!pause_requested_.load(std::memory_order_relaxed))
        painter_.draw_rows(first, std::min(first + rows_per_chunk, panel_height));
      if (rows_left_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        xSemaphoreGive(rows_done_);
    }
  }

  Phase encode_frames_(jpeg_encoder_handle_t encoder, uint8_t *raw, uint8_t *staging) {
    jpeg_encode_cfg_t config{};
    config.width = panel_width;
    config.height = panel_height;
    config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    config.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
    config.image_quality = 60;
    auto *pixels = reinterpret_cast<uint16_t *>(raw);
    static_assert(PhotoZoomPackets::frame_count == PhotoControls::cached_steps);
    const int first_step = static_cast<int>(packets_.prepared_frames()) + 1;
    for (int step = first_step; step <= PhotoControls::cached_steps; ++step) {
      painter_.prepare(pixels, {source_->pixels, nullptr, 0}, 0, rotation_, PhotoControls::cached_progress(step));
      // Both workers claim disjoint rows. The last row releases the canvas
      // even if a gesture asked the workers to skip the rest of the draw.
      rows_left_.store(chunk_count, std::memory_order_relaxed);
      next_row_.store(0, std::memory_order_release);
      if (helper_)
        xTaskNotifyGive(helper_);
      draw_rows_();
      while (rows_left_.load(std::memory_order_acquire) != 0)
        xSemaphoreTake(rows_done_, portMAX_DELAY);
      painter_.draw_toolbar();
      if (pause_requested_.load(std::memory_order_relaxed))
        return Phase::Paused;
      uint32_t size{};
      const auto err = jpeg_encoder_process(encoder, &config, raw, panel_bytes, staging,
                                            PhotoZoomPackets::packet_limit, &size);
      if (err != ESP_OK || size > PhotoZoomPackets::packet_limit) {
        ESP_LOGW(tag, "Zoom frame encode failed at frame %d: %s", step, esp_err_to_name(err));
        return Phase::Failed;
      }
      const auto bytes = PhotoZoomPackets::allocation_size(size);
      if (bytes > PhotoZoomPackets::byte_limit - packets_.used_bytes())
        return Phase::Failed;
      const auto free = heap_caps_get_free_size(caps);
      const auto budget = free > reserve_floor_ ? free - reserve_floor_ : 0;
      if (bytes > budget) {
        wait_for_bytes_ = bytes;
        return Phase::Paused;
      }
      if (!packets_.append({staging, size}, budget))
        return Phase::Failed;
      wait_for_bytes_ = minimum_packet_space;
      // Give other work a scheduling opportunity between JPEG transactions.
      vTaskDelay(1);
    }
    return Phase::Ready;
  }

  PhotoControls painter_;
  std::optional<SlideCache::ReadLease> source_;
  std::optional<SlideCache::ReadLease> lease_;
  PhotoZoomPackets packets_;
  std::array<std::optional<ReadyClip>, 2> ready_;
  SlideIndex job_slide_{};
  bool discard_job_{};
  size_t reserve_floor_{free_after_work};
  size_t wait_for_bytes_{minimum_packet_space};
  TaskHandle_t helper_{};
  StaticSemaphore_t rows_done_storage_{};
  SemaphoreHandle_t rows_done_{};
  std::atomic<int> next_row_{panel_height};
  std::atomic<int> rows_left_{};
  uint16_t *raw_{};
  TaskHandle_t task_{};
  ScreenRotation rotation_{};
  jpeg_decoder_handle_t decoder_{};
  std::atomic<Phase> phase_{Phase::Empty};
  std::atomic<bool> pause_requested_{};
};

}  // namespace espframe::native
