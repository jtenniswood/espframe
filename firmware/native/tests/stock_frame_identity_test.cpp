#include "frame_identity.h"
#include "stock_frame_identity.h"
#include "nvs.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

std::vector<uint8_t> stored;
bool namespace_present = true;
bool opened = false;

void stock_record(std::string_view name) {
  stored.assign(128, 0);
  stored[0] = 1;
  std::copy(name.begin(), name.end(), stored.begin() + 4);
  std::fill(stored.end() - 3, stored.end(), 0xff);  // Alignment bytes are not part of the name.
}

}  // namespace

esp_err_t nvs_open_from_partition(const char *partition, const char *ns, int mode, nvs_handle_t *handle) {
  assert(std::string_view(partition) == "nvs" && std::string_view(ns) == "espframe_id" && mode == NVS_READONLY);
  assert(!opened);
  if (!namespace_present)
    return ESP_ERR_NVS_NOT_FOUND;
  *handle = 17;
  opened = true;
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *bytes, size_t *size) {
  assert(opened && handle == 17 && std::string_view(key) == "identity");
  if (stored.empty())
    return ESP_ERR_NVS_NOT_FOUND;
  if (stored.size() > *size)
    return ESP_ERR_NVS_TYPE_MISMATCH;
  *size = stored.size();
  std::memcpy(bytes, stored.data(), stored.size());
  return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
  assert(opened && handle == 17);
  opened = false;
}

int main(int argc, char **argv) {
  using espframe::native::FrameMac;
  using espframe::native::make_frame_identity;
  using espframe::native::read_stock_frame_name;
  constexpr FrameMac mac{0x02, 0xab, 0xcd, 0xef, 0x12, 0x9a};
  constexpr char hostname[] = "immich-frame-10inch";
  constexpr char friendly_name[] = "Espframe 10inch";

  stock_record("  Salon été  ");
  const auto saved_name = read_stock_frame_name("nvs");
  assert(saved_name == "Salon été" && !opened);
  assert(make_frame_identity(*saved_name, mac, hostname, friendly_name, true).hostname == "salon-t-129a");
  stock_record("");
  const auto cleared_name = read_stock_frame_name("nvs");
  assert(cleared_name == "");
  assert(make_frame_identity(*cleared_name, mac, hostname, friendly_name, true).hostname == "immich-frame-10inch-ef129a");
  stock_record(std::string(120, 'a'));
  assert(read_stock_frame_name("nvs") == std::string(120, 'a'));
  stored[124] = 'a';
  assert(!read_stock_frame_name("nvs") && !opened);
  stock_record("wrong version");
  stored[0] = 2;
  assert(!read_stock_frame_name("nvs"));
  stock_record("truncated blob");
  stored.pop_back();
  assert(!read_stock_frame_name("nvs"));
  stored.resize(129);
  assert(!read_stock_frame_name("nvs"));
  stock_record("invalid\xc0\xaf");
  assert(!read_stock_frame_name("nvs"));
  stock_record("invalid\x1f");
  assert(!read_stock_frame_name("nvs"));
  stored.clear();
  assert(!read_stock_frame_name("nvs") && !opened);
  namespace_present = false;
  assert(!read_stock_frame_name("nvs") && !opened);

  if (argc > 1) {
    std::ifstream blob(argv[1], std::ios::binary);
    assert(blob);
    stored.assign(std::istreambuf_iterator<char>(blob), {});
    namespace_present = true;
    assert(read_stock_frame_name("nvs") == "Salon été" && !opened);
  }
}
