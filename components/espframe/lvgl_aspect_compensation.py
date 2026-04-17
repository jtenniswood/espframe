"""PlatformIO build hook for Guition JC1060P470 pixel-aspect compensation."""

from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SOURCE_FILE = PROJECT_DIR / "src" / "esphome" / "components" / "lvgl" / "lvgl_esphome.cpp"

START = "// ESPFrame aspect compensation start"
END = "// ESPFrame aspect compensation end"

if not SOURCE_FILE.exists():
    print(f"ESPFrame aspect compensation: {SOURCE_FILE} not found")
else:
    text = SOURCE_FILE.read_text()
    old_condition = (
        "#if defined(ESPFRAME_LVGL_ASPECT_SCALE_X) && "
        "ESPFRAME_LVGL_ASPECT_SCALE_X > 0 && ESPFRAME_LVGL_ASPECT_SCALE_X < 1"
    )
    text = text.replace(old_condition, "#if defined(ESPFRAME_LVGL_ASPECT_SCALE_X)")

    if START not in text:
        text = text.replace(
            "#include <numeric>\n",
            "#include <numeric>\n#include <cmath>\n#include <cstring>\n",
        )

        original_draw = """\
  for (auto *display : this->displays_) {
    display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) ptr, display::COLOR_ORDER_RGB, LV_BITNESS,
                            this->big_endian_);
  }
"""

        compensated_draw = """\
  // ESPFrame aspect compensation start
#if defined(ESPFRAME_LVGL_ASPECT_SCALE_X)
  const float scale_x = ESPFRAME_LVGL_ASPECT_SCALE_X;
  const int panel_width = ESPFRAME_LVGL_ASPECT_PANEL_WIDTH > 0 ? ESPFRAME_LVGL_ASPECT_PANEL_WIDTH : this->width_;
  const int scaled_panel_width = static_cast<int>(std::round(panel_width * scale_x));
  const int margin_x = (panel_width - scaled_panel_width) / 2;
  int dest_x1 = margin_x + static_cast<int>(std::floor(x1 * scale_x));
  int dest_x2 = margin_x + static_cast<int>(std::ceil((x1 + width) * scale_x)) - 1;
  if (dest_x1 < 0) dest_x1 = 0;
  if (dest_x2 >= panel_width) dest_x2 = panel_width - 1;
  const int dest_width = dest_x2 - dest_x1 + 1;

  if (dest_width > 0 && height > 0 && width > 0) {
    const size_t out_pixels = static_cast<size_t>(dest_width) * height;
    auto *scaled = static_cast<lv_color_data *>(lv_malloc(out_pixels * sizeof(lv_color_data)));
    if (scaled != nullptr) {
      for (int yy = 0; yy < height; yy++) {
        const auto *src_row = ptr + static_cast<size_t>(yy) * width;
        auto *dst_row = scaled + static_cast<size_t>(yy) * dest_width;
        for (int dx = 0; dx < dest_width; dx++) {
          float logical_x = (static_cast<float>(dest_x1 + dx - margin_x) + 0.5f) / scale_x - 0.5f;
          int sx = static_cast<int>(std::round(logical_x)) - x1;
          if (sx < 0) sx = 0;
          if (sx >= width) sx = width - 1;
          dst_row[dx] = src_row[sx];
        }
      }

      lv_color_data *black = nullptr;
      if (margin_x > 0) {
        black = static_cast<lv_color_data *>(lv_malloc(static_cast<size_t>(margin_x) * height * sizeof(lv_color_data)));
        if (black != nullptr) {
          std::memset(black, 0, static_cast<size_t>(margin_x) * height * sizeof(lv_color_data));
        }
      }

      for (auto *display : this->displays_) {
        if (black != nullptr) {
          display->draw_pixels_at(0, y1, margin_x, height, reinterpret_cast<const uint8_t *>(black),
                                  display::COLOR_ORDER_RGB, LV_BITNESS, this->big_endian_);
          display->draw_pixels_at(panel_width - margin_x, y1, margin_x, height, reinterpret_cast<const uint8_t *>(black),
                                  display::COLOR_ORDER_RGB, LV_BITNESS, this->big_endian_);
        }
        display->draw_pixels_at(dest_x1, y1, dest_width, height, reinterpret_cast<const uint8_t *>(scaled),
                                display::COLOR_ORDER_RGB, LV_BITNESS, this->big_endian_);
      }

      if (black != nullptr) lv_free(black);
      lv_free(scaled);
      return;
    }
    ESP_LOGW(TAG, "Aspect compensation allocation failed; drawing unscaled");
  }
#endif
  for (auto *display : this->displays_) {
    display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) ptr, display::COLOR_ORDER_RGB, LV_BITNESS,
                            this->big_endian_);
  }
  // ESPFrame aspect compensation end
"""

        if original_draw not in text:
            raise RuntimeError("ESPFrame aspect compensation: draw hook target not found")
        text = text.replace(original_draw, compensated_draw, 1)

        original_touch = "      l->parent_->rotate_coordinates(data->point.x, data->point.y);\n"
        compensated_touch = """\
      l->parent_->rotate_coordinates(data->point.x, data->point.y);
      // ESPFrame aspect compensation start
#if defined(ESPFRAME_LVGL_ASPECT_SCALE_X)
      const float scale_x = ESPFRAME_LVGL_ASPECT_SCALE_X;
      const int logical_width = l->parent_->get_width();
      const int scaled_width = static_cast<int>(std::round(logical_width * scale_x));
      const int margin_x = (logical_width - scaled_width) / 2;
      int mapped_x = static_cast<int>(std::round((data->point.x - margin_x) / scale_x));
      if (mapped_x < 0) mapped_x = 0;
      if (mapped_x >= logical_width) mapped_x = logical_width - 1;
      data->point.x = mapped_x;
#endif
      // ESPFrame aspect compensation end
"""

        if original_touch not in text:
            raise RuntimeError("ESPFrame aspect compensation: touch hook target not found")
        text = text.replace(original_touch, compensated_touch, 1)

        SOURCE_FILE.write_text(text)
        print("ESPFrame aspect compensation: LVGL flush hook patched")
    elif old_condition in SOURCE_FILE.read_text():
        SOURCE_FILE.write_text(text)
        print("ESPFrame aspect compensation: LVGL flush hook normalized")
