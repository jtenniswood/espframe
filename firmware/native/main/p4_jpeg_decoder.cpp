#include "p4_jpeg_decoder.h"

#include "esp_heap_caps.h"
#include "jpeg_accelerator_helpers.h"
#include "slide_geometry.h"

#include <cstring>
#include <memory>

namespace espframe::native {
namespace {

using esphome::remote_image::P4JpegInfo;
using DmaBuffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;

bool inspect(const uint8_t *encoded, size_t length, P4JpegInfo &info) {
  using esphome::remote_image::parse_p4_baseline_jpeg;
  using esphome::remote_image::p4_jpeg_fits_workspace;
  // The hardware parser needs the whole stream and a pixel count divisible by eight.
  return encoded && length >= 4 && encoded[length - 2] == 0xff && encoded[length - 1] == 0xd9 &&
         parse_p4_baseline_jpeg(encoded, length, &info) && p4_jpeg_fits_workspace(info, length) &&
         info.width * info.height % 8 == 0;
}

DmaBuffer allocate(size_t bytes, jpeg_dec_buffer_alloc_direction_t direction, size_t &capacity) {
  jpeg_decode_memory_alloc_cfg_t config{};
  config.buffer_direction = direction;
  return DmaBuffer(static_cast<uint8_t *>(jpeg_alloc_decoder_mem(bytes, &config, &capacity)), heap_caps_free);
}

bool resize(const uint16_t *source, uint32_t stride, uint16_t *pixels, const PhotoFraming &framing,
            PreparationCheckpoint checkpoint) {
  const auto &crop = framing.source;
  const uint32_t step = (static_cast<uint32_t>(crop.width) << 16) / framing.width;
  for (int y = 0; y < framing.height; ++y) {
    if (y % 16 == 0 && !checkpoint())
      return false;
    const auto *row = source + (crop.y + y * crop.height / framing.height) * stride + crop.x;
    auto *target = pixels + y * framing.width;
    if (framing.width == crop.width) {
      std::memcpy(target, row, framing.width * sizeof(*pixels));
      continue;
    }
    uint32_t source_x = 0;
    for (int x = 0; x < framing.width; ++x) {
      target[x] = row[source_x >> 16];
      source_x += step;
    }
  }
  return true;
}

}  // namespace

P4JpegDecoder::~P4JpegDecoder() {
  if (engine_)
    jpeg_del_decoder_engine(engine_);
}

bool P4JpegDecoder::decode(const uint8_t *encoded, size_t length, uint16_t *pixels, const PhotoFraming &framing,
                            PreparationCheckpoint checkpoint) {
  P4JpegInfo info;
  if (disabled_ || !inspect(encoded, length, info) ||
      static_cast<uint32_t>(framing.source.x + framing.source.width) > info.width ||
      static_cast<uint32_t>(framing.source.y + framing.source.height) > info.height)
    return false;

  size_t output_capacity = 0;
  const auto output = allocate(info.decoded_bytes, JPEG_DEC_ALLOC_OUTPUT_BUFFER, output_capacity);
  if (!output || output_capacity < info.decoded_bytes ||
      !process_(encoded, length, output.get(), output_capacity, info.decoded_bytes, checkpoint))
    return false;
  return resize(reinterpret_cast<const uint16_t *>(output.get()), info.padded_width, pixels, framing, checkpoint);
}

bool P4JpegDecoder::decode_panel(const uint8_t *encoded, size_t length, uint16_t *pixels,
                                  PreparationCheckpoint checkpoint) {
  P4JpegInfo info;
  constexpr size_t bytes = pixel_count * sizeof(*pixels);
  return !disabled_ && inspect(encoded, length, info) && info.width == panel_width && info.height == panel_height &&
         info.padded_width == panel_width && info.decoded_bytes == bytes &&
         process_(encoded, length, reinterpret_cast<uint8_t *>(pixels), bytes, bytes, checkpoint);
}

bool P4JpegDecoder::process_(const uint8_t *encoded, size_t length, uint8_t *output, size_t capacity,
                             size_t expected_bytes, PreparationCheckpoint checkpoint) {
  size_t input_capacity = 0;
  const auto input = allocate(length, JPEG_DEC_ALLOC_INPUT_BUFFER, input_capacity);
  if (!input || input_capacity < length)
    return false;

  if (!engine_) {
    jpeg_decode_engine_cfg_t config{};
    config.timeout_ms = 150;
    if (jpeg_new_decoder_engine(&config, &engine_) != ESP_OK) {
      disabled_ = true;
      return false;
    }
  }

  if (!checkpoint())
    return false;
  std::memcpy(input.get(), encoded, length);
  if (!checkpoint())
    return false;
  jpeg_decode_cfg_t config{};
  config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t written = 0;
  const auto result = jpeg_decoder_process(engine_, &config, input.get(), length, output, capacity, &written);
  if (result == ESP_ERR_TIMEOUT) {
    jpeg_del_decoder_engine(engine_);
    engine_ = nullptr;
    disabled_ = true;
  }
  return result == ESP_OK && written == expected_bytes;
}

}  // namespace espframe::native
