#include "preview_decoder.h"

#include "esp_heap_caps.h"
#include "slide_geometry.h"
#include "webp_rgb565.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <jerror.h>
#include <jpeglib.h>

namespace espframe::native {
namespace {

struct Size {
  int width;
  int height;
};

bool supported(unsigned width, unsigned height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 && width * height <= 4'000'000;
}

Size fit(int width, int height) {
  if (width * slide_height >= height * slide_width)
    return {slide_width, std::max(1, height * slide_width / width)};
  return {std::max(1, width * slide_height / height), slide_height};
}

struct JpegError {
  jpeg_error_mgr manager;
  std::jmp_buf escape;
};

void jpeg_failure(j_common_ptr info) {
  auto *error = reinterpret_cast<JpegError *>(info->err);
  std::longjmp(error->escape, 1);
}

void jpeg_message(j_common_ptr info, int) {
  if (info->err->msg_code == JWRN_JPEG_EOF)
    jpeg_failure(info);
}

bool read_jpeg(jpeg_decompress_struct &decoder, JpegError &error, const uint8_t *encoded,
               size_t length, uint16_t *pixels, Size &size) {
  if (setjmp(error.escape) != 0)
    return false;

  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, encoded, length);
  if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK ||
      !supported(decoder.image_width, decoder.image_height))
    return false;
  size = fit(static_cast<int>(decoder.image_width), static_cast<int>(decoder.image_height));
  // Let the DCT do the first downsampling step when both axes can afford it.
  for (unsigned divisor : {2, 4, 8}) {
    if (decoder.image_width / divisor < static_cast<unsigned>(size.width) ||
        decoder.image_height / divisor < static_cast<unsigned>(size.height))
      break;
    decoder.scale_denom = divisor;
  }
  decoder.out_color_space = JCS_RGB;
  decoder.do_fancy_upsampling = FALSE;
  if (!jpeg_start_decompress(&decoder) || decoder.output_components != 3)
    return false;
  auto rows = decoder.mem->alloc_sarray(reinterpret_cast<j_common_ptr>(&decoder), JPOOL_IMAGE,
                                        decoder.output_width * 3, 1);
  unsigned target_y = 0;
  while (decoder.output_scanline < decoder.output_height) {
    const unsigned source_y = decoder.output_scanline;
    if (jpeg_read_scanlines(&decoder, rows, 1) != 1)
      return false;
    while (target_y < static_cast<unsigned>(size.height) &&
           target_y * decoder.output_height / size.height == source_y) {
      auto *line = pixels + target_y * size.width;
      for (int x = 0; x < size.width; ++x) {
        const auto *rgb = rows[0] + (x * decoder.output_width / size.width) * 3;
        line[x] = static_cast<uint16_t>(((rgb[0] & 0xf8) << 8) | ((rgb[1] & 0xfc) << 3) | (rgb[2] >> 3));
      }
      ++target_y;
    }
  }
  return jpeg_finish_decompress(&decoder);
}

bool decode_jpeg(const uint8_t *encoded, size_t length, uint16_t *pixels, Size &size) {
  jpeg_decompress_struct decoder{};
  JpegError error{};
  decoder.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = jpeg_failure;
  error.manager.emit_message = jpeg_message;
  const bool ok = read_jpeg(decoder, error, encoded, length, pixels, size);
  jpeg_destroy_decompress(&decoder);
  return ok;
}

bool decode_webp(const uint8_t *encoded, size_t length, uint16_t *pixels, Size &size) {
  WebPDecoderConfig decoder;
  if (!WebPInitDecoderConfig(&decoder) || WebPGetFeatures(encoded, length, &decoder.input) != VP8_STATUS_OK ||
      decoder.input.has_animation || !supported(decoder.input.width, decoder.input.height))
    return false;
  size = fit(decoder.input.width, decoder.input.height);
  decoder.options.use_scaling = size.width != decoder.input.width || size.height != decoder.input.height;
  decoder.options.scaled_width = size.width;
  decoder.options.scaled_height = size.height;
  decoder.options.no_fancy_upsampling = 1;
  return esphome::remote_image::decode_webp_rgb565(encoded, length, decoder, reinterpret_cast<uint8_t *>(pixels),
                                                  size.width * size.height * sizeof(*pixels), size.width,
                                                  size.height, false) == VP8_STATUS_OK;
}

void rotate_into_panel(const uint16_t *pixels, Size size, uint16_t *panel) {
  if (size.width != slide_width || size.height != slide_height)
    std::fill_n(panel, pixel_count, 0);
  const int left = (slide_width - size.width) / 2;
  const int top = (slide_height - size.height) / 2;
  constexpr int tile = 16;
  for (int y0 = 0; y0 < size.height; y0 += tile) {
    for (int x0 = 0; x0 < size.width; x0 += tile) {
      for (int x = x0; x < std::min(x0 + tile, size.width); ++x) {
        auto *column = panel + (x + left) * panel_width;
        for (int y = y0; y < std::min(y0 + tile, size.height); ++y)
          column[panel_width - 1 - top - y] = pixels[y * size.width + x];
      }
    }
  }
}

}  // namespace

bool decode_preview(const uint8_t *encoded, size_t length, uint16_t *panel) {
  const bool jpeg = length >= 2 && encoded[0] == 0xff && encoded[1] == 0xd8;
  const bool webp = length >= 12 && std::memcmp(encoded, "RIFF", 4) == 0 &&
                    std::memcmp(encoded + 8, "WEBP", 4) == 0;
  if (!jpeg && !webp)
    return false;
  auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(pixel_count * sizeof(*panel), MALLOC_CAP_SPIRAM));
  if (pixels == nullptr)
    return false;
  Size size{};
  const bool ok = jpeg ? decode_jpeg(encoded, length, pixels, size) : decode_webp(encoded, length, pixels, size);
  if (ok)
    rotate_into_panel(pixels, size, panel);
  heap_caps_free(pixels);
  return ok;
}

}  // namespace espframe::native
