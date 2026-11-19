#include "frame_blitter.h"

#include <cassert>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

struct async_color_convert_context_t {};

namespace {
async_color_convert_context_t driver;
bool reject_install{};
int reject_request{};
int requests{};
int releases{};
}  // namespace

esp_err_t esp_async_color_convert_install_dma2d(const async_color_convert_config_t *config,
                                                async_color_convert_handle_t *handle) {
  assert(config->backlog == 1 && config->dma_burst_size == espframe::native::FrameBlitter::burst_bytes);
  if (reject_install)
    return ESP_FAIL;
  *handle = &driver;
  return ESP_OK;
}

esp_err_t esp_async_color_convert_uninstall(async_color_convert_handle_t handle) {
  assert(handle == &driver);
  ++releases;
  return ESP_OK;
}

esp_err_t esp_color_convert_blocking(async_color_convert_handle_t handle, const async_color_convert_request_t *request,
                                     int32_t timeout) {
  using espframe::native::panel_width;
  assert(handle == &driver && timeout == -1);
  assert(request->src_color_format == ESP_COLOR_FOURCC_RGB16 && request->dst_color_format == ESP_COLOR_FOURCC_RGB16);
  assert(request->src_stride == panel_width && request->dst_stride == panel_width);
  assert(request->src_x + request->copy_width <= panel_width && request->dst_x + request->copy_width <= panel_width);
  assert(request->copy_height == request->src_height && request->copy_height == request->dst_height);
  assert(request->src_y == 0 && request->dst_y == 0);
  if (++requests == reject_request)
    return ESP_FAIL;
  for (size_t row = 0; row < request->copy_height; ++row) {
    std::memcpy(static_cast<uint16_t *>(request->dst_buffer) + row * panel_width + request->dst_x,
                static_cast<const uint16_t *>(request->src_buffer) + row * panel_width + request->src_x,
                request->copy_width * sizeof(uint16_t));
  }
  return ESP_OK;
}

int main() {
  using namespace espframe::native;
  std::vector<uint16_t> left(pixel_count), right(pixel_count), output(pixel_count), expected(pixel_count);
  std::iota(left.begin(), left.end(), uint16_t{1});
  std::iota(right.begin(), right.end(), uint16_t{117});
  constexpr int offset = 613;
  const auto span = orient_slide_span(left.data(), right.data(), panel_height, offset, ScreenRotation::UpsideDown);
  blit_slides(expected.data(), span.first, span.second, panel_width, panel_height, span.offset);

  FrameBlitter dma;
  dma.init();
  dma.slides(output.data(), span);
  assert(output == expected && requests == 2);

  constexpr int edge_offset = -36;
  dma.edge(output.data(), left.data(), edge_offset);
  blit_slide_edge(expected.data(), left.data(), panel_width, panel_height, edge_offset);
  assert(output == expected && requests == 3);

  for (auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown}) {
    for (const auto &direction : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
      const auto [dx, dy] = direction;
      const int extent = dx ? slide_width : slide_height;
      constexpr uint8_t progress = 51;  // One fifth of the screen.
      const int distance = extent / 5;
      const auto panel = [rotation](int x, int y) { return panel_offset(x, slide_height - 1 - y, rotation); };
      for (int y = 0; y < slide_height; ++y) {
        for (int x = 0; x < slide_width; ++x) {
          int source_x = x - dx * distance;
          int source_y = y - dy * distance;
          const bool incoming = source_x < 0 || source_x >= slide_width || source_y < 0 || source_y >= slide_height;
          if (incoming) {
            source_x += dx * slide_width;
            source_y += dy * slide_height;
          }
          expected[panel(x, y)] = (incoming ? right : left)[panel(source_x, source_y)];
        }
      }
      const int before = requests;
      if (dy)
        reject_request = before + 2;
      dma.push(output.data(), left.data(), right.data(), dx, dy, progress, rotation);
      assert(output == expected && requests == before + 2 && releases == 0);
    }
  }

  reject_request = requests + 2;
  blit_slides(expected.data(), left.data(), right.data(), panel_width, panel_height, offset);
  dma.slides(output.data(), {left.data(), right.data(), offset});
  assert(output == expected && releases == 1);
  dma.slides(output.data(), {right.data(), nullptr, 0});
  assert(output == right && requests == reject_request && releases == 1);

  reject_install = true;
  FrameBlitter cpu;
  cpu.init();
  cpu.slides(output.data(), {left.data(), nullptr, 0});
  assert(output == left && requests == reject_request);
}
