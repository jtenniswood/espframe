#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <webp/decode.h>

namespace esphome::remote_image {

// libwebp writes RGB565 most-significant byte first unless built with
// WEBP_SWAP_16BITS_CSP. Match the destination's configured byte order in place.
inline VP8StatusCode decode_webp_rgb565(const uint8_t *input, size_t input_size, WebPDecoderConfig &config,
                                        uint8_t *output, size_t capacity, int width, int height, bool big_endian) {
  if (output == nullptr || width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 2 ||
      static_cast<size_t>(height) > capacity / (static_cast<size_t>(width) * 2))
    return VP8_STATUS_INVALID_PARAM;
  config.output.colorspace = MODE_RGB_565;
  config.output.u.RGBA.rgba = output;
  config.output.u.RGBA.stride = width * 2;
  config.output.u.RGBA.size = capacity;
  config.output.is_external_memory = 1;
  const VP8StatusCode status = WebPDecode(input, input_size, &config);
  // Release decoder-owned bookkeeping on success and error. External output
  // remains owned by OnlineImage; it must never be freed by this decoder.
  WebPFreeDecBuffer(&config.output);
  if (status != VP8_STATUS_OK) return status;
#ifdef WEBP_SWAP_16BITS_CSP
  const bool swap = big_endian;
#else
  const bool swap = !big_endian;
#endif
  if (swap) {
    const size_t bytes = static_cast<size_t>(width) * height * 2;
    for (size_t i = 0; i < bytes; i += 2) {
      const uint8_t first = output[i];
      output[i] = output[i + 1];
      output[i + 1] = first;
    }
  }
  return status;
}

}  // namespace esphome::remote_image
