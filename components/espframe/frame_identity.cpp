#include "frame_identity.h"

#include <cstring>
#include <nvs.h>
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

// Startup adapter follows espcontrol PR #1924. Application's backing StringRefs
// are non-const in this version. Review this adapter when upgrading ESPHome.
static_assert(ESPHOME_VERSION_CODE == VERSION_CODE(2026, 8, 2),
              "Review frame identity startup adapter for this ESPHome version");

namespace esphome::espframe {
namespace {
constexpr char STORAGE_NAMESPACE[] = "espframe_id";
constexpr char STORAGE_KEY[] = "identity";
struct IdentityRecord {
  uint32_t version{1};
  char name[FRAME_NAME_MAX_BYTES + 1]{};
};
}

void FrameIdentity::setup() {
  default_hostname_ = App.get_name().c_str();
  default_friendly_ = App.get_friendly_name().c_str();
  if (default_friendly_[0] == 0) default_friendly_ = default_hostname_;

  // ESPHome initializes NVS before component setup. Read-only opening means an
  // upgrade with no saved name does not consume storage or write preferences.
  nvs_handle_t handle;
  esp_err_t result = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    IdentityRecord record;
    size_t length = sizeof(record);
    result = nvs_get_blob(handle, STORAGE_KEY, &record, &length);
    nvs_close(handle);
    if (result == ESP_OK && length == sizeof(record) && record.version == 1 &&
        std::memchr(record.name, 0, sizeof(record.name)) != nullptr &&
        normalize_frame_name(record.name, saved_name_)) {
      running_friendly_ = saved_name_;
    } else {
      saved_name_.clear();
      ESP_LOGW("espframe.identity", "Invalid or unavailable saved name; using firmware defaults");
    }
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW("espframe.identity", "Name storage unavailable; using firmware defaults");
  }
  if (!running_friendly_.empty()) {
    const std::string hostname = target_hostname();
    running_hostname_ = std::make_unique<char[]>(hostname.size() + 1);
    std::memcpy(running_hostname_.get(), hostname.c_str(), hostname.size() + 1);
    // Entity registration has already copied names and calculated hashes. Only
    // rebind Application identity, before WiFi, mDNS and API component setup.
    const_cast<StringRef &>(App.get_name()) = StringRef(running_hostname_.get(), hostname.size());
    const_cast<StringRef &>(App.get_friendly_name()) = StringRef(running_friendly_.c_str(), running_friendly_.size());
  }
}

std::string FrameIdentity::suffix() const {
  char mac[MAC_ADDRESS_BUFFER_SIZE];
  get_mac_address_into_buffer(mac);
  std::string suffix = std::string(mac + 8, 4);
  for (char &c : suffix) if (c >= 'A' && c <= 'F') c += 'a' - 'A';

  return suffix;
}

bool FrameIdentity::save(const std::string &name) {
  std::string normalized;
  if (!normalize_frame_name(name, normalized)) return false;
  // An empty in-memory name may follow an NVS read failure. Always persist
  // explicit clearing so a previously stored name cannot return on reboot.
  if (!normalized.empty() && normalized == saved_name_) return true;
  IdentityRecord record;
  std::memcpy(record.name, normalized.data(), normalized.size());
  nvs_handle_t handle;
  esp_err_t result = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    result = nvs_set_blob(handle, STORAGE_KEY, &record, sizeof(record));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  // Never erase shared storage on failure, and never report an unsaved name.
  if (result != ESP_OK) {
    ESP_LOGE("espframe.identity", "Name save failed: %d", result);
    return false;
  }
  saved_name_ = normalized;
  return true;
}
}  // namespace esphome::espframe
