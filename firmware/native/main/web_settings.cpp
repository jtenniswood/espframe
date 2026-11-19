#include "web_settings.h"
#include "firmware_update.h"
#include "native_network.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

#include <array>
#include <cstdio>
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
extern const char recovery_start[] asm("_binary_recovery_html_start");
extern const char recovery_end[] asm("_binary_recovery_html_end");
extern const char recovery_script_start[] asm("_binary_recovery_js_start");
extern const char recovery_script_end[] asm("_binary_recovery_js_end");

void set_response_headers(httpd_req_t *request) {
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; form-action 'none'");
}

NativeNetwork &network(httpd_req_t *request) { return *static_cast<NativeNetwork *>(request->user_ctx); }

uint32_t local_address(httpd_req_t *request) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&address), &length) != 0 ||
      address.sin_family != AF_INET)
    return 0;
  return address.sin_addr.s_addr;
}

bool hotspot_socket(httpd_req_t *request) {
  return network(request).recovery_status() != NativeNetwork::RecoveryStatus::Off &&
         local_address(request) == inet_addr(recovery_host);
}

bool header_is(httpd_req_t *request, const char *name, std::string_view value) {
  char header[64]{};
  return httpd_req_get_hdr_value_str(request, name, header, sizeof(header)) == ESP_OK && value == header;
}

bool from_hotspot_client(httpd_req_t *request) {
  sockaddr_in peer{};
  socklen_t length = sizeof(peer);
  if (getpeername(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&peer), &length) != 0 ||
      peer.sin_family != AF_INET)
    return false;

  wifi_sta_list_t associated{};
  wifi_sta_mac_ip_list_t addresses{};
  if (esp_wifi_ap_get_sta_list(&associated) != ESP_OK || esp_wifi_ap_get_sta_list_with_ip(&associated, &addresses) != ESP_OK)
    return false;
  for (int i = 0; i < addresses.num; ++i) {
    if (addresses.sta[i].ip.addr == peer.sin_addr.s_addr)
      return true;
  }
  return false;
}

bool authorize(httpd_req_t *request) {
  set_response_headers(request);
  const auto station = network(request).address().ipv4;
  if (!station || local_address(request) != station || hotspot_socket(request) || authorization.empty()) {
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Settings are unavailable on this connection.");
    return false;
  }
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

esp_err_t asset(httpd_req_t *request, const char *type, const char *start, const char *end) {
  httpd_resp_set_type(request, type);
  return httpd_resp_send(request, start, end - start - 1);
}

esp_err_t serve(httpd_req_t *request, const char *type, const char *start, const char *end) {
  return authorize(request) ? asset(request, type, start, end) : ESP_OK;
}

esp_err_t recovery_asset(httpd_req_t *request, const char *type, const char *start, const char *end) {
  set_response_headers(request);
  if (!hotspot_socket(request) || !header_is(request, "Host", recovery_host))
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Open Wi-Fi setup at http://192.168.4.1.");
  return asset(request, type, start, end);
}

esp_err_t page(httpd_req_t *request) {
  if (hotspot_socket(request))
    return recovery_asset(request, "text/html; charset=utf-8", recovery_start, recovery_end);
  return serve(request, "text/html; charset=utf-8", page_start, page_end);
}
esp_err_t script(httpd_req_t *request) { return serve(request, "text/javascript", script_start, script_end); }
esp_err_t source_script(httpd_req_t *request) { return serve(request, "text/javascript", source_start, source_end); }
esp_err_t backup_script(httpd_req_t *request) { return serve(request, "text/javascript", backup_start, backup_end); }
esp_err_t style(httpd_req_t *request) {
  if (hotspot_socket(request))
    return recovery_asset(request, "text/css", style_start, style_end);
  return serve(request, "text/css", style_start, style_end);
}
esp_err_t recovery_script(httpd_req_t *request) {
  return recovery_asset(request, "text/javascript", recovery_script_start, recovery_script_end);
}

esp_err_t recovery_status(httpd_req_t *request) {
  set_response_headers(request);
  if (!hotspot_socket(request) || !header_is(request, "Host", recovery_host))
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "The Wi-Fi setup hotspot is unavailable.");
  using Status = NativeNetwork::RecoveryStatus;
  auto &wifi = network(request);
  const auto status = wifi.recovery_status();
  const auto ip = esp_ip4_addr_t{wifi.address().ipv4};
  httpd_resp_set_type(request, "application/json");
  if (status != Status::Joining && ip.addr) {
    char body[64]{};
    std::snprintf(body, sizeof(body), "{\"status\":\"connected\",\"ipv4\":\"" IPSTR "\"}", IP2STR(&ip));
    return httpd_resp_sendstr(request, body);
  }
  if (status == Status::Saved)
    return httpd_resp_sendstr(request, "{\"status\":\"saved\"}");
  if (status == Status::Joining)
    return httpd_resp_sendstr(request, "{\"status\":\"joining\"}");
  if (status == Status::Failed)
    return httpd_resp_sendstr(request, "{\"status\":\"failed\"}");
  return httpd_resp_sendstr(request, "{\"status\":\"ready\"}");
}

esp_err_t recover_wifi(httpd_req_t *request) {
  set_response_headers(request);
  if (!hotspot_socket(request) || !header_is(request, "Host", recovery_host) ||
      !header_is(request, "Origin", recovery_url) || !from_hotspot_client(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Join the frame's Wi-Fi setup hotspot to continue.");

  std::array<uint8_t, 2 + 32 + 64> body{};
  if (!header_is(request, "Content-Type", "application/octet-stream") ||
      request->content_len == 0 || request->content_len > body.size())
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Enter a valid Wi-Fi network name and password.");
  size_t offset = 0;
  while (offset < request->content_len) {
    const int bytes = httpd_req_recv(request, reinterpret_cast<char *>(body.data() + offset), request->content_len - offset);
    if (bytes <= 0)
      return ESP_FAIL;
    offset += bytes;
  }
  const auto credentials = WifiCredentials::from_length_prefixed({body.data(), offset});
  if (!credentials)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Enter a valid Wi-Fi network name and password.");
  if (esp_timer_is_active(restart_timer) || !network(request).submit_recovery(*credentials)) {
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "The frame is already connecting or restarting.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  return httpd_resp_sendstr(request, "Connecting to Wi-Fi.");
}

esp_err_t current(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  Settings settings;
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(cJSON_CreateObject(), cJSON_Delete);
  if (!settings.read() || !document ||
      !cJSON_AddStringToObject(document.get(), "immich_url", settings.immich_url.c_str()) ||
      !cJSON_AddNumberToObject(document.get(), "slide_seconds", settings.slide_seconds) ||
      !cJSON_AddNumberToObject(document.get(), "brightness_day", settings.brightness_day) ||
      !cJSON_AddNumberToObject(document.get(), "brightness_night", settings.night_brightness()) ||
      !cJSON_AddBoolToObject(document.get(), "schedule_enabled", settings.schedule.enabled) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_on_hour", settings.schedule.on_hour) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_off_hour", settings.schedule.off_hour) ||
      !cJSON_AddNumberToObject(document.get(), "schedule_wake_timeout", settings.schedule.wake_seconds) ||
      !cJSON_AddBoolToObject(document.get(), "portrait_pairing", settings.portrait_pairing) ||
      !cJSON_AddStringToObject(document.get(), "display_mode", display_mode_name(settings.display_mode)) ||
      !cJSON_AddStringToObject(document.get(), "screen_rotation", screen_rotation_name(settings.screen_rotation)) ||
      !cJSON_AddStringToObject(document.get(), "photo_source", photo_source_name(settings.photo_source.mode)) ||
      !cJSON_AddStringToObject(document.get(), "album_id", settings.photo_source.album_id.c_str()) ||
      !cJSON_AddBoolToObject(document.get(), "photo_metadata_date_enabled", settings.metadata.show_date) ||
      !cJSON_AddBoolToObject(document.get(), "photo_metadata_location_enabled", settings.metadata.show_location) ||
      !cJSON_AddStringToObject(document.get(), "photo_metadata_date_format", settings.metadata.date_format()) ||
      !cJSON_AddStringToObject(document.get(), "photo_metadata_date_taken_format", settings.metadata.taken_format()) ||
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

bool start_web_settings(const Settings &settings, NativeNetwork &network) {
  if (server)
    return true;
  if (!settings.web_password.empty()) {
    const std::string credentials = "espframe:" + settings.web_password;
    std::array<unsigned char, auth_capacity> base64{};
    size_t size = 0;
    if (mbedtls_base64_encode(base64.data(), base64.size(), &size,
                              reinterpret_cast<const unsigned char *>(credentials.data()), credentials.size()) != 0)
      return false;
    authorization = "Basic ";
    authorization.append(reinterpret_cast<const char *>(base64.data()), size);
  }

  esp_timer_create_args_t timer{};
  timer.callback = restart;
  timer.name = "web-restart";
  if (esp_timer_create(&timer, &restart_timer) != ESP_OK)
    return false;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.stack_size = 6144;
  config.max_open_sockets = 4;
  config.max_uri_handlers = 11;
  config.recv_wait_timeout = 3;
  if (httpd_start(&server, &config) != ESP_OK) {
    esp_timer_delete(restart_timer);
    return false;
  }

  const httpd_uri_t routes[]{
      {.uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = &network},
      {.uri = "/settings.js", .method = HTTP_GET, .handler = script, .user_ctx = &network},
      {.uri = "/photo_source.mjs", .method = HTTP_GET, .handler = source_script, .user_ctx = &network},
      {.uri = "/backup.mjs", .method = HTTP_GET, .handler = backup_script, .user_ctx = &network},
      {.uri = "/settings.css", .method = HTTP_GET, .handler = style, .user_ctx = &network},
      {.uri = "/api/playback", .method = HTTP_GET, .handler = current, .user_ctx = &network},
      {.uri = "/api/playback", .method = HTTP_POST, .handler = update, .user_ctx = &network},
      {.uri = "/api/firmware", .method = HTTP_POST, .handler = update_firmware, .user_ctx = &network},
      {.uri = "/recovery.js", .method = HTTP_GET, .handler = recovery_script, .user_ctx = &network},
      {.uri = "/api/recovery", .method = HTTP_GET, .handler = recovery_status, .user_ctx = &network},
      {.uri = "/api/recovery", .method = HTTP_POST, .handler = recover_wifi, .user_ctx = &network},
  };
  for (const auto &route : routes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server);
      server = nullptr;
      esp_timer_delete(restart_timer);
      return false;
    }
  }

  esp_netif_ip_info_t ip{};
  if (!settings.web_password.empty() &&
      esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip) == ESP_OK && ip.ip.addr)
    ESP_LOGI(tag, "Settings page listening at http://" IPSTR "/", IP2STR(&ip.ip));
  return true;
}

}  // namespace espframe::native
