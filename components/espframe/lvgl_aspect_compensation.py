"""PlatformIO build hook for Guition JC1060P470 pixel-aspect compensation."""

from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SOURCE_FILE = PROJECT_DIR / "src" / "esphome" / "components" / "mipi_dsi" / "mipi_dsi.cpp"

START = "// ESPFrame MIPI aspect compensation start"

if not SOURCE_FILE.exists():
    print(f"ESPFrame aspect compensation: {SOURCE_FILE} not found")
else:
    text = SOURCE_FILE.read_text()

    if START not in text:
        text = text.replace(
            "#include <utility>\n",
            "#include <utility>\n#include <cmath>\n#include <cstring>\n",
            1,
        )

        original = """\
void MIPI_DSI::write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                                 int x_pad) {
  esp_err_t err = ESP_OK;
"""

        replacement = """\
void MIPI_DSI::write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                                 int x_pad) {
  // ESPFrame MIPI aspect compensation start
#if defined(ESPFRAME_DISPLAY_ASPECT_SCALE_X)
  const float scale_x = ESPFRAME_DISPLAY_ASPECT_SCALE_X;
  if (scale_x > 0.0f && scale_x < 1.0f && w > 0 && h > 0) {
    const int panel_width = ESPFRAME_DISPLAY_ASPECT_PANEL_WIDTH > 0 ? ESPFRAME_DISPLAY_ASPECT_PANEL_WIDTH : this->width_;
    const int scaled_panel_width = static_cast<int>(std::round(panel_width * scale_x));
    const int margin_x = (panel_width - scaled_panel_width) / 2;
    int dest_x1 = margin_x + static_cast<int>(std::floor(x_start * scale_x));
    int dest_x2 = margin_x + static_cast<int>(std::ceil((x_start + w) * scale_x)) - 1;
    if (dest_x1 < 0) dest_x1 = 0;
    if (dest_x2 >= panel_width) dest_x2 = panel_width - 1;
    const int dest_width = dest_x2 - dest_x1 + 1;

    if (dest_width > 0) {
      auto bytes_per_pixel = 3 - this->color_depth_;
      const int source_stride = (x_offset + w + x_pad) * bytes_per_pixel;
      const uint8_t *source_base = ptr + y_offset * source_stride + x_offset * bytes_per_pixel;
      const size_t scaled_bytes = static_cast<size_t>(dest_width) * h * bytes_per_pixel;
      RAMAllocator<uint8_t> allocator;
      auto *scaled = allocator.allocate(scaled_bytes);

      if (scaled != nullptr) {
        for (int yy = 0; yy < h; yy++) {
          const uint8_t *src_row = source_base + yy * source_stride;
          uint8_t *dst_row = scaled + static_cast<size_t>(yy) * dest_width * bytes_per_pixel;
          for (int dx = 0; dx < dest_width; dx++) {
            float logical_x = (static_cast<float>(dest_x1 + dx - margin_x) + 0.5f) / scale_x - 0.5f;
            int sx = static_cast<int>(std::round(logical_x)) - x_start;
            if (sx < 0) sx = 0;
            if (sx >= w) sx = w - 1;
            std::memcpy(dst_row + dx * bytes_per_pixel, src_row + sx * bytes_per_pixel, bytes_per_pixel);
          }
        }

        uint8_t *black = nullptr;
        if (margin_x > 0) {
          const size_t black_bytes = static_cast<size_t>(margin_x) * h * bytes_per_pixel;
          black = allocator.allocate(black_bytes);
          if (black != nullptr) {
            std::memset(black, 0, black_bytes);
          }
        }

        esp_err_t err = ESP_OK;
        if (black != nullptr) {
          err = esp_lcd_panel_draw_bitmap(this->handle_, 0, y_start, margin_x, y_start + h, black);
          xSemaphoreTake(this->io_lock_, portMAX_DELAY);
          if (err == ESP_OK) {
            err = esp_lcd_panel_draw_bitmap(this->handle_, panel_width - margin_x, y_start, panel_width, y_start + h,
                                            black);
            xSemaphoreTake(this->io_lock_, portMAX_DELAY);
          }
        }
        if (err == ESP_OK) {
          err = esp_lcd_panel_draw_bitmap(this->handle_, dest_x1, y_start, dest_x1 + dest_width, y_start + h, scaled);
          xSemaphoreTake(this->io_lock_, portMAX_DELAY);
        }

        if (black != nullptr) free(black);
        free(scaled);
        if (err != ESP_OK)
          ESP_LOGE(TAG, "lcd_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
        return;
      }
      ESP_LOGW(TAG, "Aspect compensation allocation failed; drawing unscaled");
    }
  }
#endif
  // ESPFrame MIPI aspect compensation end
  esp_err_t err = ESP_OK;
"""

        if original not in text:
            raise RuntimeError("ESPFrame aspect compensation: MIPI write hook target not found")
        text = text.replace(original, replacement, 1)
        SOURCE_FILE.write_text(text)
        print("ESPFrame aspect compensation: MIPI draw hook patched")
