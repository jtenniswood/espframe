#include "photo_controls.h"

#include <cmath>
#include <cstdlib>

namespace espframe::native {
namespace {

constexpr uint16_t background = 0x10A3;
constexpr uint16_t button = 0x2946;
constexpr uint16_t icon = 0xD6DC;

int lerp(int from, int to, int progress) {
  return from + ((to - from) * progress + PhotoControls::open / 2) / PhotoControls::open;
}

uint16_t tint(uint16_t color, int progress) {
  const auto red = lerp(background >> 11, color >> 11, progress);
  const auto green = lerp((background >> 5) & 63, (color >> 5) & 63, progress);
  const auto blue = lerp(background & 31, color & 31, progress);
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

template<size_t Size>
void corner_insets(std::array<int, Size> &insets, int r) {
  for (int i = 0; i < r; ++i) {
    const float distance = r - i - 0.5f;
    insets[i] = r - static_cast<int>(std::sqrt(r * r - distance * distance) + 0.5f);
  }
}

void paint_toolbar(uint16_t *output, ScreenRotation rotation, int progress) {
  constexpr int reveal = 160;
  if (progress <= reveal)
    return;
  const int opacity = (progress - reveal) * PhotoControls::open / (PhotoControls::open - reveal);
  const auto fill = tint(button, opacity);
  const auto ink = tint(icon, opacity);
  const int y = lerp(39, 52, opacity);
  const Viewport viewport{rotation};
  std::array<int, 16> corners;
  corner_insets(corners, 16);
  const auto rectangle = [&](int left, int top, int width, int height, uint16_t color, int round = 0) {
    const int rows = viewport.portrait ? height : width;
    const int columns = viewport.portrait ? width : height;
    const int stride = viewport.portrait ? viewport.x_stride : viewport.y_stride;
    for (int i = 0; i < rows; ++i) {
      const int edge = std::min(i, rows - i - 1);
      const int inset = edge < round ? corners[edge] : 0;
      const int column = stride > 0 ? inset : columns - inset - 1;
      const int first = viewport.portrait ? viewport.offset(left + column, top + i)
                                          : viewport.offset(left + i, top + column);
      std::fill_n(output + first, columns - 2 * inset, color);
    }
  };
  for (int i = 0; i < 4; ++i) {
    const int x = viewport.width / 2 + (2 * i - 3) * 45;
    rectangle(x - 36, y - 24, 72, 48, fill, 16);
    const auto stroke = [&](int dx, int dy, int w, int h) {
      rectangle(x + dx, y + dy, w, h, ink);
    };
    switch (i) {
      case 0:  // Photo
        stroke(-12, -9, 24, 2);
        stroke(-12, 8, 24, 2);
        stroke(-12, -7, 2, 15);
        stroke(10, -7, 2, 15);
        stroke(4, -5, 3, 3);
        for (int dx = -8; dx < 1; ++dx)
          stroke(dx, 2 + std::abs(dx + 4), 1, 2);
        break;
      case 1:  // Framing
        for (int dx : {-12, 4}) {
          for (int dy : {-10, 8})
            stroke(dx, dy, 8, 2);
        }
        for (int dx : {-12, 10}) {
          stroke(dx, -8, 2, 5);
          stroke(dx, 3, 2, 5);
        }
        break;
      case 2:  // Clock
        for (int dx = -11; dx <= 11; ++dx) {
          for (int dy = -11; dy <= 11; ++dy) {
            const int distance = dx * dx + dy * dy;
            if (distance >= 90 && distance <= 127)
              stroke(dx, dy, 1, 1);
          }
        }
        stroke(-1, -6, 2, 7);
        stroke(0, -1, 5, 2);
        break;
      case 3:  // Settings
        stroke(-12, -7, 24, 2);
        stroke(-7, -10, 3, 8);
        stroke(-12, 5, 24, 2);
        stroke(4, 2, 3, 8);
        break;
    }
  }
}

// Sample around pixel centres so opposite orientations mirror the same image.
int source_coordinate(int pixel, int size, int source_size, ScreenRotation rotation) {
  const int logical = oriented_axis(pixel, size, rotation);
  return oriented_axis((2 * logical + 1) * source_size / (2 * size), source_size, rotation);
}

}  // namespace

int PhotoControls::progress(int64_t now) const {
  if (this->start_ == this->target_ || now - this->started_us_ >= this->duration_us_)
    return this->target_;
  if (now <= this->started_us_)
    return this->start_;
  const int elapsed = static_cast<int>((now - this->started_us_) * open / this->duration_us_);
  // Start and end at rest so the largest steps are in the middle of the move.
  const int eased = elapsed * elapsed * (3 * open - 2 * elapsed) / (open * open);
  return this->start_ + (this->target_ - this->start_) * eased / open;
}

int64_t PhotoControls::text_duration() const {
  return target_open() ? 50'000 * this->text_from_ / opaque
                       : 80'000 * (opaque - this->text_from_) / opaque;
}

uint8_t PhotoControls::text_opacity(int64_t now) const {
  const int target = target_open() ? 0 : opaque;
  const auto duration = this->text_duration();
  if (duration == 0 || now - this->text_started_us_ >= duration)
    return target;
  const auto elapsed = std::max<int64_t>(0, now - this->text_started_us_);
  return this->text_from_ + (target - this->text_from_) * elapsed / duration;
}

void PhotoControls::animate(bool show, int64_t now, bool text_visible) {
  constexpr int64_t full_duration_us = 200'000;
  constexpr int64_t minimum_duration_us = 87'500;
  const int target = show ? open : 0;
  if (target == this->target_)
    return;
  this->text_from_ = show && !text_visible ? 0 : this->text_opacity(now);
  this->start_ = this->progress(now);
  this->target_ = target;
  this->started_us_ = now;
  this->duration_us_ = minimum_duration_us +
                       (full_duration_us - minimum_duration_us) * std::abs(target - this->start_) / open;
  // Fade on the full-size photo on either side of the resize.
  this->text_started_us_ = now;
  if (show)
    this->started_us_ += this->text_duration();
  else if (this->start_ != 0)
    this->text_started_us_ += this->duration_us_;
}

void PhotoControls::prepare(uint16_t *output, SlideSpan span, int pull, ScreenRotation rotation, int progress) {
  this->output_ = output;
  this->span_ = span;
  this->pull_ = pull;
  this->rotation_ = rotation;
  this->progress_ = progress;
  const Viewport viewport{rotation};
  const int left = lerp(0, 104 * viewport.width / slide_width, progress);
  const int width = viewport.width - 2 * left;
  const int removed_height = lerp(0, 130 * viewport.height / slide_height, progress);
  const int height = viewport.height - removed_height;
  const int top = removed_height * 4 / 5;
  const int first = viewport.offset(left + (viewport.x_stride < 0 ? width - 1 : 0),
                                    top + (viewport.y_stride < 0 ? height - 1 : 0));
  this->first_row_ = first / panel_width;
  this->first_column_ = first % panel_width;
  this->rows_ = viewport.portrait ? height : width;
  this->columns_ = viewport.portrait ? width : height;
  this->rounding_ = lerp(0, radius, progress);
  corner_insets(this->corners_, this->rounding_);
  this->source_split_ = this->columns_;
  for (int column = 0; column < this->columns_; ++column) {
    int source = source_coordinate(column, this->columns_, panel_width, rotation);
    if (viewport.portrait)
      source = std::clamp(source + span.offset + pull, 0, panel_width + span.offset - 1);
    if (source >= panel_width) {
      this->source_split_ = std::min(this->source_split_, column);
      source -= panel_width;
    }
    this->source_columns_[column] = source;
  }
}

void PhotoControls::draw_rows(int first, int end) const {
  const bool portrait = portrait_screen(this->rotation_);
  const int first_card = std::clamp(this->first_row_, first, end);
  const int end_card = std::clamp(this->first_row_ + this->rows_, first, end);
  std::fill_n(this->output_ + first * panel_width, (first_card - first) * panel_width, background);
  for (int row = first_card; row < end_card; ++row) {
    auto *out = this->output_ + row * panel_width;
    const int card_row = row - this->first_row_;
    int mapped = source_coordinate(card_row, this->rows_, panel_height, this->rotation_);
    if (!portrait)
      mapped = std::clamp(mapped + this->span_.offset + this->pull_, 0, panel_height + this->span_.offset - 1);
    const auto *source = mapped < panel_height ? this->span_.first + mapped * panel_width
                                               : this->span_.second + (mapped - panel_height) * panel_width;
    const int edge = std::min(card_row, this->rows_ - card_row - 1);
    const int inset = edge < this->rounding_ ? this->corners_[edge] : 0;
    std::fill_n(out, this->first_column_ + inset, background);
    auto *card = out + this->first_column_;
    const int last = this->columns_ - inset;
    if (this->columns_ == panel_width && (!portrait || (this->span_.offset == 0 && this->pull_ == 0))) {
      copy_slide_rows(card, source, panel_width, 1);
    } else {
      const auto sample = [&](const uint16_t *pixels, int column, int stop) {
        for (; column + 4 <= stop; column += 4) {
          card[column] = pixels[this->source_columns_[column]];
          card[column + 1] = pixels[this->source_columns_[column + 1]];
          card[column + 2] = pixels[this->source_columns_[column + 2]];
          card[column + 3] = pixels[this->source_columns_[column + 3]];
        }
        for (; column < stop; ++column)
          card[column] = pixels[this->source_columns_[column]];
      };
      const int split = std::clamp(this->source_split_, inset, last);
      sample(source, inset, split);
      if (split < last)
        sample(this->span_.second + mapped * panel_width, split, last);
    }
    std::fill_n(card + last, panel_width - this->first_column_ - last, background);
  }
  std::fill_n(this->output_ + end_card * panel_width, (end - end_card) * panel_width, background);
}

void PhotoControls::draw_toolbar() const {
  paint_toolbar(this->output_, this->rotation_, this->progress_);
}

}  // namespace espframe::native
