# remote_image component

ESPHome component that downloads images from HTTP/HTTPS URLs and decodes them for display. Supports JPEG, PNG, BMP, and WebP. Images are cached in a buffer so they can be redrawn without re-downloading. Optional placeholder image and ETag/Last-Modified handling reduce redundant transfers.

## Requirements

- **ESPHome** with:
  - **display** component
  - **http_request** component
- Supported frameworks: ESP32 (Arduino/IDF), RP2040, host. ESP8266 is not supported.
- For **JPEG**: the build uses the bundled `libjpeg-turbo-esp32` (no extra config).
- For **PNG**: the component depends on the `pngle` library (v1.1.0).
- For **WebP**: the build uses the bundled `libwebp-esp32` (decode-only, v1.6.0).

## Files

| File | Purpose |
|------|--------|
| `__init__.py` | Component registration, config schema, actions, triggers, and format selection (BMP/JPEG/PNG/WebP). |
| `remote_image.h` | `OnlineImage` class (main entity), `ImageFormat` enum, actions and triggers. |
| `remote_image.cpp` | Download loop, HTTP handling, decoder lifecycle, buffer and pixel drawing. |
| `image_decoder.h` | `ImageDecoder` base class and `DownloadBuffer` for streaming decode. |
| `image_decoder.cpp` | Scaling/positioning, `draw`, `draw_rgb565_block`, `draw_rgb888_scaled`, `fill_row_gap`, buffer logic. |
| `jpeg_image.h` / `jpeg_image.cpp` | JPEG decoder (when `USE_REMOTE_IMAGE_JPEG_SUPPORT` is set). |
| `png_image.h` / `png_image.cpp` | PNG decoder (when `USE_REMOTE_IMAGE_PNG_SUPPORT` is set). |
| `bmp_image.h` / `bmp_image.cpp` | BMP decoder (when `USE_REMOTE_IMAGE_BMP_SUPPORT` is set). |
| `webp_image.h` / `webp_image.cpp` | WebP decoder (when `USE_REMOTE_IMAGE_WEBP_SUPPORT` is set). |

---

## YAML configuration

### Basic

```yaml
# Required: http_request component (e.g. with id: http_request_id)
http_request:

display:
  - platform: ...
    # ...

remote_image:
  - id: my_remote_image
    url: "https://example.com/image.jpg"
    format: JPEG
    http_request_id: http_request_id
```

### Full options

| Option | Type | Default | Description |
|--------|------|--------|-------------|
| `id` | required | — | ID for this `OnlineImage` (e.g. for actions or as image source). |
| `url` | required | — | URL to download (must use `http://` or `https://`). |
| `format` | required | — | `BMP`, `JPEG`, `PNG`, or `WEBP` (alias `JPG` for JPEG). |
| `http_request_id` | required | — | ID of the `http_request` component to use. |
| `request_headers` | optional | `{}` | Extra HTTP request headers (e.g. `x-api-key: !lambda 'return id(api_key).state;'`). |
| `placeholder` | optional | — | ID of another `image` to show until the download is ready. |
| `fill` | optional | `false` | If `true`, scale image to fill the target area (crop if needed); otherwise fit (letterbox). |
| `buffer_size` | optional | `65536` | Download buffer size in bytes (256–524288). |
| `resize` | optional | — | `[width, height]` to fix display size; omit for auto (image native size). |
| `type` | optional | — | Image type (e.g. RGB565, RGB, BINARY, GRAYSCALE) per ESPHome image schema. |
| `transparency` | optional | — | Transparency mode per ESPHome image schema. |
| `byte_order` | optional | — | Byte order for 16-bit (e.g. LITTLE_ENDIAN). |
| `on_download_finished` | optional | — | Automation when download finishes (receives `cached`: true if 304 Not Modified). |
| `on_error` | optional | — | Automation when download or decode fails. |

Example with resize, placeholder, and triggers:

```yaml
remote_image:
  - id: photo
    url: "https://example.com/photo.jpg"
    format: JPEG
    http_request_id: http_request_id
    resize: [800, 480]
    placeholder: id(placeholder_image)
    fill: true
    on_download_finished:
      - lambda: |-
          ESP_LOGI("main", "Image ready, cached=%s", cached ? "yes" : "no");
    on_error:
      - lambda: ESP_LOGW("main", "Image download or decode error");
```

---

## Actions

### remote_image.set_url

Set the URL to download and optionally trigger an update immediately.

```yaml
- remote_image.set_url:
    id: id(my_remote_image)
    url: "https://example.com/other.jpg"
    update: true   # optional, default true — call update() after setting URL
```

Use templatable values for dynamic URLs:

```yaml
- remote_image.set_url:
    id: id(photo)
    url: !lambda 'return id(immich_base_url).state + "/api/assets/" + id(asset_id).state + "/thumbnail?size=preview";'
    update: true
```

### remote_image.release

Release the decoded image buffer. The image will need to be downloaded again to be displayed. Useful to free memory when the image is no longer needed.

```yaml
- remote_image.release:
    id: id(my_remote_image)
```

---

## Triggers

### on_download_finished

Fires when a download completes successfully. Receives one argument:

- **cached** (`bool`): `true` if the server responded with 304 Not Modified (no new data); `false` if a new image was downloaded and decoded.

```yaml
on_download_finished:
  - lambda: |-
      if (cached) {
        ESP_LOGI("main", "Image unchanged");
      } else {
        ESP_LOGI("main", "New image decoded");
      }
```

### on_error

Fires when the HTTP request fails, returns a non-2xx/304 status, or the decoder reports an error.

```yaml
on_error:
  - lambda: id(some_fallback).perform();
```

---

## C++ API (for advanced use or extensions)

### ImageFormat enum (remote_image.h)

| Value | Description |
|-------|-------------|
| `AUTO` | Detect from MIME type (not implemented yet). |
| `JPEG` | JPEG format. |
| `PNG` | PNG format. |
| `BMP` | BMP format. |
| `WEBP` | WebP format. |

### OnlineImage class (remote_image.h)

Main component class: `esphome::remote_image::OnlineImage`. Inherits `PollingComponent`, `image::Image`, and `Parented<HttpRequestComponent>`.

#### Public methods

| Method | Description |
|--------|-------------|
| `set_url(const std::string &url)` | Set download URL. Validates scheme; clears ETag/Last-Modified. |
| `add_request_header(name, value)` | Add an HTTP request header (templatable value). |
| `set_placeholder(image::Image *placeholder)` | Image to show until the download is ready. |
| `release()` | Free the decoded buffer; image must be re-downloaded to show again. |
| `resize_download_buffer(size_t size)` | Resize the download buffer; returns new size. |
| `add_on_finished_callback(std::function<void(bool)> &&cb)` | Called when download finishes; argument is `cached`. |
| `add_on_error_callback(std::function<void()> &&cb)` | Called on download or decode error. |
| `set_fill_mode(bool fill)` | Set fill mode (true = fill/crop, false = fit). |
| `is_fill_mode() const` | Current fill mode. |
| `is_big_endian() const` | Whether buffer is big-endian (16-bit). |
| `get_fixed_width()`, `get_fixed_height() const` | Fixed resize dimensions from config (0 if auto). |
| `image_type() const` | ESPHome image type (e.g. RGB565). |

#### Drawing

- `draw(int x, int y, display::Display *display, Color color_on, Color color_off)` — Draws the image or the placeholder if the image is not ready.

---

### ImageDecoder base class (image_decoder.h)

Abstract base for format-specific decoders. Used internally; implement a subclass if adding a new image format.

#### DecodeError enum

| Value | Meaning |
|-------|---------|
| `DECODE_ERROR_INVALID_TYPE` | -1 |
| `DECODE_ERROR_UNSUPPORTED_FORMAT` | -2 |
| `DECODE_ERROR_OUT_OF_MEMORY` | -3 |

#### Methods

| Method | Description |
|--------|-------------|
| `prepare(size_t download_size)` | Initialize decoder with expected total size. Return 0 on success, negative `DecodeError` on failure. |
| `decode(uint8_t *buffer, size_t size)` | Decode up to `size` bytes from `buffer`. Return bytes consumed (0 if need more data), or negative on error. |
| `set_size(int width, int height)` | Ask `OnlineImage` to resize buffer to (width, height); applies config resize/fill. |
| `draw(int x, int y, int w, int h, const Color &color)` | Fill a rectangle in the image buffer (scaled/positioned). |
| `draw_rgb565_block(x, y, w, h, const uint8_t *data)` | Write a block of RGB565 pixels (with optional scaling). |
| `draw_rgb888_scaled(src_y, src_w, const uint8_t *rgb888, bool big_endian)` | Convert and write one scanline of RGB888 with horizontal scaling. |
| `fill_row_gap(gap_start, gap_end, src_row_y)` | Copy row `src_row_y` into destination rows [gap_start, gap_end) (for upscaling). |
| `is_finished() const` | True when all `download_size` bytes have been decoded. |

---

### DownloadBuffer class (image_decoder.h)

Ring-style buffer for incoming HTTP data. Used by `OnlineImage` and decoders.

| Method | Description |
|--------|-------------|
| `DownloadBuffer(size_t size)` | Allocate buffer of `size` bytes. |
| `data(size_t offset)` | Pointer to buffer at `offset`. |
| `append()` | Pointer to write position (same as `data(unread_)`). |
| `unread()` | Number of bytes not yet consumed. |
| `size()`, `free_capacity()` | Total size and free space. |
| `read(size_t len)` | Mark `len` bytes as consumed; compact buffer. Returns new unread count. |
| `write(size_t len)` | Advance write position by `len`. |
| `reset()` | Set unread to 0. |
| `resize(size_t size)` | Resize buffer (grow only in implementation). |
| `shrink(size_t max_size)` | Shrink buffer to at most `max_size`. |

---

## How to use in a display pipeline

1. Add `http_request:` and give it an `id`.
2. Add a `remote_image:` block with `url`, `format`, and `http_request_id`; set `id` for actions/triggers.
3. Use the `remote_image` ID wherever an image is needed (e.g. LVGL image source, or draw calls).
4. To change the image at runtime, use `remote_image.set_url` with the new URL and `update: true`.
5. Optionally use `on_download_finished` and `on_error` for loading state or fallbacks.
6. Use `remote_image.release` when the image is no longer needed to free memory.

## License

See [LICENSE](LICENSE) in this directory.
