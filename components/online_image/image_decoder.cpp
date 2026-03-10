#include "image_decoder.h"
#include "online_image.h"

#include "esphome/core/log.h"

namespace esphome {
namespace online_image {

static const char *const TAG = "online_image.decoder";

bool ImageDecoder::set_size(int width, int height) {
  bool success = this->image_->resize_(width, height) > 0;

  int buf_w = this->image_->buffer_width_;
  int buf_h = this->image_->buffer_height_;

  if (buf_w == width && buf_h == height) {
    this->x_scale_ = 1.0;
    this->y_scale_ = 1.0;
    this->x_offset_ = 0;
    this->y_offset_ = 0;
    this->scaled_width_ = width;
    this->scaled_height_ = height;
  } else {
    double scale = std::min(
      static_cast<double>(buf_w) / width,
      static_cast<double>(buf_h) / height
    );
    this->x_scale_ = scale;
    this->y_scale_ = scale;
    this->scaled_width_ = static_cast<int>(width * scale);
    this->scaled_height_ = static_cast<int>(height * scale);
    this->x_offset_ = (buf_w - this->scaled_width_) / 2;
    this->y_offset_ = (buf_h - this->scaled_height_) / 2;
  }

  if (success) {
    memset(this->image_->buffer_, 0, this->image_->get_buffer_size_());
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

  double inv_scale = (this->x_scale_ > 0) ? 1.0 / this->x_scale_ : 1.0;

  for (int row = 0; row < h; row++) {
    int src_y = y + row;
    int dst_y = static_cast<int>(src_y * this->y_scale_) + this->y_offset_;
    if (dst_y < 0 || dst_y >= this->image_->buffer_height_)
      continue;

    int dst_x_start = std::max(0, this->x_offset_);
    int dst_x_end = std::min(this->x_offset_ + this->scaled_width_, this->image_->buffer_width_);

    for (int dst_x = dst_x_start; dst_x < dst_x_end; dst_x++) {
      int src_col = static_cast<int>((dst_x - this->x_offset_) * inv_scale);
      if (src_col >= w) src_col = w - 1;
      int src_offset = (row * w + src_col) * 2;
      int dst_pos = this->image_->get_position_(dst_x, dst_y);
      memcpy(this->image_->buffer_ + dst_pos, data + src_offset, 2);
      if (bpp_bytes > 2) {
        this->image_->buffer_[dst_pos + 2] = 0xFF;
      }
    }
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
  this->unread_ -= len;
  if (this->unread_ > 0) {
    memmove(this->data(), this->data(len), this->unread_);
  }
  return this->unread_;
}

size_t DownloadBuffer::resize(size_t size) {
  if (this->size_ >= size) {
    // Avoid useless reallocations; if the buffer is big enough, don't reallocate.
    return this->size_;
  }
  this->allocator_.deallocate(this->buffer_, this->size_);
  this->buffer_ = this->allocator_.allocate(size);
  this->reset();
  if (this->buffer_) {
    this->size_ = size;
    return size;
  } else {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", size,
             this->allocator_.get_max_free_block_size());
    this->size_ = 0;
    return 0;
  }
}

}  // namespace online_image
}  // namespace esphome
