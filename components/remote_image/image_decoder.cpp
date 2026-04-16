#include "image_decoder.h"
#include "remote_image.h"

#include "esphome/core/log.h"

namespace esphome {
namespace remote_image {

// Shared drawing helpers used by all image formats. Format decoders discover
// source dimensions and produce rows/blocks; this layer maps those pixels into
// the final ESPHome image buffer, including fit/fill scaling and centering.
static const char *const TAG = "remote_image.decoder";

bool ImageDecoder::set_size(int width, int height) {
  if (width <= 0 || height <= 0) {
    ESP_LOGE(TAG, "Invalid image dimensions: %dx%d", width, height);
    return false;
  }
  bool success = this->image_->resize_(width, height) > 0;

  int buf_w = this->image_->buffer_width_;
  int buf_h = this->image_->buffer_height_;

  // The remote image may have fixed display dimensions. When it does, keep the
  // buffer at the fixed size and calculate how incoming source pixels should be
  // scaled and centered inside it.
  if (buf_w == width && buf_h == height) {
    this->x_scale_ = 1.0;
    this->y_scale_ = 1.0;
    this->x_offset_ = 0;
    this->y_offset_ = 0;
    this->scaled_width_ = width;
    this->scaled_height_ = height;
  } else {
    double scale = this->image_->fill_mode_
      ? std::max(static_cast<double>(buf_w) / width,
                 static_cast<double>(buf_h) / height)
      : std::min(static_cast<double>(buf_w) / width,
                 static_cast<double>(buf_h) / height);
    this->x_scale_ = scale;
    this->y_scale_ = scale;
    this->scaled_width_ = static_cast<int>(width * scale);
    this->scaled_height_ = static_cast<int>(height * scale);
    this->x_offset_ = (buf_w - this->scaled_width_) / 2;
    this->y_offset_ = (buf_h - this->scaled_height_) / 2;
  }

  // Clear the destination unless the fill-mode fast path is about to overwrite
  // every RGB565 pixel. Avoiding that clear saves time on large photos.
  if (success && !(this->image_->fill_mode_ && this->image_->fixed_width_ > 0
                   && this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565)) {
    memset(this->image_->buffer_, 0, this->image_->get_buffer_size_());
  }

  // Horizontal lookup table lets row decoders scale by indexing into a prepared
  // map instead of recalculating source columns for every output pixel.
  double inv = (this->x_scale_ > 0) ? 1.0 / this->x_scale_ : 1.0;
  this->src_x_lut_.resize(this->scaled_width_);
  for (int i = 0; i < this->scaled_width_; i++) {
    this->src_x_lut_[i] = std::min(static_cast<int>(i * inv), width - 1);
  }

  return success;
}

void ImageDecoder::draw(int x, int y, int w, int h, const Color &color) {
  int start_x = std::max(0, static_cast<int>(x * this->x_scale_) + this->x_offset_);
  int start_y = std::max(0, static_cast<int>(y * this->y_scale_) + this->y_offset_);
  int end_x = std::min(this->image_->buffer_width_,
                       static_cast<int>(std::ceil((x + w) * this->x_scale_)) + this->x_offset_);
  int end_y = std::min(this->image_->buffer_height_,
                       static_cast<int>(std::ceil((y + h) * this->y_scale_)) + this->y_offset_);
  for (int i = start_x; i < end_x; i++) {
    for (int j = start_y; j < end_y; j++) {
      this->image_->draw_pixel_(i, j, color);
    }
  }
}

void ImageDecoder::draw_rgb565_block(int x, int y, int w, int h, const uint8_t *data) {
  int bpp_bytes = this->image_->get_bpp() / 8;
  bool no_transform = (this->x_scale_ == 1.0 && this->y_scale_ == 1.0 &&
                        this->x_offset_ == 0 && this->y_offset_ == 0);

  // Most ESPFrame images end up as RGB565. When no scaling is needed, copy whole
  // row spans directly instead of converting pixel by pixel.
  if (no_transform && bpp_bytes == 2) {
    for (int row = 0; row < h; row++) {
      int dy = y + row;
      if (dy < 0 || dy >= this->image_->buffer_height_)
        continue;
      int start_x = std::max(0, x);
      int end_x = std::min(x + w, this->image_->buffer_width_);
      if (start_x >= end_x)
        continue;
      int copy_w = end_x - start_x;
      int src_offset = (row * w + (start_x - x)) * 2;
      int dst_pos = this->image_->get_position_(start_x, dy);
      memcpy(this->image_->buffer_ + dst_pos, data + src_offset, copy_w * 2);
    }
    return;
  }

  for (int row = 0; row < h; row++) {
    int src_y = y + row;
    int dst_y = static_cast<int>(src_y * this->y_scale_) + this->y_offset_;
    if (dst_y < 0 || dst_y >= this->image_->buffer_height_)
      continue;

    int dst_x_start = std::max(0, this->x_offset_);
    int dst_x_end = std::min(this->x_offset_ + this->scaled_width_, this->image_->buffer_width_);

    for (int dst_x = dst_x_start; dst_x < dst_x_end; dst_x++) {
      int src_col = this->src_x_lut_[dst_x - this->x_offset_];
      int src_offset = (row * w + src_col) * 2;
      int dst_pos = this->image_->get_position_(dst_x, dst_y);
      memcpy(this->image_->buffer_ + dst_pos, data + src_offset, 2);
      if (bpp_bytes > 2) {
        this->image_->buffer_[dst_pos + 2] = 0xFF;
      }
    }
  }
}

void ImageDecoder::draw_rgb888_scaled(int src_y, int src_w, const uint8_t *rgb888, bool big_endian) {
  int dst_y = static_cast<int>(src_y * this->y_scale_) + this->y_offset_;
  if (dst_y < 0 || dst_y >= this->image_->buffer_height_)
    return;

  int dst_x_start = std::max(0, this->x_offset_);
  int dst_x_end = std::min(this->x_offset_ + this->scaled_width_, this->image_->buffer_width_);

  for (int dst_x = dst_x_start; dst_x < dst_x_end; dst_x++) {
    int src_col = this->src_x_lut_[dst_x - this->x_offset_];
    int si = src_col * 3;
    uint8_t r = rgb888[si], g = rgb888[si + 1], b = rgb888[si + 2];
    uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    int dst_pos = this->image_->get_position_(dst_x, dst_y);
    if (big_endian) {
      this->image_->buffer_[dst_pos] = rgb565 >> 8;
      this->image_->buffer_[dst_pos + 1] = rgb565 & 0xFF;
    } else {
      this->image_->buffer_[dst_pos] = rgb565 & 0xFF;
      this->image_->buffer_[dst_pos + 1] = rgb565 >> 8;
    }
  }
}

void ImageDecoder::fill_row_gap(int gap_start, int gap_end, int src_row_y) {
  int buf_h = this->image_->buffer_height_;
  int buf_w = this->image_->buffer_width_;
  gap_start = std::max(gap_start, 0);
  gap_end = std::min(gap_end, buf_h);
  if (gap_end <= gap_start) return;
  if (src_row_y < 0 || src_row_y >= buf_h) return;

  // Nearest-neighbor upscaling can skip destination rows. Duplicate the nearest
  // completed row to avoid thin black gaps between scaled scanlines.
  int row_bytes = buf_w * (this->image_->get_bpp() / 8);
  uint8_t *src_row = this->image_->buffer_ + src_row_y * row_bytes;
  for (int fy = gap_start; fy < gap_end; fy++) {
    memcpy(this->image_->buffer_ + fy * row_bytes, src_row, row_bytes);
  }
}

DownloadBuffer::DownloadBuffer(size_t size) : size_(size) {
  this->buffer_ = this->allocator_.allocate(size);
  this->reset();
  if (!this->buffer_) {
    ESP_LOGE(TAG, "Initial allocation of download buffer failed!");
    this->size_ = 0;
  }
}

uint8_t *DownloadBuffer::data(size_t offset) {
  if (offset > this->size_) {
    ESP_LOGE(TAG, "Tried to access beyond download buffer bounds!!!");
    return this->buffer_;
  }
  return this->buffer_ + offset;
}

size_t DownloadBuffer::read(size_t len) {
  if (len > this->unread_) {
    ESP_LOGE(TAG, "DownloadBuffer::read(%zu) exceeds unread %zu, clamping", len, this->unread_);
    len = this->unread_;
  }
  // Decoders may consume only part of the buffered network data. Compact the
  // unread tail so the next HTTP read can append directly after it.
  this->unread_ -= len;
  if (this->unread_ > 0) {
    memmove(this->data(), this->data(len), this->unread_);
  }
  return this->unread_;
}

size_t DownloadBuffer::resize(size_t size) {
  if (this->size_ >= size) {
    return this->size_;
  }
  auto *new_buffer = this->allocator_.allocate(size);
  if (new_buffer) {
    if (this->buffer_ && this->unread_ > 0) {
      memcpy(new_buffer, this->buffer_, this->unread_);
    }
    if (this->buffer_) {
      this->allocator_.deallocate(this->buffer_, this->size_);
    }
    this->buffer_ = new_buffer;
    this->size_ = size;
    return size;
  } else {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", size,
             this->allocator_.get_max_free_block_size());
    return this->size_;
  }
}

void DownloadBuffer::shrink(size_t max_size) {
  if (this->size_ <= max_size) return;
  // Some decoders temporarily grow the download buffer to hold a whole file.
  // Shrink after the transfer so future images do not pin that larger block.
  auto *new_buffer = this->allocator_.allocate(max_size);
  if (!new_buffer) {
    ESP_LOGW(TAG, "shrink allocation failed, keeping existing %zu-byte buffer", this->size_);
    return;
  }
  this->allocator_.deallocate(this->buffer_, this->size_);
  this->buffer_ = new_buffer;
  this->size_ = max_size;
  this->reset();
}

}  // namespace remote_image
}  // namespace esphome
