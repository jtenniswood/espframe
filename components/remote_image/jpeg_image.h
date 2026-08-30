#pragma once

#include "image_decoder.h"
#include "esphome/core/defines.h"
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT
#include <jpeglib.h>
#include <csetjmp>

namespace esphome {
namespace remote_image {

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
  enum Phase { WAITING, HARDWARE_DRAWING, DECOMPRESSING, FINISHED };

  void cleanup_();
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  bool try_start_hardware_decode_(uint8_t *buffer, size_t size);
  void continue_hardware_draw_();
#endif

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
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  bool hardware_attempted_ = false;
  uint32_t hardware_src_width_ = 0;
  uint32_t hardware_src_height_ = 0;
  uint32_t hardware_stride_ = 0;
  int hardware_draw_y_ = -1;
  uint32_t hardware_started_us_ = 0;
  uint32_t hardware_decode_us_ = 0;
  uint32_t hardware_draw_us_ = 0;
#endif

  static constexpr int SCANLINES_PER_CHUNK = 100;
  static constexpr int HARDWARE_DRAW_ROWS_PER_CHUNK = 32;
};

}  // namespace remote_image
}  // namespace esphome

#endif  // USE_REMOTE_IMAGE_JPEG_SUPPORT
