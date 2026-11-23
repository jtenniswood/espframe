#include "frame_blitter.h"
#include "freertos/semphr.h"
#include "touch_report.h"

#include <algorithm>
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
struct QueuedCopy {
  async_color_convert_request_t request;
  async_color_convert_isr_cb_t completed;
  void *context;
};
std::vector<QueuedCopy> queued;
size_t most_queued{};

void complete(const async_color_convert_request_t &copy) {
  using espframe::native::panel_width;
  for (size_t row = 0; row < copy.copy_height; ++row) {
    std::memcpy(static_cast<uint16_t *>(copy.dst_buffer) + row * panel_width + copy.dst_x,
                static_cast<const uint16_t *>(copy.src_buffer) + row * panel_width + copy.src_x,
                copy.copy_width * sizeof(uint16_t));
  }
}
}  // namespace

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t maximum, UBaseType_t count, StaticSemaphore_t *storage) {
  assert(queued.empty() && maximum == 2 && count == 0);
  *storage = {maximum, count};
  return storage;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t signal, TickType_t timeout) {
  assert(timeout == portMAX_DELAY);
  // Drain simulated hardware in reverse order to cover out-of-order callbacks.
  if (signal->count == 0) {
    assert(!queued.empty());
    const auto last = queued.back();
    queued.pop_back();
    complete(last.request);
    last.completed(&driver, nullptr, last.context);
  }
  assert(signal->count != 0);
  --signal->count;
  return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t signal, BaseType_t *task_woken) {
  assert(signal->count < signal->maximum);
  ++signal->count;
  *task_woken = pdTRUE;
  return pdTRUE;
}

esp_err_t esp_async_color_convert_install_dma2d(const async_color_convert_config_t *config,
                                                async_color_convert_handle_t *handle) {
  assert(config->backlog == 2 && config->dma_burst_size == espframe::native::FrameBlitter::burst_bytes);
  if (reject_install)
    return ESP_FAIL;
  *handle = &driver;
  return ESP_OK;
}

esp_err_t esp_async_color_convert_uninstall(async_color_convert_handle_t handle) {
  assert(handle == &driver && queued.empty());
  ++releases;
  return ESP_OK;
}

esp_err_t esp_async_color_convert(async_color_convert_handle_t handle, const async_color_convert_request_t *request,
                                  async_color_convert_isr_cb_t done, void *context) {
  using espframe::native::panel_width;
  assert(handle == &driver && queued.size() < 2 && done != nullptr);
  assert(request->copy_width == panel_width && request->dst_x == 0 && request->src_x == 0);
  if (++requests == reject_request)
    return ESP_FAIL;
  queued.push_back({*request, done, context});
  most_queued = std::max(most_queued, queued.size());
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
  complete(*request);
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
  assert(output == expected && requests == 2 && most_queued == 2 && queued.empty());

  constexpr int edge_offset = -36;
  dma.edge(output.data(), left.data(), edge_offset);
  blit_slide_edge(expected.data(), left.data(), panel_width, panel_height, edge_offset);
  assert(output == expected && requests == 3);

  for (auto rotation : {ScreenRotation::Normal, ScreenRotation::UpsideDown,
                        ScreenRotation::Clockwise, ScreenRotation::Counterclockwise}) {
    const Viewport viewport{rotation};
    const auto panel = [rotation](int x, int y) {
      switch (rotation) {
        case ScreenRotation::Normal: return x * panel_width + panel_width - 1 - y;
        case ScreenRotation::UpsideDown: return (panel_height - 1 - x) * panel_width + y;
        case ScreenRotation::Clockwise: return pixel_count - 1 - (y * panel_width + x);
        case ScreenRotation::Counterclockwise: return y * panel_width + x;
      }
      return -1;
    };
    for (int y : {0, 101, viewport.height - 1}) {
      for (int x : {0, 527, viewport.width - 1}) {
        const int physical = panel(x, y);
        assert(viewport.offset(x, y) == physical);
        TouchReport report;
        map_gsl_touch(report, panel_width - 1 - physical % panel_width, physical / panel_width, rotation);
        assert(report.x == x && report.y == y);
      }
    }
    for (const auto &direction : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
      const auto [dx, dy] = direction;
      const int extent = dx ? viewport.width : viewport.height;
      constexpr uint8_t progress = 51;  // One fifth of the screen.
      const int distance = extent / 5;
      for (int y = 0; y < viewport.height; ++y) {
        for (int x = 0; x < viewport.width; ++x) {
          int source_x = x - dx * distance;
          int source_y = y - dy * distance;
          const bool incoming = source_x < 0 || source_x >= viewport.width || source_y < 0 || source_y >= viewport.height;
          if (incoming) {
            source_x += dx * viewport.width;
            source_y += dy * viewport.height;
          }
          expected[panel(x, y)] = (incoming ? right : left)[panel(source_x, source_y)];
        }
      }
      const int before = requests;
      if (viewport.portrait ? dx != 0 : dy != 0)
        reject_request = before + 2;
      dma.push(output.data(), left.data(), right.data(), dx, dy, progress, rotation);
      assert(output == expected && requests == before + 2 && releases == 0);
    }
    if (viewport.portrait) {
      constexpr int offset = 167;
      const auto span = orient_slide_span(left.data(), right.data(), viewport.width, offset, rotation);
      dma.slides(output.data(), span, true);
      for (int y = 0; y < viewport.height; ++y) {
        for (int x = 0; x < viewport.width; ++x) {
          const int source_x = x + offset;
          const auto &source = source_x < viewport.width ? left : right;
          assert(output[panel(x, y)] == source[panel(source_x % viewport.width, y)]);
        }
      }
      for (int pull : {-36, 36}) {
        dma.edge(output.data(), left.data(), pull * axis_direction(rotation), true);
        for (int y = 0; y < viewport.height; ++y) {
          for (int x = 0; x < viewport.width; ++x) {
            const int source_x = std::clamp(x + pull, 0, viewport.width - 1);
            assert(output[panel(x, y)] == left[panel(source_x, y)]);
          }
        }
      }
    }
  }

  reject_request = requests + 2;
  blit_slides(expected.data(), left.data(), right.data(), panel_width, panel_height, offset);
  dma.slides(output.data(), {left.data(), right.data(), offset});
  assert(output == expected && releases == 1);
  dma.slides(output.data(), {right.data(), nullptr, 0});
  assert(output == right && requests == reject_request && releases == 1);

  FrameBlitter failed_first;
  failed_first.init();
  reject_request = requests + 1;
  failed_first.slides(output.data(), {left.data(), right.data(), offset});
  assert(output == expected && requests == reject_request && releases == 2 && queued.empty());

  reject_install = true;
  FrameBlitter cpu;
  cpu.init();
  cpu.slides(output.data(), {left.data(), nullptr, 0});
  assert(output == left && requests == reject_request);
}
