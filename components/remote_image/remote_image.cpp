#include "remote_image.h"

#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// Owns the runtime side of the remote_image component: HTTP fetching, cache
// validation, decoder selection, and exposing the decoded pixel buffer as an
// ESPHome Image. The format-specific decoder files only turn bytes into pixels;
// this file coordinates when those bytes are fetched and when the final image is
// safe to display.
static const char *const TAG = "remote_image";
static const char *const ETAG_HEADER_NAME = "etag";
static const char *const IF_NONE_MATCH_HEADER_NAME = "if-none-match";
static const char *const LAST_MODIFIED_HEADER_NAME = "last-modified";
static const char *const IF_MODIFIED_SINCE_HEADER_NAME = "if-modified-since";
static const char *const CONTENT_TYPE_HEADER_NAME = "content-type";

#include "image_decoder.h"

#ifdef USE_REMOTE_IMAGE_BMP_SUPPORT
#include "bmp_image.h"
#endif
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT
#include "jpeg_image.h"
#endif
#ifdef USE_REMOTE_IMAGE_PNG_SUPPORT
#include "png_image.h"
#endif
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT
#include "webp_image.h"
#endif

namespace esphome {
namespace remote_image {

using image::ImageType;

#ifdef USE_LVGL
template<typename T> auto refresh_lv_image_descriptor_(T *obj, int) -> decltype(obj->get_lv_image_dsc(), void()) {
  obj->get_lv_image_dsc();
}

template<typename T> auto refresh_lv_image_descriptor_(T *obj, long) -> decltype(obj->get_lv_img_dsc(), void()) {
  obj->get_lv_img_dsc();
}
#endif

inline bool is_color_on(const Color &color) {
  // This produces the most accurate monochrome conversion, but is slightly slower.
  //  return (0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b) > 127;

  // Approximation using fast integer computations; produces acceptable results
  // Equivalent to 0.25 * R + 0.5 * G + 0.25 * B
  return ((color.r >> 2) + (color.g >> 1) + (color.b >> 2)) & 0x80;
}

OnlineImage::OnlineImage(const std::string &url, int width, int height, ImageFormat format, ImageType type,
                         image::Transparency transparency, uint32_t download_buffer_size, bool is_big_endian)
    : Image(nullptr, 0, 0, type, transparency),
      buffer_(nullptr),
      download_buffer_(download_buffer_size),
      download_buffer_initial_size_(download_buffer_size),
      format_(format),
      fixed_width_(width),
      fixed_height_(height),
      is_big_endian_(is_big_endian) {
  this->set_url(url);
}

void OnlineImage::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (this->data_start_) {
    Image::draw(x, y, display, color_on, color_off);
  } else if (this->placeholder_) {
    this->placeholder_->draw(x, y, display, color_on, color_off);
  }
}

void OnlineImage::release() {
  if (this->buffer_) {
    ESP_LOGV(TAG, "Deallocating old buffer");
    this->allocator_.deallocate(this->buffer_, this->get_buffer_size_());
    this->data_start_ = nullptr;
    this->buffer_ = nullptr;
    this->width_ = 0;
    this->height_ = 0;
    this->buffer_width_ = 0;
    this->buffer_height_ = 0;
#ifdef USE_LVGL
    memset(&this->dsc_, 0, sizeof(this->dsc_));
#endif
    this->last_modified_ = "";
    this->etag_ = "";
    this->end_connection_();
  }
}

void OnlineImage::abort_download() {
  if (this->decoder_ || this->downloader_) {
    ESP_LOGW(TAG, "Aborting in-progress download");
    this->end_connection_();
  }
}

size_t OnlineImage::resize_(int width_in, int height_in) {
  int width = this->fixed_width_;
  int height = this->fixed_height_;
  if (this->is_auto_resize_()) {
    width = width_in;
    height = height_in;
    if (this->width_ != width || this->height_ != height) {
      this->release();
    }
  } else {
    // Fixed dimensions: always use full target size for the buffer so
    // the stride never changes between images (avoids LVGL descriptor
    // cache mismatches).  Centering/scaling is handled in the decoder.
  }
  size_t new_size = this->get_buffer_size_(width, height);
  if (this->buffer_) {
    if (new_size <= this->get_buffer_size_()) {
      memset(this->buffer_, 0, this->get_buffer_size_());
      this->buffer_width_ = width;
      this->buffer_height_ = height;
      this->width_ = width;
      return new_size;
    }
    this->allocator_.deallocate(this->buffer_, this->get_buffer_size_());
    this->buffer_ = nullptr;
    this->data_start_ = nullptr;
#ifdef USE_LVGL
    memset(&this->dsc_, 0, sizeof(this->dsc_));
#endif
  }
  ESP_LOGD(TAG, "Allocating new buffer of %zu bytes", new_size);
  this->buffer_ = this->allocator_.allocate(new_size);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", new_size,
             this->allocator_.get_max_free_block_size());
    this->end_connection_();
    return 0;
  }
  this->buffer_width_ = width;
  this->buffer_height_ = height;
  this->width_ = width;
#ifdef USE_LVGL
  memset(&this->dsc_, 0, sizeof(this->dsc_));
#endif
  ESP_LOGV(TAG, "New size: (%d, %d)", width, height);
  return new_size;
}

void OnlineImage::update() {
  if (this->decoder_ || this->downloader_) {
    ESP_LOGW(TAG, "Cancelling in-progress image download to fetch new URL");
    this->end_connection_();
  }
  ESP_LOGI(TAG, "Updating image %s", this->url_.c_str());

  std::vector<http_request::Header> headers = {};

  // Tell servers the preferred image type while still allowing a generic image
  // response. This helps Immich and other services pick a sensible thumbnail.
  http_request::Header accept_header;
  accept_header.name = "Accept";
  std::string accept_mime_type;
  switch (this->format_) {
#ifdef USE_REMOTE_IMAGE_BMP_SUPPORT
    case ImageFormat::BMP:
      accept_mime_type = "image/bmp";
      break;
#endif  // USE_REMOTE_IMAGE_BMP_SUPPORT
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT
    case ImageFormat::JPEG:
      accept_mime_type = "image/jpeg";
      break;
#endif  // USE_REMOTE_IMAGE_JPEG_SUPPORT
#ifdef USE_REMOTE_IMAGE_PNG_SUPPORT
    case ImageFormat::PNG:
      accept_mime_type = "image/png";
      break;
#endif  // USE_REMOTE_IMAGE_PNG_SUPPORT
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT
    case ImageFormat::WEBP:
      accept_mime_type = "image/webp";
      break;
#endif  // USE_REMOTE_IMAGE_WEBP_SUPPORT
    default:
      accept_mime_type = "image/*";
  }
  accept_header.value = accept_mime_type + ",*/*;q=0.8";

  // Reuse HTTP cache validators when available so unchanged photos do not waste
  // bandwidth or decoder memory on small ESP32 devices.
  if (!this->etag_.empty()) {
    headers.push_back(http_request::Header{IF_NONE_MATCH_HEADER_NAME, this->etag_});
  }

  if (!this->last_modified_.empty()) {
    headers.push_back(http_request::Header{IF_MODIFIED_SINCE_HEADER_NAME, this->last_modified_});
  }

  headers.push_back(accept_header);

  for (auto &header : this->request_headers_) {
    headers.push_back(http_request::Header{header.first, header.second.value()});
  }

  this->downloader_ = this->parent_->get(this->url_, headers, {ETAG_HEADER_NAME, LAST_MODIFIED_HEADER_NAME, CONTENT_TYPE_HEADER_NAME});

  if (this->downloader_ == nullptr) {
    ESP_LOGE(TAG, "Download failed.");
    this->end_connection_();
    this->download_error_callback_.call();
    return;
  }

  int http_code = this->downloader_->status_code;
  if (http_code == HTTP_CODE_NOT_MODIFIED) {
    // Image hasn't changed on server. Skip download.
    ESP_LOGI(TAG, "Server returned HTTP 304 (Not Modified). Download skipped.");
    this->end_connection_();
    this->download_finished_callback_.call(true);
    return;
  }
  if (http_code != HTTP_CODE_OK) {
    ESP_LOGE(TAG, "HTTP result: %d", http_code);
    this->end_connection_();
    this->download_error_callback_.call();
    return;
  }

  ESP_LOGD(TAG, "Starting download");
  size_t total_size = this->downloader_->content_length;

  ImageFormat resolved = this->detect_format_();

  if (resolved == ImageFormat::AUTO) {
    // Some servers omit or mislabel Content-Type. In that case the loop buffers
    // the first bytes of the file and detect_format_ checks the file signature.
    ESP_LOGD(TAG, "Image format not identified from Content-Type, deferring to magic-byte detection");
    this->data_start_ = nullptr;
    this->start_time_ = ::time(nullptr);
    this->last_progress_millis_ = millis();
    this->enable_loop();
    return;
  }

  this->decoder_ = this->create_decoder_for_format_(resolved);

  if (!this->decoder_) {
    ESP_LOGE(TAG, "Could not instantiate decoder. Image format unsupported: %d", resolved);
    this->end_connection_();
    this->download_error_callback_.call();
    return;
  }
  auto prepare_result = this->decoder_->prepare(total_size);
  if (prepare_result < 0) {
    this->end_connection_();
    this->download_error_callback_.call();
    return;
  }
  this->data_start_ = nullptr;
  this->enable_loop();
  ESP_LOGI(TAG, "Downloading image (Size: %zu)", total_size);
  this->start_time_ = ::time(nullptr);
  this->last_progress_millis_ = millis();
}

void OnlineImage::loop() {
  bool made_progress = false;
  if (!this->decoder_) {
    if (!this->downloader_) {
      this->disable_loop();
      return;
    }
    // AUTO format detection: buffer data until we can identify the format.
    // Waiting for 12 bytes is enough for JPEG, PNG, BMP, and WebP signatures.
    size_t available = this->download_buffer_.free_capacity();
    if (available) {
      available = std::min(available, this->download_buffer_initial_size_);
      auto len = this->downloader_->read(this->download_buffer_.append(), available);
      if (len > 0) {
        this->download_buffer_.write(len);
        made_progress = true;
      } else if (len < 0) {
        this->fail_download_("HTTP read failed while detecting image format", len);
        return;
      }
    }
    if (this->download_buffer_.unread() < 12) {
      if (!made_progress && (millis() - this->last_progress_millis_) > DOWNLOAD_STALL_TIMEOUT_MS) {
        this->fail_download_("Download stalled while detecting image format", http_request::HTTP_ERROR_CONNECTION_CLOSED);
      } else if (made_progress) {
        this->last_progress_millis_ = millis();
      }
      return;
    }
    if (made_progress) {
      this->last_progress_millis_ = millis();
    }
    auto detected = this->detect_format_();
    if (detected == ImageFormat::AUTO) {
      ESP_LOGE(TAG, "Could not determine image format from headers or file content");
      this->end_connection_();
      this->download_error_callback_.call();
      return;
    }
    this->decoder_ = this->create_decoder_for_format_(detected);
    if (!this->decoder_) {
      ESP_LOGE(TAG, "No compiled decoder for detected format %d", detected);
      this->end_connection_();
      this->download_error_callback_.call();
      return;
    }
    auto prepare_result = this->decoder_->prepare(this->downloader_->content_length);
    if (prepare_result < 0) {
      this->end_connection_();
      this->download_error_callback_.call();
      return;
    }
    return;
  }
  if (!this->downloader_) {
    this->fail_download_("Downloader disappeared mid-transfer", http_request::HTTP_ERROR_CONNECTION_CLOSED);
    return;
  }
  if (this->decoder_->is_finished()) {
    // Only publish width_/height_ after decode is complete. ESPHome display
    // code treats non-zero dimensions as drawable, so this prevents partially
    // decoded images from flashing on screen.
    this->data_start_ = buffer_;
    this->width_ = buffer_width_;
    this->height_ = buffer_height_;
    ESP_LOGD(TAG, "Image fully downloaded, read %zu bytes, width/height = %d/%d", this->downloader_->get_bytes_read(),
             this->width_, this->height_);
    ESP_LOGD(TAG, "Total time: %" PRIu32 "s", (uint32_t) (::time(nullptr) - this->start_time_));
#ifdef USE_LVGL
    this->dsc_.data = nullptr;
    refresh_lv_image_descriptor_(this, 0);
#endif
    this->etag_ = this->downloader_->get_response_header(ETAG_HEADER_NAME);
    this->last_modified_ = this->downloader_->get_response_header(LAST_MODIFIED_HEADER_NAME);
    this->download_finished_callback_.call(false);
    this->end_connection_();
    return;
  }
  // Download phase: pull data from the HTTP connection
  size_t available = this->download_buffer_.free_capacity();
  if (available) {
    available = std::min(available, this->download_buffer_initial_size_);
    auto len = this->downloader_->read(this->download_buffer_.append(), available);
    if (len > 0) {
      this->download_buffer_.write(len);
      made_progress = true;
    } else if (len < 0) {
      this->fail_download_("HTTP read failed while downloading image", len);
      return;
    }
  }

  // Decode phase: always attempt decode when data is buffered,
  // so chunked decoders get called every loop iteration.
  if (this->download_buffer_.unread() > 0) {
    auto fed = this->decoder_->decode(this->download_buffer_.data(), this->download_buffer_.unread());
    if (fed < 0) {
      ESP_LOGE(TAG, "Error when decoding image.");
      this->end_connection_();
      this->download_error_callback_.call();
      return;
    }
    if (fed > 0) {
      this->download_buffer_.read(fed);
      made_progress = true;
    }
  }
  if (made_progress) {
    this->last_progress_millis_ = millis();
  } else if ((millis() - this->last_progress_millis_) > DOWNLOAD_STALL_TIMEOUT_MS) {
    this->fail_download_("Download stalled while downloading image", http_request::HTTP_ERROR_CONNECTION_CLOSED);
  }
}

void OnlineImage::map_chroma_key(Color &color) {
  if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
    // ESPHome reserves a near-black green value as the transparency key. Move
    // real pixels away from that value, then map translucent input pixels to it.
    if (color.g == 1 && color.r == 0 && color.b == 0) {
      color.g = 0;
    }
    if (color.w < 0x80) {
      color.r = 0;
      color.g = this->type_ == ImageType::IMAGE_TYPE_RGB565 ? 4 : 1;
      color.b = 0;
    }
  }
}

void OnlineImage::draw_pixel_(int x, int y, Color color) {
  if (!this->buffer_) {
    ESP_LOGE(TAG, "Buffer not allocated!");
    return;
  }
  if (x < 0 || y < 0 || x >= this->buffer_width_ || y >= this->buffer_height_) {
    ESP_LOGE(TAG, "Tried to paint a pixel (%d,%d) outside the image!", x, y);
    return;
  }
  uint32_t pos = this->get_position_(x, y);
  switch (this->type_) {
    case ImageType::IMAGE_TYPE_BINARY: {
      const uint32_t width_8 = ((this->width_ + 7u) / 8u) * 8u;
      pos = x + y * width_8;
      auto bitno = 0x80 >> (pos % 8u);
      pos /= 8u;
      auto on = is_color_on(color);
      if (this->has_transparency() && color.w < 0x80)
        on = false;
      if (on) {
        this->buffer_[pos] |= bitno;
      } else {
        this->buffer_[pos] &= ~bitno;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_GRAYSCALE: {
      auto gray = static_cast<uint8_t>(0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b);
      if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
        if (gray == 1) {
          gray = 0;
        }
        if (color.w < 0x80) {
          gray = 1;
        }
      } else if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        if (color.w != 0xFF)
          gray = color.w;
      }
      this->buffer_[pos] = gray;
      break;
    }
    case ImageType::IMAGE_TYPE_RGB565: {
      // The Guition display stores RGB565 little-endian, while other targets
      // may expect big-endian. Keep the byte-order decision here so decoders can
      // write normal colors without knowing the display wiring.
      this->map_chroma_key(color);
      uint16_t col565 = display::ColorUtil::color_to_565(color);
      if (this->is_big_endian_) {
        this->buffer_[pos + 0] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
        this->buffer_[pos + 1] = static_cast<uint8_t>(col565 & 0xFF);
      } else {
        this->buffer_[pos + 0] = static_cast<uint8_t>(col565 & 0xFF);
        this->buffer_[pos + 1] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
      }
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->buffer_[pos + 2] = color.w;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_RGB: {
      this->map_chroma_key(color);
      this->buffer_[pos + 0] = color.r;
      this->buffer_[pos + 1] = color.g;
      this->buffer_[pos + 2] = color.b;
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->buffer_[pos + 3] = color.w;
      }
      break;
    }
  }
}

ImageFormat OnlineImage::detect_format_() {
  if (this->format_ != ImageFormat::AUTO) {
    return this->format_;
  }

  // Prefer the HTTP header because it is available before the body, then fall
  // back to magic bytes for servers that serve images as application/octet-stream
  // or omit Content-Type entirely.
  if (this->downloader_) {
    std::string ct = this->downloader_->get_response_header(CONTENT_TYPE_HEADER_NAME);
    if (ct.find("image/jpeg") != std::string::npos || ct.find("image/jpg") != std::string::npos) {
      ESP_LOGD(TAG, "Detected JPEG from Content-Type: %s", ct.c_str());
      return ImageFormat::JPEG;
    }
    if (ct.find("image/png") != std::string::npos) {
      ESP_LOGD(TAG, "Detected PNG from Content-Type: %s", ct.c_str());
      return ImageFormat::PNG;
    }
    if (ct.find("image/bmp") != std::string::npos) {
      ESP_LOGD(TAG, "Detected BMP from Content-Type: %s", ct.c_str());
      return ImageFormat::BMP;
    }
    if (ct.find("image/webp") != std::string::npos) {
      ESP_LOGD(TAG, "Detected WebP from Content-Type: %s", ct.c_str());
      return ImageFormat::WEBP;
    }
  }

  if (this->download_buffer_.unread() >= 12) {
    const uint8_t *data = this->download_buffer_.data();
    if (data[0] == 0xFF && data[1] == 0xD8) {
      ESP_LOGD(TAG, "Detected JPEG from magic bytes");
      return ImageFormat::JPEG;
    }
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
      ESP_LOGD(TAG, "Detected PNG from magic bytes");
      return ImageFormat::PNG;
    }
    if (data[0] == 0x42 && data[1] == 0x4D) {
      ESP_LOGD(TAG, "Detected BMP from magic bytes");
      return ImageFormat::BMP;
    }
    if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
        data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
      ESP_LOGD(TAG, "Detected WebP from magic bytes");
      return ImageFormat::WEBP;
    }
  }

  return ImageFormat::AUTO;
}

std::unique_ptr<ImageDecoder> OnlineImage::create_decoder_for_format_(ImageFormat format) {
  switch (format) {
#ifdef USE_REMOTE_IMAGE_BMP_SUPPORT
    case ImageFormat::BMP:
      ESP_LOGD(TAG, "Allocating BMP decoder");
      return make_unique<BmpDecoder>(this);
#endif
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT
    case ImageFormat::JPEG:
      ESP_LOGD(TAG, "Allocating JPEG decoder");
      return esphome::make_unique<JpegDecoder>(this);
#endif
#ifdef USE_REMOTE_IMAGE_PNG_SUPPORT
    case ImageFormat::PNG:
      ESP_LOGD(TAG, "Allocating PNG decoder");
      return make_unique<PngDecoder>(this);
#endif
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT
    case ImageFormat::WEBP:
      ESP_LOGD(TAG, "Allocating WebP decoder");
      return make_unique<WebpDecoder>(this);
#endif
    default:
      return nullptr;
  }
}

void OnlineImage::end_connection_() {
  // End every transfer through one path so decoder state, buffered unread bytes,
  // and temporary buffer growth are all reset together.
  if (this->downloader_) {
    this->downloader_->end();
    this->downloader_ = nullptr;
  }
  this->decoder_.reset();
  this->download_buffer_.reset();
  this->download_buffer_.shrink(this->download_buffer_initial_size_);
}

void OnlineImage::fail_download_(const char *reason, int error_code) {
  ESP_LOGW(TAG, "%s (%d)", reason, error_code);
  this->end_connection_();
  this->download_error_callback_.call();
}

bool OnlineImage::validate_url_(const std::string &url) {
  if ((url.length() < 8) || !url.starts_with("http") || (url.find("://") == std::string::npos)) {
    ESP_LOGE(TAG, "URL is invalid and/or must be prefixed with 'http://' or 'https://'");
    return false;
  }
  return true;
}

void OnlineImage::add_on_finished_callback(std::function<void(bool)> &&callback) {
  this->download_finished_callback_.add(std::move(callback));
}

void OnlineImage::add_on_error_callback(std::function<void()> &&callback) {
  this->download_error_callback_.add(std::move(callback));
}

}  // namespace remote_image
}  // namespace esphome
