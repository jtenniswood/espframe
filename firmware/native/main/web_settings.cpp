#include "web_settings.h"
#include "firmware_update.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"

#include <array>
#include <cstring>
#include <memory>
#include <string_view>

namespace espframe::native {
namespace {

constexpr const char *tag = "web-settings";
constexpr size_t auth_capacity = 120;
httpd_handle_t server{};
esp_timer_handle_t restart_timer{};
std::string authorization;

extern const char page_start[] asm("_binary_settings_html_start");
extern const char page_end[] asm("_binary_settings_html_end");
extern const char script_start[] asm("_binary_settings_js_start");
extern const char script_end[] asm("_binary_settings_js_end");
extern const char source_start[] asm("_binary_photo_source_mjs_start");
extern const char source_end[] asm("_binary_photo_source_mjs_end");
extern const char backup_start[] asm("_binary_backup_mjs_start");
extern const char backup_end[] asm("_binary_backup_mjs_end");
extern const char style_start[] asm("_binary_setup_css_start");
extern const char style_end[] asm("_binary_setup_css_end");

bool authorize(httpd_req_t *request) {
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; form-action 'none'");
  std::array<char, auth_capacity> header{};
  if (httpd_req_get_hdr_value_str(request, "Authorization", header.data(), header.size()) == ESP_OK &&
      std::strlen(header.data()) == authorization.size()) {
    unsigned difference = 0;
    for (size_t index = 0; index < authorization.size(); ++index)
      difference |= header[index] ^ authorization[index];
    if (difference == 0)
      return true;
  }
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"Espframe settings\", charset=\"UTF-8\"");
  httpd_resp_sendstr(request, "Enter the Espframe settings password.");
  return false;
}

esp_err_t serve(httpd_req_t *request, const char *type, const char *start, const char *end) {
  if (!authorize(request))
    return ESP_OK;
  httpd_resp_set_type(request, type);
  return httpd_resp_send(request, start, end - start - 1);
}

esp_err_t page(httpd_req_t *request) { return serve(request, "text/html; charset=utf-8", page_start, page_end); }
esp_err_t script(httpd_req_t *request) { return serve(request, "text/javascript", script_start, script_end); }
esp_err_t source_script(httpd_req_t *request) { return serve(request, "text/javascript", source_start, source_end); }
esp_err_t backup_script(httpd_req_t *request) { return serve(request, "text/javascript", backup_start, backup_end); }
esp_err_t style(httpd_req_t *request) { return serve(request, "text/css", style_start, style_end); }

esp_err_t current(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  Settings settings;
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(cJSON_CreateObject(), cJSON_Delete);
  if (!settings.read() || !document ||
      !cJSON_AddStringToObject(document.get(), "immich_url", settings.immich_url.c_str()) ||
      !cJSON_AddNumberToObject(document.get(), "slide_seconds", settings.slide_seconds) ||
      !cJSON_AddNumberToObject(document.get(), "brightness_day", settings.brightness_day) ||
      !cJSON_AddBoolToObject(document.get(), "schedule_enabled", settings.schedule.enabled) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_on_hour", settings.schedule.on_hour) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_off_hour", settings.schedule.off_hour) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_wake_timeout", settings.schedule.wake_seconds) ||
      !cJSON_AddBoolToObject(document.get(), "portrait_pairing", settings.portrait_pairing) ||
      !cJSON_AddStringToObject(document.get(), "display_mode", display_mode_name(settings.display_mode)) ||
      !cJSON_AddStringToObject(document.get(), "photo_source", photo_source_name(settings.photo_source.mode)) ||
      !cJSON_AddStringToObject(document.get(), "album_id", settings.photo_source.album_id.c_str()) ||
      !cJSON_AddBoolToObject(document.get(), "show_clock", settings.clock.show) ||
      !cJSON_AddStringToObject(document.get(), "clock_format", settings.clock.format.c_str()) ||
      !cJSON_AddStringToObject(document.get(), "timezone", settings.clock.timezone.c_str()) ||
      !cJSON_AddBoolToObject(document.get(), "firmware_update", can_update_firmware()) ||
      !cJSON_AddStringToObject(document.get(), "board_revision", NATIVE_BOARD))
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to load settings.");
  auto *zones = cJSON_AddObjectToObject(document.get(), "clock_timezones");
  if (!zones)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to load timezones.");
  for (const auto &zone : clock_zones()) {
    if (!cJSON_AddStringToObject(zones, zone.option, zone.label))
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to load timezones.");
  }
  std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(document.get()), cJSON_free);
  if (!body)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to load settings.");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, body.get());
}

esp_err_t update(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  char mime[40]{};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", mime, sizeof(mime)) != ESP_OK ||
      std::string_view(mime) != "application/json" ||
      request->content_len == 0 || request->content_len > Settings::max_json_size)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected playback settings as JSON.");
  if (esp_timer_is_active(restart_timer)) {
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "The frame is restarting.");
  }

  auto body = std::make_unique<char[]>(request->content_len + 1);
  size_t received = 0;
  while (received < request->content_len) {
    const int bytes = httpd_req_recv(request, body.get() + received, request->content_len - received);
    if (bytes <= 0)
      return ESP_FAIL;
    received += bytes;
  }
  body[received] = '\0';
  if (std::memchr(body.get(), '\0', received) || !Settings::save_playback(body.get()))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Check the Immich connection and playback settings.");
  if (esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Saved. Restart the frame to apply settings.");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, "{\"saved\":true}");
}

int receive_firmware(void *context, uint8_t *buffer, size_t length) {
  return httpd_req_recv(static_cast<httpd_req_t *>(context), reinterpret_cast<char *>(buffer), length);
}

esp_err_t update_firmware(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  char mime[40]{};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", mime, sizeof(mime)) != ESP_OK ||
      std::string_view(mime) != "application/octet-stream")
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose a native firmware update image.");
  if (esp_timer_is_active(restart_timer)) {
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "The frame is restarting.");
  }
  using Result = FirmwareUpdateResult;
  switch (stage_firmware(request->content_len, receive_firmware, request)) {
    case Result::Staged:
      if (esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "The update is ready. Restart the frame to boot it.");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(request, "{\"staged\":true}");
    case Result::Unavailable:
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Install an update-capable factory image over USB first.");
    case Result::RestartPending:
      httpd_resp_set_status(request, "409 Conflict");
      return httpd_resp_sendstr(request, "An update is waiting for restart or startup checks.");
    case Result::Size:
    case Result::WrongBoard:
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose the native OTA image for this Guition revision.");
    case Result::Interrupted:
      return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "The firmware transfer was interrupted.");
    case Result::Invalid:
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "The firmware image failed validation.");
    case Result::WriteFailed:
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not stage the firmware update.");
  }
  return ESP_FAIL;
}

void restart(void *) { esp_restart(); }

}  // namespace

void start_web_settings(const Settings &settings) {
  if (server || settings.web_password.empty())
    return;
  const std::string credentials = "espframe:" + settings.web_password;
  std::array<unsigned char, auth_capacity> base64{};
  size_t size = 0;
  if (mbedtls_base64_encode(base64.data(), base64.size(), &size,
                            reinterpret_cast<const unsigned char *>(credentials.data()), credentials.size()) != 0)
    return;
  authorization = "Basic ";
  authorization.append(reinterpret_cast<const char *>(base64.data()), size);

  esp_timer_create_args_t timer{};
  timer.callback = restart;
  timer.name = "web-restart";
  if (esp_timer_create(&timer, &restart_timer) != ESP_OK)
    return;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.stack_size = 6144;
  config.max_open_sockets = 4;
  config.recv_wait_timeout = 3;
  if (httpd_start(&server, &config) != ESP_OK) {
    esp_timer_delete(restart_timer);
    return;
  }

  const httpd_uri_t routes[]{
      {.uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = nullptr},
      {.uri = "/settings.js", .method = HTTP_GET, .handler = script, .user_ctx = nullptr},
      {.uri = "/photo_source.mjs", .method = HTTP_GET, .handler = source_script, .user_ctx = nullptr},
      {.uri = "/backup.mjs", .method = HTTP_GET, .handler = backup_script, .user_ctx = nullptr},
      {.uri = "/settings.css", .method = HTTP_GET, .handler = style, .user_ctx = nullptr},
      {.uri = "/api/playback", .method = HTTP_GET, .handler = current, .user_ctx = nullptr},
      {.uri = "/api/playback", .method = HTTP_POST, .handler = update, .user_ctx = nullptr},
      {.uri = "/api/firmware", .method = HTTP_POST, .handler = update_firmware, .user_ctx = nullptr},
  };
  for (const auto &route : routes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server);
      server = nullptr;
      esp_timer_delete(restart_timer);
      return;
    }
  }

  esp_netif_ip_info_t ip{};
  if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip) == ESP_OK)
    ESP_LOGI(tag, "Settings page listening at http://" IPSTR "/", IP2STR(&ip.ip));
}

}  // namespace espframe::native
