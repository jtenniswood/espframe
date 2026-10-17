"""Fault-inject the production transition setup method without ESP32 hardware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "components/mipi_dsi/mipi_dsi.cpp").read_text()
start = source.index("void MipiDsi::setup_frame_transitions_() {")
end = source.index("\n}\n", start) + len("\n}\n")
method = source[start:end]

# Compile the actual method against allocation/PPA fakes so every failure
# path can run on the host. No framebuffer memory or ESP-IDF is required.
harness = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <iostream>

constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
constexpr int ESP_OK = 0, PPA_OPERATION_BLEND = 1;
#define ESP_LOGW(...) ((void) 0)
struct ppa_client_config_t { int oper_type{}; int max_pending_trans_num{}; };
using ppa_client_handle_t = void *;
static std::set<void *> allocations;
static int allocation_calls = 0, fail_allocation = 0, register_calls = 0;
static bool fail_registration = false;
static int client;

void *heap_caps_aligned_alloc(size_t alignment, size_t size, int caps) {
  assert(alignment == 128 && size == 800 * 1280 * 2);
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (++allocation_calls == fail_allocation) return nullptr;
  void *p = std::malloc(1);
  assert(p != nullptr);
  assert(allocations.insert(p).second);
  return p;
}
void heap_caps_free(void *p) {
  if (p == nullptr) return;
  assert(allocations.erase(p) == 1); // Detect double frees and non-owned pointers.
  std::free(p);
}
int ppa_register_client(const ppa_client_config_t *config, ppa_client_handle_t *handle) {
  assert(config->oper_type == PPA_OPERATION_BLEND && config->max_pending_trans_num == 1);
  ++register_calls;
  if (fail_registration) return -1;
  *handle = &client;
  return ESP_OK;
}
struct MipiDsi {
  size_t width_{800}, height_{1280};
  int color_depth_{1};
  bool frame_transitions_{true};
  ppa_client_handle_t ppa_client_{&client}, blend_client_{nullptr};
  uint8_t *snapshot_{nullptr}, *target_{nullptr};
  void setup_frame_transitions_();
};
""" + method + r"""
int main() {
  // Disabled transitions and unavailable SRM must never reserve frame memory.
  for (bool enabled : {false, true}) {
    MipiDsi display;
    display.frame_transitions_ = enabled;
    display.ppa_client_ = nullptr;
    display.setup_frame_transitions_();
    assert(!display.frame_transitions_ && allocation_calls == 0 && register_calls == 0);
  }
  // Failure of either allocation must release the other successful allocation.
  // A registration failure must release both; success must retain both.
  for (int failure : {0, 1, 2, 3}) {
    allocation_calls = register_calls = 0;
    fail_allocation = failure <= 2 ? failure : 0;
    fail_registration = failure == 3;
    MipiDsi display;
    display.setup_frame_transitions_();
    assert(allocation_calls == 2);
    assert(register_calls == (failure == 1 || failure == 2 ? 0 : 1));
    if (failure == 0) {
      assert(display.frame_transitions_ && allocations.size() == 2);
      assert(display.snapshot_ && display.target_ && display.blend_client_ == &client);
      heap_caps_free(display.snapshot_);
      heap_caps_free(display.target_);
    } else {
      assert(!display.frame_transitions_ && allocations.empty());
      assert(!display.snapshot_ && !display.target_ && !display.blend_client_);
      // The ordinary double-buffering copy client remains available.
      assert(display.ppa_client_ == &client);
    }
    assert(allocations.empty());
  }
  std::cout << "MIPI transition setup fault-injection tests passed\n";
}
"""
with tempfile.TemporaryDirectory(prefix="espframe-mipi-transitions-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
