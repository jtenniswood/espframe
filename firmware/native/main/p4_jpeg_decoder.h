#pragma once

#include "photo_framing.h"
#include "driver/jpeg_decode.h"

#include <cstddef>
#include <cstdint>

namespace espframe::native {

// Reuse the worker's hardware engine. DMA scratch is held only during a
// decode so the prepared primary and companion can coexist in PSRAM.
class P4JpegDecoder {
 public:
  P4JpegDecoder() = default;
  ~P4JpegDecoder();
  P4JpegDecoder(const P4JpegDecoder &) = delete;
  P4JpegDecoder &operator=(const P4JpegDecoder &) = delete;

  bool decode(const uint8_t *encoded, size_t length, uint16_t *pixels, const PhotoFraming &framing);

 private:
  jpeg_decoder_handle_t engine_{};
  bool disabled_{};
};

}  // namespace espframe::native
