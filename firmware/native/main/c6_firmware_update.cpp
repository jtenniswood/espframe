#include "c6_firmware_update.h"

#if !SWIPE_ESPHOME_TRIAL
#include "c6_firmware_manifest.h"
#include "clock_settings.h"
#include "firmware_update.h"
#include "native_network.h"
#include "settings.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>

namespace espframe::native {
namespace {

constexpr char tag[] = "wifi-firmware";
constexpr char partition[] = "native_config";
constexpr char attempt_namespace[] = "wifi_firmware";
constexpr char attempt_key[] = "last_attempt";
constexpr int64_t second = 1'000'000;
constexpr int64_t daily = 86'400 * second;
constexpr int64_t retry_after = 900 * second;
constexpr size_t maximum_image = 2 * 1024 * 1024;
constexpr size_t legacy_chunk = 1500;
constexpr HostedVersion host_version{
    ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1, ESP_HOSTED_VERSION_PATCH_1};

enum class Action { None, Check, Install };
std::mutex status_mutex;
TaskHandle_t worker{};
Action pending{};
bool active{};
C6FirmwareStatus state{{}, {}, "idle"};

class Download {
 public:
  ~Download() { close(); }

  void close() {
    if (client_)
      esp_http_client_cleanup(std::exchange(client_, nullptr));
  }

  int64_t open(const std::string &url) {
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.timeout_ms = 5000;
    client_ = esp_http_client_init(&config);
    if (!client_ || esp_http_client_open(client_, 0) != ESP_OK)
      return -1;
    const auto length = esp_http_client_fetch_headers(client_);
    return esp_http_client_get_status_code(client_) == 200 ? length : -1;
  }

  bool read(uint8_t *data, size_t length, int64_t deadline) {
    for (size_t offset = 0; offset < length;) {
      if (esp_timer_get_time() >= deadline)
        return false;
      const int read = esp_http_client_read(client_, reinterpret_cast<char *>(data + offset),
                                            std::min<size_t>(length - offset, 4096));
      if (read <= 0)
        return false;
      offset += read;
    }
    return esp_http_client_is_complete_data_received(client_);
  }

 private:
  esp_http_client_handle_t client_{};
};

std::optional<C6Release> fetch_release() {
  Download download;
  const auto length = download.open(std::string(hosted_firmware_origin) + "manifest/esp32c6.json");
  if (length <= 0 || length > c6_manifest_limit)
    return {};
  std::string json(length, '\0');
  if (!download.read(reinterpret_cast<uint8_t *>(json.data()), json.size(), esp_timer_get_time() + 15 * second))
    return {};
  return read_c6_release(json, host_version);
}

std::optional<HostedVersion> current_version() {
  esp_hosted_coprocessor_fwver_t version{};
  if (esp_hosted_get_coprocessor_fwversion(&version) != ESP_OK || version.major1 == 0)
    return {};
  return HostedVersion{version.major1, version.minor1, version.patch1};
}

bool already_attempted(const std::string &version) {
  nvs_handle_t storage{};
  if (nvs_open_from_partition(partition, attempt_namespace, NVS_READONLY, &storage) != ESP_OK)
    return false;
  std::array<char, 33> attempt{};
  size_t size = attempt.size();
  const auto result = nvs_get_str(storage, attempt_key, attempt.data(), &size);
  nvs_close(storage);
  return result == ESP_OK && version == attempt.data();
}

bool remember_attempt(const std::string &version) {
  nvs_handle_t storage{};
  if (nvs_open_from_partition(partition, attempt_namespace, NVS_READWRITE, &storage) != ESP_OK)
    return false;
  const bool saved = nvs_set_str(storage, attempt_key, version.c_str()) == ESP_OK && nvs_commit(storage) == ESP_OK;
  nvs_close(storage);
  return saved;
}

bool verified_image(const uint8_t *bytes, size_t length, const C6Release &release) {
  constexpr auto descriptor_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
  if (length < descriptor_offset + sizeof(esp_app_desc_t))
    return false;
  esp_image_header_t image{};
  esp_app_desc_t descriptor{};
  std::memcpy(&image, bytes, sizeof(image));
  std::memcpy(&descriptor, bytes + descriptor_offset, sizeof(descriptor));
  const std::string_view version(descriptor.version, strnlen(descriptor.version, sizeof(descriptor.version)));
  if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.chip_id != ESP_CHIP_ID_ESP32C6 || image.hash_appended != 1 ||
      descriptor.magic_word != ESP_APP_DESC_MAGIC_WORD || parse_hosted_version(version) != release.version)
    return false;
  std::array<uint8_t, 32> digest{};
  size_t digest_size{};
  return psa_crypto_init() == PSA_SUCCESS &&
         psa_hash_compute(PSA_ALG_SHA_256, bytes, length, digest.data(), digest.size(), &digest_size) == PSA_SUCCESS &&
         digest_size == digest.size() && digest == release.sha256;
}

bool automatic_setting() {
  Settings settings;
  return settings.read() && settings.stock_update_preferences.wifi_auto_update.value_or(true);
}

bool install_release(const C6Release &release, const HostedVersion &current, bool automatic) {
  Download download;
  const auto length = download.open(release.url);
  if (length <= 0 || length > maximum_image)
    return false;
  const std::unique_ptr<uint8_t, decltype(&std::free)> bytes(
      static_cast<uint8_t *>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), std::free);
  if (!bytes || !download.read(bytes.get(), length, esp_timer_get_time() + 90 * second) ||
      !verified_image(bytes.get(), length, release))
    return false;
  download.close();

  struct Transfer {
    const uint8_t *bytes;
    size_t size;
    std::string version;
    HostedVersion current;
    bool automatic;
  } transfer{bytes.get(), static_cast<size_t>(length), hosted_version_text(release.version), current, automatic};
  return run_coprocessor_update([](void *context) {
    const auto &image = *static_cast<const Transfer *>(context);
    if ((image.automatic && !automatic_setting()) || current_version() != image.current ||
        esp_hosted_cp_ota_begin() != ESP_OK)
      return false;
    for (size_t offset = 0; offset < image.size;) {
      const auto count = std::min(image.size - offset, legacy_chunk);
      if (esp_hosted_cp_ota_write(image.bytes + offset, count) != ESP_OK) {
        // Do not finalize a partial transfer. Older radios activate in ota_end.
        return false;
      }
      offset += count;
    }
    if (!remember_attempt(image.version) || esp_hosted_cp_ota_end() != ESP_OK)
      return false;
    if (esp_hosted_cp_ota_activate() != ESP_OK)
      return false;
    ESP_LOGI(tag, "Wi-Fi firmware %s staged. Restarting.", image.version.c_str());
    return true;
  }, &transfer);
}

void publish(std::optional<HostedVersion> current, const std::optional<C6Release> &release, const char *status) {
  const std::lock_guard lock(status_mutex);
  if (current)
    state.current = hosted_version_text(*current);
  state.latest = release ? hosted_version_text(release->version) : "";
  state.state = status;
}

void run(void *context) {
  auto &network = *static_cast<NativeNetwork *>(context);
  std::optional<C6Release> release;
  int64_t due = esp_timer_get_time() + 30 * second;
  int64_t read_preferences_at{};
  bool automatic{};
  bool identified{};
  for (;;) {
    const auto now = esp_timer_get_time();
    if (now >= read_preferences_at) {
      const bool enabled = automatic_setting();
      if (enabled && !automatic && read_preferences_at)
        due = now;
      automatic = enabled;
      read_preferences_at = now + 15 * second;
    }
    if (network.station_lease() && !identified) {
      const auto version = current_version();
      identified = version.has_value();
      if (version) {
        const std::lock_guard lock(status_mutex);
        state.current = hosted_version_text(*version);
      }
    }
    Action action;
    {
      const std::lock_guard lock(status_mutex);
      action = pending;
    }
    const bool scheduled = now >= due;
    if ((!scheduled && action == Action::None) || !network.station_lease() || network.busy() ||
        network.recovery_status() != NativeNetwork::RecoveryStatus::Off ||
        !clock_time_is_valid(std::time(nullptr))) {
      if (network.station_lease() && (scheduled || action != Action::None))
        network.request_time_sync();
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    {
      const std::lock_guard lock(status_mutex);
      active = true;
      action = std::exchange(pending, Action::None);
      state.state = action == Action::Install ? "installing" : "checking";
    }
    const auto current = current_version();
    if (action != Action::Install)
      release = fetch_release();
    const char *result = current && release ? "ready" : "unavailable";
    const bool manual_install = action == Action::Install;
    const bool automatic_install = action == Action::None && automatic && automatic_setting();
    if ((manual_install || automatic_install) && current && release && release->version > *current) {
      if (automatic_install && already_attempted(hosted_version_text(release->version))) {
        result = "failed";
      } else {
        publish(current, release, "installing");
        // On success the coordinated install restarts the host and radio.
        if (!install_release(*release, *current, automatic_install))
          result = "failed";
      }
    }
    publish(current, release, result);
    if (action == Action::None)
      due = esp_timer_get_time() + (current && release ? daily : retry_after);
    {
      const std::lock_guard lock(status_mutex);
      active = false;
    }
  }
}

}  // namespace

void start_c6_firmware_updates(NativeNetwork &network) {
  if (is_firmware_test_build())
    return;
  const std::lock_guard lock(status_mutex);
  if (!worker && xTaskCreate(run, "wifi-firmware", 6144, &network, 2, &worker) != pdPASS)
    ESP_LOGE(tag, "Could not start Wi-Fi update worker");
}

C6FirmwareStatus c6_firmware_status() {
  const std::lock_guard lock(status_mutex);
  return state;
}

bool request_c6_firmware_update(bool install) {
  const std::lock_guard lock(status_mutex);
  if (!worker || active || pending != Action::None)
    return false;
  pending = install ? Action::Install : Action::Check;
  state.state = install ? "installing" : "checking";
  xTaskNotifyGive(worker);
  return true;
}

}  // namespace espframe::native

#else

namespace espframe::native {

void start_c6_firmware_updates(NativeNetwork &) {}
C6FirmwareStatus c6_firmware_status() { return {{}, {}, "unavailable"}; }
bool request_c6_firmware_update(bool) { return false; }

}  // namespace espframe::native

#endif
