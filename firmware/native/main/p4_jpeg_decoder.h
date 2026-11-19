#pragma once

#include "photo_framing.h"
#include "preparation_checkpoint.h"
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

  bool decode(const uint8_t *encoded, size_t length, uint16_t *pixels, const PhotoFraming &framing,
              PreparationCheckpoint checkpoint = {});
  bool decode_panel(const uint8_t *encoded, size_t length, uint16_t *pixels, PreparationCheckpoint checkpoint);

 private:
  bool process_(const uint8_t *encoded, size_t length, uint8_t *output, size_t capacity, size_t expected_bytes,
                PreparationCheckpoint checkpoint);
  jpeg_decoder_handle_t engine_{};
  bool disabled_{};
};

}  // namespace espframe::native
