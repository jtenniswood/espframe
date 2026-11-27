#include "native_release_updates.h"

#include "clock_settings.h"
#include "firmware_update.h"
#include "native_network.h"
#include "native_release_index.h"
#include "native_release_schedule.h"
#include "settings.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "psa/crypto.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>

namespace espframe::native {
namespace {

constexpr char tag[] = "native-release";
constexpr std::string_view feed = NATIVE_RELEASE_FEED;
constexpr bool stable = NATIVE_STABLE_RELEASE;
constexpr auto parse_version = stable ? parse_stable_version : parse_preview_version;
constexpr int64_t second = 1'000'000;

std::mutex mutex;
std::vector<NativeRelease> releases;
std::optional<std::string> pending;
const char *status = "idle";
TaskHandle_t worker{};
bool busy{};

class Download {
 public:
  ~Download() {
    if (client_)
      esp_http_client_cleanup(client_);
    psa_hash_abort(&hash_);
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

  int read(uint8_t *data, size_t length) {
    return esp_http_client_read(client_, reinterpret_cast<char *>(data), length);
  }

  bool verify(const NativeRelease &release) {
    remaining_ = release.size;
    expected_ = release.sha256;
    return psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&hash_, PSA_ALG_SHA_256) == PSA_SUCCESS;
  }

  static int read_verified(void *context, uint8_t *data, size_t length) {
    auto &download = *static_cast<Download *>(context);
    const auto count = download.read(data, length);
    if (count <= 0 || static_cast<size_t>(count) > download.remaining_ ||
        psa_hash_update(&download.hash_, data, count) != PSA_SUCCESS)
      return -1;
    download.remaining_ -= count;
    if (download.remaining_ == 0) {
      FirmwareDigest digest{};
      size_t digest_size{};
      if (psa_hash_finish(&download.hash_, digest.data(), digest.size(), &digest_size) != PSA_SUCCESS ||
          digest_size != digest.size() || digest != download.expected_)
        return -1;
    }
    return count;
  }

 private:
  esp_http_client_handle_t client_{};
  psa_hash_operation_t hash_ = PSA_HASH_OPERATION_INIT;
  size_t remaining_{};
  FirmwareDigest expected_{};
};

std::optional<std::vector<NativeRelease>> fetch_releases() {
  const auto url = std::string(feed) + NATIVE_BOARD ".json";
  Download reader;
  const auto length = reader.open(url);
  if (length <= 0 || length > max_release_index_bytes)
    return {};
  std::string json(length, '\0');
  const auto deadline = esp_timer_get_time() + 10 * second;
  for (size_t offset = 0; offset < json.size();) {
    if (esp_timer_get_time() >= deadline)
      return {};
    const auto read = reader.read(reinterpret_cast<uint8_t *>(json.data() + offset), json.size() - offset);
    if (read <= 0)
      return {};
    offset += read;
  }
  return read_release_index(json, NATIVE_BOARD, stable);
}

bool install(const NativeRelease &release) {
  const auto rejected = firmware_boot_status().rejected;
  if (rejected && release.elf_sha256 == rejected->elf_sha256)
    return false;
  const auto url = std::string(feed) + release.version + "/" NATIVE_BOARD "-ota.bin";
  Download reader;
  return reader.open(url) == release.size && reader.verify(release) &&
         stage_firmware(release.size, Download::read_verified, &reader) == FirmwareUpdateResult::Staged;
}

void run(void *context) {
  auto &network = *static_cast<NativeNetwork *>(context);
  const auto installed = *parse_version(esp_app_get_description()->version);
#if NATIVE_LEGACY_REHEARSAL
  NativeReleaseSchedule schedule{10 * second, 5 * second, 12 * second};
#else
  NativeReleaseSchedule schedule;
#endif
  uint32_t preferences_revision = Settings::storage_revision();
  int64_t refresh_preferences = 0;
  while (true) {
    const auto now = esp_timer_get_time();
    const auto revision = Settings::storage_revision();
    if (now >= refresh_preferences || revision != preferences_revision) {
      Settings prefs;
      const bool enabled = prefs.read() && prefs.stock_update_preferences.auto_update.value_or(true);
      schedule.configure(now, enabled, prefs.stock_update_preferences.frequency.value_or("Daily"));
      preferences_revision = revision;
      refresh_preferences = now + 15 * second;
    }
    const bool automatic = schedule.due(now);
    bool requested;
    {
      const std::lock_guard lock(mutex);
      requested = pending.has_value();
    }
    if (!requested && !automatic) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    if (!network.station_lease() || !clock_time_is_valid(std::time(nullptr))
#if NATIVE_LEGACY_MIGRATION
        || !firmware_boot_status().boot_verified
#endif
    ) {
      network.request_time_sync();
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }

    std::optional<std::string> request;
    {
      const std::lock_guard lock(mutex);
      busy = true;
      request = std::exchange(pending, std::nullopt);
      status = request && !request->empty() ? "installing" : "checking";
    }
    const bool manual_install = request && !request->empty();

    std::optional<NativeRelease> image;
    const char *result = "unavailable";
    if (manual_install) {
      const std::lock_guard lock(mutex);
      const auto match = std::ranges::find(releases, *request, &NativeRelease::version);
      if (match != releases.end() && parse_version(match->version) != installed)
        image = *match;
    } else if (auto index = fetch_releases()) {
      if (!request && !index->empty() && parse_version(index->front().version) > installed)
        image = index->front();
      const std::lock_guard lock(mutex);
      releases = std::move(*index);
      result = "ready";
    }
    if (image && !request) {
      Settings prefs;
      if (!prefs.read() || !prefs.stock_update_preferences.auto_update.value_or(true))
        image.reset();
    }
    if (image) {
      {
        const std::lock_guard lock(mutex);
        status = "installing";
      }
      if (install(*image)) {
        {
          const std::lock_guard lock(mutex);
          status = "restarting";
        }
        vTaskDelay(pdMS_TO_TICKS(750));
        esp_restart();
      }
      result = "failed";
    }
    {
      const std::lock_guard lock(mutex);
      busy = false;
      status = result;
    }
    if (automatic && !request)
      schedule.checked(esp_timer_get_time(), std::string_view(result) != "unavailable"
#if NATIVE_LEGACY_MIGRATION
                       && std::string_view(result) != "failed"
#endif
      );
  }
}

}  // namespace

void start_native_release_updates(NativeNetwork &network) {
  if (!parse_version(esp_app_get_description()->version) || !can_update_firmware())
    return;
  const std::lock_guard lock(mutex);
  if (xTaskCreate(run, "native-release", 12288, &network, 1, &worker) != pdPASS)
    ESP_LOGW(tag, "Could not start the release updater");
}

cJSON *native_release_status() {
  const std::lock_guard lock(mutex);
  auto *document = cJSON_CreateObject();
  if (!document)
    return nullptr;
  auto *versions = cJSON_AddArrayToObject(document, "versions");
  if (!versions || !cJSON_AddBoolToObject(document, "available", worker != nullptr) ||
      !cJSON_AddStringToObject(document, "current", esp_app_get_description()->version) ||
      !cJSON_AddStringToObject(document, "status", pending ? (pending->empty() ? "checking" : "installing") : status)) {
    cJSON_Delete(document);
    return nullptr;
  }
  for (const auto &release : releases) {
    auto *version = cJSON_CreateString(release.version.c_str());
    if (!version || !cJSON_AddItemToArray(versions, version)) {
      cJSON_Delete(version);
      cJSON_Delete(document);
      return nullptr;
    }
  }
  return document;
}

bool request_native_release(std::string_view version) {
  const std::lock_guard lock(mutex);
  if (!worker || busy || pending ||
      (!version.empty() && std::ranges::find(releases, version, &NativeRelease::version) == releases.end()))
    return false;
  pending = version;
  xTaskNotifyGive(worker);
  return true;
}

}  // namespace espframe::native
