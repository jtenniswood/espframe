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

using Decode = DecodedPreview::Result;

struct Size {
  int width;
  int height;
  bool portrait{};
};

constexpr int pair_gap = 8;
constexpr int pair_width = (slide_width - pair_gap) / 2;

bool supported(unsigned width, unsigned height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 && width * height <= 4'000'000;
}

Size fit(int width, int height, int max_width) {
  if (width * slide_height >= height * max_width)
    return {max_width, std::max(1, height * max_width / width), height > width};
  return {std::max(1, width * slide_height / height), slide_height, height > width};
}

uint16_t *allocate(Size size) {
  return static_cast<uint16_t *>(heap_caps_malloc(size.width * size.height * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
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

Decode read_jpeg(jpeg_decompress_struct &decoder, JpegError &error, const uint8_t *encoded,
                 size_t length, uint16_t *&pixels, Size &size, int max_width) {
  if (setjmp(error.escape) != 0)
    return error.manager.msg_code == JERR_OUT_OF_MEMORY ? Decode::NoMemory : Decode::Invalid;

  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, encoded, length);
  if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK ||
      !supported(decoder.image_width, decoder.image_height))
    return Decode::Invalid;
  size = fit(static_cast<int>(decoder.image_width), static_cast<int>(decoder.image_height), max_width);
  pixels = allocate(size);
  if (pixels == nullptr)
    return Decode::NoMemory;
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
    return Decode::Invalid;
  auto rows = decoder.mem->alloc_sarray(reinterpret_cast<j_common_ptr>(&decoder), JPOOL_IMAGE,
                                        decoder.output_width * 3, 1);
  unsigned target_y = 0;
  while (decoder.output_scanline < decoder.output_height) {
    const unsigned source_y = decoder.output_scanline;
    if (jpeg_read_scanlines(&decoder, rows, 1) != 1)
      return Decode::Invalid;
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
  return jpeg_finish_decompress(&decoder) ? Decode::Ready : Decode::Invalid;
}

Decode decode_jpeg(const uint8_t *encoded, size_t length, uint16_t *&pixels, Size &size, int max_width) {
  jpeg_decompress_struct decoder{};
  JpegError error{};
  decoder.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = jpeg_failure;
  error.manager.emit_message = jpeg_message;
  const auto result = read_jpeg(decoder, error, encoded, length, pixels, size, max_width);
  jpeg_destroy_decompress(&decoder);
  return result;
}

Decode decode_webp(const uint8_t *encoded, size_t length, uint16_t *&pixels, Size &size, int max_width) {
  WebPDecoderConfig decoder;
  if (!WebPInitDecoderConfig(&decoder) || WebPGetFeatures(encoded, length, &decoder.input) != VP8_STATUS_OK ||
      decoder.input.has_animation || !supported(decoder.input.width, decoder.input.height))
    return Decode::Invalid;
  size = fit(decoder.input.width, decoder.input.height, max_width);
  pixels = allocate(size);
  if (pixels == nullptr)
    return Decode::NoMemory;
  decoder.options.use_scaling = size.width != decoder.input.width || size.height != decoder.input.height;
  decoder.options.scaled_width = size.width;
  decoder.options.scaled_height = size.height;
  decoder.options.no_fancy_upsampling = 1;
  const auto status = esphome::remote_image::decode_webp_rgb565(
      encoded, length, decoder, reinterpret_cast<uint8_t *>(pixels), size.width * size.height * sizeof(*pixels),
      size.width, size.height, false);
  if (status == VP8_STATUS_OK)
    return Decode::Ready;
  return status == VP8_STATUS_OUT_OF_MEMORY ? Decode::NoMemory : Decode::Invalid;
}

}  // namespace

DecodedPreview::~DecodedPreview() { heap_caps_free(this->pixels_); }

void DecodedPreview::paint_(uint16_t *panel, int left, int max_width) const {
  const auto size = this->width_ <= max_width ? Size{this->width_, this->height_}
                                              : fit(this->width_, this->height_, max_width);
  left += (max_width - size.width) / 2;
  const int top = (slide_height - size.height) / 2;
  // Refit an already decoded portrait to a half-screen without divisions
  // in the pixel loop. A 1:1 fit keeps the original integer coordinates.
  const uint32_t scale_x = (static_cast<uint32_t>(this->width_) << 16) / size.width;
  const uint32_t scale_y = (static_cast<uint32_t>(this->height_) << 16) / size.height;
  constexpr int tile = 16;
  for (int y0 = 0; y0 < size.height; y0 += tile) {
    for (int x0 = 0; x0 < size.width; x0 += tile) {
      for (int x = x0; x < std::min(x0 + tile, size.width); ++x) {
        auto *column = panel + (x + left) * panel_width;
        const int source_x = (x * scale_x) >> 16;
        for (int y = y0; y < std::min(y0 + tile, size.height); ++y) {
          const int source_y = (y * scale_y) >> 16;
          column[panel_width - 1 - top - y] = this->pixels_[source_y * this->width_ + source_x];
        }
      }
    }
  }
}

DecodedPreview::Result DecodedPreview::decode(const uint8_t *encoded, size_t length, bool companion) {
  heap_caps_free(this->pixels_);
  this->pixels_ = nullptr;
  this->width_ = 0;
  this->height_ = 0;
  this->portrait_ = false;
  const bool jpeg = length >= 2 && encoded[0] == 0xff && encoded[1] == 0xd8;
  const bool webp = length >= 12 && std::memcmp(encoded, "RIFF", 4) == 0 &&
                    std::memcmp(encoded + 8, "WEBP", 4) == 0;
  if (!jpeg && !webp)
    return Result::Invalid;
  Size size{};
  const int max_width = companion ? pair_width : slide_width;
  const auto result = jpeg ? decode_jpeg(encoded, length, this->pixels_, size, max_width)
                           : decode_webp(encoded, length, this->pixels_, size, max_width);
  if (result == Result::Ready) {
    this->width_ = size.width;
    this->height_ = size.height;
    this->portrait_ = size.portrait;
  } else {
    heap_caps_free(this->pixels_);
    this->pixels_ = nullptr;
  }
  return result;
}

void DecodedPreview::render(uint16_t *panel, const DecodedPreview *companion) const {
  const bool pair = companion != nullptr && this->portrait() && companion->portrait();
  if (pair || this->width_ != slide_width || this->height_ != slide_height)
    std::fill_n(panel, pixel_count, 0);
  if (this->width_ == 0)
    return;
  this->paint_(panel, 0, pair ? pair_width : slide_width);
  if (pair)
    companion->paint_(panel, pair_width + pair_gap, pair_width);
}

}  // namespace espframe::native
