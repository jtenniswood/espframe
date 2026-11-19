#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr int ESP_OK = 0;
inline constexpr int ESP_ERR_TIMEOUT = 0x107;

struct JpegDecoderEngine;
using jpeg_decoder_handle_t = JpegDecoderEngine *;
enum jpeg_dec_buffer_alloc_direction_t { JPEG_DEC_ALLOC_INPUT_BUFFER, JPEG_DEC_ALLOC_OUTPUT_BUFFER };
enum jpeg_dec_output_format_t { JPEG_DECODE_OUT_FORMAT_RGB565 };
enum jpeg_dec_rgb_element_order_t { JPEG_DEC_RGB_ELEMENT_ORDER_BGR };
enum jpeg_yuv_rgb_conv_std_t { JPEG_YUV_RGB_CONV_STD_BT601 };

struct jpeg_decode_engine_cfg_t {
  int timeout_ms;
};

struct jpeg_decode_memory_alloc_cfg_t {
  jpeg_dec_buffer_alloc_direction_t buffer_direction;
};

struct jpeg_decode_cfg_t {
  jpeg_dec_output_format_t output_format;
  jpeg_dec_rgb_element_order_t rgb_order;
  jpeg_yuv_rgb_conv_std_t conv_std;
};

int jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *, jpeg_decoder_handle_t *);
int jpeg_del_decoder_engine(jpeg_decoder_handle_t);
void *jpeg_alloc_decoder_mem(size_t, const jpeg_decode_memory_alloc_cfg_t *, size_t *);
int jpeg_decoder_process(jpeg_decoder_handle_t, const jpeg_decode_cfg_t *, const uint8_t *, uint32_t,
                         uint8_t *, uint32_t, uint32_t *);
