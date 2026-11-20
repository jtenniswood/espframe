#include "preview_decoder.h"

#include "esp_heap_caps.h"
#include "p4_jpeg_decoder.h"
#include "slide_geometry.h"

#include <algorithm>
#include <bit>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <jerror.h>
#include <jpeglib.h>
#include <webp/decode.h>

namespace espframe::native {
namespace {

using Decode = DecodedPreview::Result;

constexpr int row_batch = 16;

bool clear_region(uint16_t *panel, int left, int top, int width, int height,
                  ScreenRotation rotation, PreparationCheckpoint checkpoint) {
  if (width == 0 || height == 0)
    return true;
  // A logical column is stored as one scanline on the panel.
  const bool flipped = rotation == ScreenRotation::UpsideDown;
  const int start_column = flipped ? slide_height - 1 - top : slide_height - top - height;
  for (int x = left; x < left + width; x += row_batch) {
    if (!checkpoint())
      return false;
    const int count = std::min(row_batch, left + width - x);
    if (height == slide_height) {
      const int first_x = flipped ? x + count - 1 : x;
      std::fill_n(panel + panel_offset(first_x, start_column, rotation), count * height, 0);
    } else {
      for (int column = x; column < x + count; ++column)
        std::fill_n(panel + panel_offset(column, start_column, rotation), height, 0);
    }
  }
  return true;
}

bool supported(unsigned width, unsigned height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 && width * height <= 4'000'000;
}

PhotoFraming framing(int width, int height, PreviewLayout layout) {
  const bool portrait = height > width;
  if (layout.target == PreviewTarget::PairPane || (layout.target == PreviewTarget::RequiredPair && portrait))
    return frame_photo(width, height, portrait_pair_width, slide_height, layout.pair);
  // Retain the portrait's full image until the companion is ready so
  // either layout can choose its own crop.
  const auto effective = layout.target == PreviewTarget::OptionalPair && portrait ? DisplayMode::Fit : layout.photo;
  return frame_photo(width, height, slide_width, slide_height, effective);
}

uint16_t *allocate(const PhotoFraming &size) {
  return static_cast<uint16_t *>(heap_caps_malloc(size.width * size.height * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
}

struct JpegCallbacks {
  jpeg_error_mgr manager;
  std::jmp_buf escape;
  jpeg_progress_mgr progress;
  PreparationCheckpoint checkpoint;
};

constexpr int jpeg_cancelled = 2;

void jpeg_failure(j_common_ptr info) {
  auto *callbacks = reinterpret_cast<JpegCallbacks *>(info->err);
  std::longjmp(callbacks->escape, 1);
}

void jpeg_message(j_common_ptr info, int) {
  if (info->err->msg_code == JWRN_JPEG_EOF)
    jpeg_failure(info);
}

void jpeg_progress(j_common_ptr info) {
  auto *callbacks = reinterpret_cast<JpegCallbacks *>(info->err);
  if (!callbacks->checkpoint())
    std::longjmp(callbacks->escape, jpeg_cancelled);
}

Decode read_jpeg(jpeg_decompress_struct &decoder, JpegCallbacks &callbacks, const uint8_t *encoded,
                 size_t length, uint16_t *&pixels, PhotoFraming &size, PreviewLayout layout,
                 P4JpegDecoder *hardware, bool &accelerated, PreparationCheckpoint checkpoint, PhotoOrientation orientation) {
  if (const int status = setjmp(callbacks.escape); status != 0) {
    if (status == jpeg_cancelled)
      return Decode::Interrupted;
    return callbacks.manager.msg_code == JERR_OUT_OF_MEMORY ? Decode::NoMemory : Decode::Invalid;
  }

  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, encoded, length);
  if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK ||
      !supported(decoder.image_width, decoder.image_height))
    return Decode::Invalid;
  if (!matches_photo_orientation(orientation, decoder.image_height > decoder.image_width))
    return Decode::OrientationMismatch;
  size = framing(decoder.image_width, decoder.image_height, layout);
  pixels = allocate(size);
  if (pixels == nullptr)
    return Decode::NoMemory;

  // Reuse libjpeg's header interpretation to keep RGB, progressive, and
  // non-interleaved JPEGs out of the hardware YCbCr path.
  if (hardware && decoder.jpeg_color_space == JCS_YCbCr && !decoder.progressive_mode &&
      !decoder.arith_code && decoder.comps_in_scan == 3 &&
      hardware->decode(encoded, length, pixels, size, checkpoint)) {
    accelerated = true;
    return Decode::Ready;
  }
  if (!checkpoint())
    return Decode::Interrupted;
  // libjpeg-turbo scales by eighths in the IDCT. Pick the smallest scale
  // that still covers the crop at its final size on both axes.
  const auto &crop = size.source;
  const auto scale_needed = [](unsigned source, unsigned target) {
    return (DCTSIZE * target + source - 1) / source;
  };
  const unsigned scale = std::max(scale_needed(crop.width, size.width), scale_needed(crop.height, size.height));
  if (scale < DCTSIZE) {
    decoder.scale_num = scale;
    decoder.scale_denom = DCTSIZE;
  }
  // The common YCbCr path can pack the final color in libjpeg's upsampler.
  // Leave RGB JPEGs on the existing path, including lossless RGB input.
  const bool packed = std::endian::native == std::endian::little && decoder.jpeg_color_space == JCS_YCbCr;
  decoder.out_color_space = packed ? JCS_RGB565 : JCS_RGB;
  decoder.dither_mode = JDITHER_NONE;
  decoder.do_fancy_upsampling = FALSE;
  // Libjpeg reads progressive scans inside jpeg_start_decompress. Its
  // progress hook lets a swipe pause or cancel between MCU rows.
  if (checkpoint.proceed && jpeg_has_multiple_scans(&decoder)) {
    callbacks.checkpoint = checkpoint;
    callbacks.progress.progress_monitor = jpeg_progress;
    decoder.progress = &callbacks.progress;
  }
  if (!jpeg_start_decompress(&decoder) || decoder.output_components != 3)
    return Decode::Invalid;
  decoder.progress = nullptr;

  const auto left = crop.x * decoder.output_width / decoder.image_width;
  const auto top = crop.y * decoder.output_height / decoder.image_height;
  const auto crop_width = std::max(1u, (crop.x + crop.width) * decoder.output_width / decoder.image_width - left);
  const auto crop_height = std::max(1u, (crop.y + crop.height) * decoder.output_height / decoder.image_height - top);
  // Some previews already have the right dimensions. Decode their rows
  // straight into the framed preview without a scratch row or a pixel copy.
  const bool direct = packed && left == 0 && top == 0 && crop_width == decoder.output_width &&
                      crop_height == decoder.output_height && static_cast<unsigned>(size.width) == crop_width &&
                      static_cast<unsigned>(size.height) == crop_height;
  JSAMPARRAY rows = nullptr;
  uint16_t *column_offsets = nullptr;
  if (!direct) {
    rows = decoder.mem->alloc_sarray(reinterpret_cast<j_common_ptr>(&decoder), JPOOL_IMAGE,
                                     decoder.output_width * (packed ? 2 : 3), 1);
    column_offsets = static_cast<uint16_t *>(decoder.mem->alloc_small(
        reinterpret_cast<j_common_ptr>(&decoder), JPOOL_IMAGE, size.width * sizeof(uint16_t)));
    // Packed rows use pixel offsets. RGB rows use byte offsets.
    for (int x = 0; x < size.width; ++x)
      column_offsets[x] = (packed ? 1 : 3) * (left + x * crop_width / size.width);
  }

  unsigned target_y = 0;
  while (decoder.output_scanline < decoder.output_height) {
    const unsigned source_y = decoder.output_scanline;
    if (source_y % row_batch == 0 && !checkpoint())
      return Decode::Interrupted;
    auto *scanline = direct ? reinterpret_cast<JSAMPROW>(pixels + source_y * size.width) : rows[0];
    if (jpeg_read_scanlines(&decoder, &scanline, 1) != 1)
      return Decode::Invalid;
    if (direct)
      continue;
    while (target_y < static_cast<unsigned>(size.height) &&
           top + target_y * crop_height / size.height == source_y) {
      auto *line = pixels + target_y * size.width;
      if (packed) {
        const auto *rgb565 = reinterpret_cast<const uint16_t *>(scanline);
        if (crop_width == static_cast<unsigned>(size.width))
          std::copy_n(rgb565 + left, size.width, line);
        else
          for (int x = 0; x < size.width; ++x)
            line[x] = rgb565[column_offsets[x]];
      } else {
        for (int x = 0; x < size.width; ++x) {
          const auto *rgb = scanline + column_offsets[x];
          line[x] = static_cast<uint16_t>(((rgb[0] & 0xf8) << 8) | ((rgb[1] & 0xfc) << 3) | (rgb[2] >> 3));
        }
      }
      ++target_y;
    }
  }
  return jpeg_finish_decompress(&decoder) ? Decode::Ready : Decode::Invalid;
}

Decode decode_jpeg(const uint8_t *encoded, size_t length, uint16_t *&pixels, PhotoFraming &size, PreviewLayout layout,
                   P4JpegDecoder *hardware, bool &accelerated, PreparationCheckpoint checkpoint,
                   PhotoOrientation orientation) {
  jpeg_decompress_struct decoder{};
  JpegCallbacks callbacks{};
  decoder.err = jpeg_std_error(&callbacks.manager);
  callbacks.manager.error_exit = jpeg_failure;
  callbacks.manager.emit_message = jpeg_message;
  const auto result = read_jpeg(decoder, callbacks, encoded, length, pixels, size, layout, hardware, accelerated,
                                checkpoint, orientation);
  jpeg_destroy_decompress(&decoder);
  return result;
}

Decode decode_webp(const uint8_t *encoded, size_t length, uint16_t *&pixels, PhotoFraming &size, PreviewLayout layout,
                   PreparationCheckpoint checkpoint, PhotoOrientation orientation) {
  WebPDecoderConfig decoder;
  if (!WebPInitDecoderConfig(&decoder) || WebPGetFeatures(encoded, length, &decoder.input) != VP8_STATUS_OK ||
      decoder.input.has_animation || !supported(decoder.input.width, decoder.input.height))
    return Decode::Invalid;
  if (!matches_photo_orientation(orientation, decoder.input.height > decoder.input.width))
    return Decode::OrientationMismatch;
  size = framing(decoder.input.width, decoder.input.height, layout);
  pixels = allocate(size);
  if (pixels == nullptr)
    return Decode::NoMemory;
  const auto &crop = size.source;
  decoder.options.use_cropping = crop.width != decoder.input.width || crop.height != decoder.input.height;
  // WebP's chroma planes use even crop origins.
  decoder.options.crop_left = crop.x & ~1;
  decoder.options.crop_top = crop.y & ~1;
  decoder.options.crop_width = crop.width;
  decoder.options.crop_height = crop.height;
  decoder.options.use_scaling = size.width != crop.width || size.height != crop.height;
  decoder.options.scaled_width = size.width;
  decoder.options.scaled_height = size.height;
  decoder.options.no_fancy_upsampling = 1;
  decoder.output.colorspace = MODE_RGB_565;
  decoder.output.u.RGBA.rgba = reinterpret_cast<uint8_t *>(pixels);
  decoder.output.u.RGBA.stride = size.width * sizeof(*pixels);
  decoder.output.u.RGBA.size = decoder.output.u.RGBA.stride * size.height;
  decoder.output.is_external_memory = 1;
  struct Progress {
    PreparationCheckpoint checkpoint;
    bool interrupted{};
  } progress{checkpoint};
  const auto status = WebPDecodeWithCheckpoint(
      encoded, length, &decoder, [](void *context) {
        auto &progress = *static_cast<Progress *>(context);
        progress.interrupted = !progress.checkpoint();
        return progress.interrupted ? 0 : 1;
      }, &progress);
  WebPFreeDecBuffer(&decoder.output);
  if (status == VP8_STATUS_OK)
    return Decode::Ready;
  if (status == VP8_STATUS_USER_ABORT && progress.interrupted)
    return Decode::Interrupted;
  return status == VP8_STATUS_OUT_OF_MEMORY ? Decode::NoMemory : Decode::Invalid;
}

}  // namespace

DecodedPreview::~DecodedPreview() { heap_caps_free(this->pixels_); }

bool DecodedPreview::paint_(uint16_t *panel, int left, int max_width, DisplayMode mode,
                            ScreenRotation rotation, PreparationCheckpoint checkpoint) const {
  // An existing fit can be placed as-is. Avoid a second rounding step on
  // images already scaled to the destination before rotating the pixels.
  const auto size = mode == DisplayMode::Fit && this->width_ <= max_width
                        ? PhotoFraming{{0, 0, this->width_, this->height_}, this->width_, this->height_, this->portrait_}
                        : frame_photo(this->width_, this->height_, max_width, slide_height, mode);
  const int side = (max_width - size.width) / 2;
  const int top = (slide_height - size.height) / 2;
  const int photo_left = left + side;
  const int photo_right = photo_left + size.width;
  const int photo_bottom = top + size.height;
  if (size.width != max_width &&
      (!clear_region(panel, left, 0, side, slide_height, rotation, checkpoint) ||
       !clear_region(panel, photo_right, 0, left + max_width - photo_right, slide_height, rotation, checkpoint)))
    return false;
  if (size.height != slide_height &&
      (!clear_region(panel, photo_left, 0, size.width, top, rotation, checkpoint) ||
       !clear_region(panel, photo_left, photo_bottom, size.width, slide_height - photo_bottom, rotation, checkpoint)))
    return false;
  // Refit an already decoded portrait to a half-screen without divisions
  // in the pixel loop. A 1:1 fit keeps the original integer coordinates.
  const auto &crop = size.source;
  const uint32_t scale_x = (static_cast<uint32_t>(crop.width) << 16) / size.width;
  const uint32_t scale_y = (static_cast<uint32_t>(crop.height) << 16) / size.height;
  const int column_step = -axis_direction(rotation);
  constexpr int tile = row_batch;
  for (int y0 = 0; y0 < size.height; y0 += tile) {
    if (!checkpoint())
      return false;
    for (int x0 = 0; x0 < size.width; x0 += tile) {
      for (int x = x0; x < std::min(x0 + tile, size.width); ++x) {
        auto *column = panel + panel_offset(x + photo_left, panel_width - 1 - top, rotation);
        const int source_x = crop.x + ((x * scale_x) >> 16);
        for (int y = y0; y < std::min(y0 + tile, size.height); ++y) {
          const int source_y = crop.y + ((y * scale_y) >> 16);
          column[y * column_step] = this->pixels_[source_y * this->width_ + source_x];
        }
      }
    }
  }
  return true;
}

bool is_webp_preview(const uint8_t *encoded, size_t length) {
  return length >= 12 && std::memcmp(encoded, "RIFF", 4) == 0 && std::memcmp(encoded + 8, "WEBP", 4) == 0;
}

DecodedPreview::Result DecodedPreview::decode(const uint8_t *encoded, size_t length, PreviewLayout layout,
                                              P4JpegDecoder *hardware,
                                           PreparationCheckpoint checkpoint, PhotoOrientation orientation) {
  heap_caps_free(this->pixels_);
  this->pixels_ = nullptr;
  this->width_ = 0;
  this->height_ = 0;
  this->portrait_ = false;
  this->hardware_jpeg_ = false;
  if (!checkpoint())
    return Result::Interrupted;
  const bool jpeg = is_jpeg_preview(encoded, length);
  const bool webp = is_webp_preview(encoded, length);
  if (!jpeg && !webp)
    return Result::Invalid;
  PhotoFraming size{};
  const auto result = jpeg ? decode_jpeg(encoded, length, this->pixels_, size, layout, hardware,
                                        this->hardware_jpeg_, checkpoint, orientation)
                           : decode_webp(encoded, length, this->pixels_, size, layout, checkpoint, orientation);
  if (result == Result::Ready) {
    this->width_ = size.width;
    this->height_ = size.height;
    this->portrait_ = size.portrait;
    this->display_mode_ = layout.photo;
  } else {
    heap_caps_free(this->pixels_);
    this->pixels_ = nullptr;
  }
  return result;
}

bool DecodedPreview::render(uint16_t *panel, const DecodedPreview *companion, DisplayMode pair_mode,
                            ScreenRotation rotation, PreparationCheckpoint checkpoint) const {
  const bool pair = companion != nullptr && this->portrait() && companion->portrait();
  if (this->width_ == 0)
    return clear_region(panel, 0, 0, slide_width, slide_height, rotation, checkpoint);
  if (pair && !clear_region(panel, portrait_pair_width, 0, portrait_pair_gap, slide_height, rotation, checkpoint))
    return false;
  return this->paint_(panel, 0, pair ? portrait_pair_width : slide_width, pair ? pair_mode : this->display_mode_, rotation,
                      checkpoint) &&
         (!pair || companion->paint_(panel, portrait_pair_width + portrait_pair_gap, portrait_pair_width, pair_mode,
                                     rotation, checkpoint));
}

}  // namespace espframe::native
