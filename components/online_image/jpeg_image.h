#pragma once

#include "image_decoder.h"
#include "esphome/core/defines.h"
#ifdef USE_ONLINE_IMAGE_JPEG_SUPPORT
#include <jpeglib.h>
#include <csetjmp>

namespace esphome {
namespace online_image {

struct JpegErrorMgr {
  jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
  char message[JMSG_LENGTH_MAX];
};

class JpegDecoder : public ImageDecoder {
 public:
  JpegDecoder(OnlineImage *image) : ImageDecoder(image) {}
  ~JpegDecoder() override { cleanup_(); }

  int prepare(size_t download_size) override;
  int HOT decode(uint8_t *buffer, size_t size) override;

 private:
  enum Phase { WAITING, DECOMPRESSING, FINISHED };

  void cleanup_();

  Phase phase_ = WAITING;
  jpeg_decompress_struct *cinfo_ = nullptr;
  JpegErrorMgr *jerr_ = nullptr;
  uint8_t *row_buffer_ = nullptr;
  int out_w_ = 0;
  int current_scanline_ = 0;
  int prev_dst_y_ = -1;
  int prev_gap_end_ = 0;
  bool use_rgb565_ = false;
  bool big_endian_ = false;
  bool scaling_ = false;

  static constexpr int SCANLINES_PER_CHUNK = 100;
};

}  // namespace online_image
}  // namespace esphome

#endif  // USE_ONLINE_IMAGE_JPEG_SUPPORT
