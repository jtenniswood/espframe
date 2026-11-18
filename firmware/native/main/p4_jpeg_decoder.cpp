#include "p4_jpeg_decoder.h"

#include "esp_heap_caps.h"
#include "jpeg_accelerator_helpers.h"

#include <cstring>
#include <memory>

namespace espframe::native {
namespace {

using esphome::remote_image::P4JpegInfo;
using DmaBuffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;

DmaBuffer allocate(size_t bytes, jpeg_dec_buffer_alloc_direction_t direction, size_t &capacity) {
  jpeg_decode_memory_alloc_cfg_t config{};
  config.buffer_direction = direction;
  return DmaBuffer(static_cast<uint8_t *>(jpeg_alloc_decoder_mem(bytes, &config, &capacity)), heap_caps_free);
}

void resize(const uint16_t *source, const P4JpegInfo &info, uint16_t *pixels, int width, int height) {
  const uint32_t step = (info.width << 16) / width;
  for (int y = 0; y < height; ++y) {
    const auto *row = source + (y * info.height / height) * info.padded_width;
    auto *target = pixels + y * width;
    if (static_cast<uint32_t>(width) == info.width) {
      std::memcpy(target, row, width * sizeof(*pixels));
      continue;
    }
    uint32_t source_x = 0;
    for (int x = 0; x < width; ++x) {
      target[x] = row[source_x >> 16];
      source_x += step;
    }
  }
}

}  // namespace

P4JpegDecoder::~P4JpegDecoder() {
  if (engine_)
    jpeg_del_decoder_engine(engine_);
}

bool P4JpegDecoder::decode(const uint8_t *encoded, size_t length, uint16_t *pixels, int width, int height) {
  using esphome::remote_image::parse_p4_baseline_jpeg;
  using esphome::remote_image::p4_jpeg_fits_workspace;
  P4JpegInfo info;
  // The hardware parser requires a whole stream and a pixel count divisible by eight.
  if (disabled_ || length < 4 || encoded[length - 2] != 0xff || encoded[length - 1] != 0xd9 ||
      !parse_p4_baseline_jpeg(encoded, length, &info) || !p4_jpeg_fits_workspace(info, length) ||
      info.width * info.height % 8 != 0)
    return false;

  size_t input_capacity = 0;
  const auto input = allocate(length, JPEG_DEC_ALLOC_INPUT_BUFFER, input_capacity);
  if (!input || input_capacity < length)
    return false;
  size_t output_capacity = 0;
  const auto output = allocate(info.decoded_bytes, JPEG_DEC_ALLOC_OUTPUT_BUFFER, output_capacity);
  if (!output || output_capacity < info.decoded_bytes)
    return false;

  if (!engine_) {
    jpeg_decode_engine_cfg_t config{};
    config.timeout_ms = 150;
    if (jpeg_new_decoder_engine(&config, &engine_) != ESP_OK) {
      disabled_ = true;
      return false;
    }
  }

  std::memcpy(input.get(), encoded, length);
  jpeg_decode_cfg_t config{};
  config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t written = 0;
  const auto result = jpeg_decoder_process(engine_, &config, input.get(), length, output.get(), output_capacity, &written);
  if (result == ESP_ERR_TIMEOUT) {
    jpeg_del_decoder_engine(engine_);
    engine_ = nullptr;
    disabled_ = true;
  }
  if (result != ESP_OK || written != info.decoded_bytes)
    return false;
  resize(reinterpret_cast<const uint16_t *>(output.get()), info, pixels, width, height);
  return true;
}

}  // namespace espframe::native
