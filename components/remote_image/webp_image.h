#pragma once

#include "esphome/core/defines.h"
#include "image_decoder.h"
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT
#include <webp/decode.h>

namespace esphome {
namespace remote_image {

class WebpDecoder : public ImageDecoder {
 public:
  WebpDecoder(OnlineImage *image) : ImageDecoder(image) {}
  ~WebpDecoder() override { cleanup_(); }

  int prepare(size_t download_size) override;
  int HOT decode(uint8_t *buffer, size_t size) override;

 private:
  void cleanup_();

  uint8_t *rgb_buffer_{nullptr};
  int out_w_{0};
  int out_h_{0};
  bool finished_{false};
};

}  // namespace remote_image
}  // namespace esphome

#endif  // USE_REMOTE_IMAGE_WEBP_SUPPORT
