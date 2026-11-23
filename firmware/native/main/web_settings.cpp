#include "web_settings.h"
#include "configuration_contract_generated.h"
#include "firmware_update.h"
#include "frame_identity.h"
#include "frame_status.h"
#include "native_configuration.h"
#include "native_network.h"
#include "screen_control.h"

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
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>

namespace espframe::native {
namespace {

constexpr const char *tag = "web-settings";
constexpr size_t auth_capacity = 120;
namespace contract = esphome::espframe::contract;
httpd_handle_t server{};
esp_timer_handle_t restart_timer{};
std::optional<ResetMode> resetting;
std::string authorization;

extern const char page_start[] asm("_binary_settings_html_start");
extern const char page_end[] asm("_binary_settings_html_end");
extern const char shared_page_start[] asm("_binary_native_settings_html_start");
extern const char shared_page_end[] asm("_binary_native_settings_html_end");
extern const char shared_app_start[] asm("_binary_shared_settings_js_start");
extern const char shared_app_end[] asm("_binary_shared_settings_js_end");
extern const char shared_style_start[] asm("_binary_style_css_start");
extern const char shared_style_end[] asm("_binary_style_css_end");
extern const char native_style_start[] asm("_binary_native_settings_css_start");
extern const char native_style_end[] asm("_binary_native_settings_css_end");
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

uint32_t socket_ipv4(const sockaddr_storage &address, socklen_t length) {
  if (address.ss_family == AF_INET && length >= sizeof(sockaddr_in))
    return reinterpret_cast<const sockaddr_in &>(address).sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
  if (address.ss_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
    // The dual-stack HTTP listener represents IPv4 connections as ::ffff:a.b.c.d.
    constexpr uint8_t mapped_prefix[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    const auto &bytes = reinterpret_cast<const sockaddr_in6 &>(address).sin6_addr.s6_addr;
    if (std::memcmp(bytes, mapped_prefix, sizeof(mapped_prefix)) == 0) {
      uint32_t ipv4;
      std::memcpy(&ipv4, bytes + sizeof(mapped_prefix), sizeof(ipv4));
      return ipv4;
    }
  }
#endif
  return 0;
}

uint32_t local_address(httpd_req_t *request) {
  sockaddr_storage address{};
  socklen_t length = sizeof(address);
  if (getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&address), &length) != 0)
    return 0;
  return socket_ipv4(address, length);
}

bool hotspot_socket(httpd_req_t *request) {
  return network(request).recovery_status() != NativeNetwork::RecoveryStatus::Off &&
         local_address(request) == inet_addr(recovery_host);
}

bool header_is(httpd_req_t *request, const char *name, std::string_view value) {
  char header[256]{};
  return httpd_req_get_hdr_value_str(request, name, header, sizeof(header)) == ESP_OK && value == header;
}

bool same_origin(httpd_req_t *request) {
  char host[248]{};
  return httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) == ESP_OK && host[0] != '\0' &&
         header_is(request, "Origin", std::string("http://") + host);
}

bool from_hotspot_client(httpd_req_t *request) {
  sockaddr_storage peer{};
  socklen_t length = sizeof(peer);
  if (getpeername(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&peer), &length) != 0)
    return false;
  const auto ipv4 = socket_ipv4(peer, length);
  if (!ipv4)
    return false;

  wifi_sta_list_t associated{};
  wifi_sta_mac_ip_list_t addresses{};
  if (esp_wifi_ap_get_sta_list(&associated) != ESP_OK || esp_wifi_ap_get_sta_list_with_ip(&associated, &addresses) != ESP_OK)
    return false;
  for (int i = 0; i < addresses.num; ++i) {
    if (addresses.sta[i].ip.addr == ipv4)
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
  return serve(request, "text/html; charset=utf-8", shared_page_start, shared_page_end);
}
esp_err_t device_tools(httpd_req_t *request) {
  return serve(request, "text/html; charset=utf-8", page_start, page_end);
}
esp_err_t shared_app(httpd_req_t *request) { return serve(request, "text/javascript", shared_app_start, shared_app_end); }
esp_err_t shared_style(httpd_req_t *request) { return serve(request, "text/css", shared_style_start, shared_style_end); }
esp_err_t native_style(httpd_req_t *request) { return serve(request, "text/css", native_style_start, native_style_end); }
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
  const auto address = wifi.address();
  const auto ip = esp_ip4_addr_t{address.ipv4};
  httpd_resp_set_type(request, "application/json");
  if (status != Status::Joining && ip.addr) {
    char body[96]{};
    const char *web_settings = address.web == NativeNetwork::WebStatus::Disabled ? "false" : "true";
    std::snprintf(body, sizeof(body), "{\"status\":\"connected\",\"ipv4\":\"" IPSTR "\",\"web_settings\":%s}",
                  IP2STR(&ip), web_settings);
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
      !settings.add_playback_fields(document.get()) ||
      !settings.add_stock_update_preferences(document.get()) ||
      !cJSON_AddBoolToObject(document.get(), "live_photo_settings", true) ||
      !cJSON_AddBoolToObject(document.get(), "api_key_configured", settings.has_immich()) ||
      !cJSON_AddNumberToObject(document.get(), "reset_epoch", settings.reset_epoch) ||
      !cJSON_AddStringToObject(document.get(), "reset_path", contract::RESET_PATH) ||
      !cJSON_AddBoolToObject(document.get(), "factory_reset", true) ||
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
  bool restart_required = false;
  if (std::memchr(body.get(), '\0', received) || !Settings::save_playback(body.get(), &restart_required))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Check the Immich connection and playback settings.");
  if (restart_required && esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Saved. Restart the frame to apply settings.");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, restart_required ? "{\"saved\":true,\"restart_required\":true}"
                                                     : "{\"saved\":true,\"restart_required\":false}");
}

esp_err_t send_configuration(httpd_req_t *request, const cJSON *document, const char *status = "200 OK") {
  const std::unique_ptr<char, decltype(&cJSON_free)> body(document ? cJSON_PrintUnformatted(document) : nullptr, cJSON_free);
  if (!body)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to load configuration.");
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, body.get());
}

esp_err_t capabilities(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(configuration_capabilities(), cJSON_Delete);
  return send_configuration(request, document.get());
}

esp_err_t configuration(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  Settings settings;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(
      settings.read() ? configuration_snapshot(settings) : nullptr, cJSON_Delete);
  return send_configuration(request, document.get());
}

esp_err_t status(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  // Observe completion before reading the state the player published for it.
  const bool screen_pending = screen_control.pending();
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(
      frame_status_snapshot(frame_status.read(), network(request).photo_connection() != 0,
                            esp_timer_is_active(restart_timer), screen_pending), cJSON_Delete);
  return send_configuration(request, document.get());
}

esp_err_t configuration_reply(httpd_req_t *request, const ConfigurationUpdate &result) {
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
  if (!body ||
      (result.error && (!cJSON_AddStringToObject(body.get(), "error", result.error) ||
                        (!result.field.empty() && !cJSON_AddStringToObject(body.get(), "field", result.field.c_str())))) ||
      (!result.error && (!cJSON_AddNumberToObject(body.get(), "api_version", contract::API_VERSION) ||
                         !cJSON_AddStringToObject(body.get(), "status", "accepted") ||
                         !cJSON_AddNumberToObject(body.get(), "updated", result.updated) ||
                         !cJSON_AddBoolToObject(body.get(), "restart_required", result.restart_required))))
    return send_configuration(request, nullptr);
  if (result.frame_name && !cJSON_AddStringToObject(body.get(), "frame_hostname",
                                                   network(request).identity_for(*result.frame_name).hostname.c_str()))
    return send_configuration(request, nullptr);
  return send_configuration(request, body.get(), result.status);
}

esp_err_t identity(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (request->method == HTTP_POST) {
    if (!same_origin(request))
      return configuration_reply(request, {"403 Forbidden", "invalid_origin", {}});
    if (esp_timer_is_active(restart_timer))
      return configuration_reply(request, {"409 Conflict", "operation_in_progress", {}});
    std::array<char, 3 * 256 + sizeof("name=") - 1> form{};
    if (!header_is(request, "Content-Type", "application/x-www-form-urlencoded") ||
        request->content_len == 0 || request->content_len > form.size())
      return configuration_reply(request, {"400 Bad Request", "invalid_name", {}});
    size_t received = 0;
    while (received < request->content_len) {
      const int bytes = httpd_req_recv(request, form.data() + received, request->content_len - received);
      if (bytes <= 0)
        return ESP_FAIL;
      received += bytes;
    }
    const auto name = decode_identity_form({form.data(), received});
    if (!name)
      return configuration_reply(request, {"400 Bad Request", "invalid_name", {}});
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> update(cJSON_CreateObject(), cJSON_Delete);
    if (!update || !cJSON_AddStringToObject(update.get(), "frame_name", name->c_str()))
      return configuration_reply(request, {"500 Internal Server Error", "settings_unavailable", {}});
    const auto result = Settings::update_playback(update.get());
    if (result != Settings::UpdateResult::Saved && result != Settings::UpdateResult::Unchanged)
      return configuration_reply(request, {"500 Internal Server Error", "settings_unavailable", {}});
  }
  Settings settings;
  if (!settings.read())
    return send_configuration(request, nullptr);
  auto &wifi = network(request);
  const auto saved = wifi.identity_for(settings.frame_name);
  const esp_ip4_addr_t ip{wifi.address().ipv4};
  char address[16]{};
  if (ip.addr)
    std::snprintf(address, sizeof(address), IPSTR, IP2STR(&ip));
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
  if (!body || !cJSON_AddStringToObject(body.get(), "name", saved.name.c_str()) ||
      !cJSON_AddStringToObject(body.get(), "friendly_name", saved.friendly_name.c_str()) ||
      !cJSON_AddStringToObject(body.get(), "hostname", saved.hostname.c_str()) ||
      (!saved.mac_suffix.empty() && !cJSON_AddStringToObject(body.get(), "mac_suffix", saved.mac_suffix.c_str())) ||
      !cJSON_AddStringToObject(body.get(), "ip_address", address) ||
      !cJSON_AddBoolToObject(body.get(), "restart_required", saved.name != wifi.running_identity().name))
    return send_configuration(request, nullptr);
  return send_configuration(request, body.get());
}

esp_err_t change_screen(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (!same_origin(request))
    return configuration_reply(request, {"403 Forbidden", "invalid_origin", {}});
  std::array<char, sizeof("action=sleep") - 1> form{};
  if (!header_is(request, "Content-Type", "application/x-www-form-urlencoded") ||
      request->content_len == 0 || request->content_len > form.size())
    return configuration_reply(request, {"400 Bad Request", "invalid_action", {}});

  size_t received = 0;
  while (received < request->content_len) {
    const int bytes = httpd_req_recv(request, form.data() + received, request->content_len - received);
    if (bytes <= 0)
      return ESP_FAIL;
    received += bytes;
  }
  const auto action = screen_action({form.data(), received});
  if (action == ScreenAction::Idle)
    return configuration_reply(request, {"400 Bad Request", "invalid_action", {}});
  if (esp_timer_is_active(restart_timer) || frame_status.read().screen == ScreenState::Starting ||
      !screen_control.submit(action))
    return configuration_reply(request, {"409 Conflict", "operation_in_progress", {}});
  httpd_resp_set_status(request, "202 Accepted");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, "{\"status\":\"accepted\"}");
}

esp_err_t save_configuration(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (!same_origin(request))
    return configuration_reply(request, {"403 Forbidden", "invalid_origin", {}});
  if (esp_timer_is_active(restart_timer))
    return configuration_reply(request, {"409 Conflict", "update_in_progress", {}});
  if (!header_is(request, "Content-Type", "application/x-www-form-urlencoded") ||
      request->content_len == 0 || request->content_len > 3 * Settings::max_json_size + sizeof("configuration=") - 1)
    return configuration_reply(request, {"400 Bad Request", "missing_configuration", {}});

  std::string form(request->content_len, '\0');
  size_t received = 0;
  while (received < form.size()) {
    const int bytes = httpd_req_recv(request, form.data() + received, form.size() - received);
    if (bytes <= 0)
      return ESP_FAIL;
    received += bytes;
  }
  const auto json = decode_configuration_form(form);
  if (!json)
    return configuration_reply(request, {"400 Bad Request", "invalid_configuration", {}});
  const auto result = update_configuration(json->c_str());
  if (!result.error && result.restart_required && esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
    return configuration_reply(request, {"500 Internal Server Error", "restart_required", {}});
  return configuration_reply(request, result);
}

esp_err_t reset_status(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  Settings settings;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
  if (!settings.read() || !body || !cJSON_AddNumberToObject(body.get(), "epoch", settings.reset_epoch) ||
      !cJSON_AddStringToObject(body.get(), "status", resetting ? "pending" : "ready") ||
      !cJSON_AddStringToObject(body.get(), "mode", resetting ? reset_mode_name(*resetting) : "none"))
    return send_configuration(request, nullptr);
  return send_configuration(request, body.get());
}

esp_err_t reset_settings(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (!same_origin(request))
    return configuration_reply(request, {"403 Forbidden", "invalid_origin", {}});
  if (esp_timer_is_active(restart_timer) || network(request).busy())
    return configuration_reply(request, {"409 Conflict", "operation_in_progress", {}});

  std::array<char, sizeof("mode=customization") - 1> form{};
  if (!header_is(request, "Content-Type", "application/x-www-form-urlencoded") ||
      request->content_len == 0 || request->content_len > form.size())
    return configuration_reply(request, {"400 Bad Request", "invalid_mode", {}});
  size_t received = 0;
  while (received < request->content_len) {
    const int bytes = httpd_req_recv(request, form.data() + received, request->content_len - received);
    if (bytes <= 0)
      return ESP_FAIL;
    received += bytes;
  }
  const auto mode = decode_reset_form({form.data(), received});
  if (!mode)
    return configuration_reply(request, {"400 Bad Request", "invalid_mode", {}});
  const auto result = reset_configuration(*mode);
  if (!result.error) {
    if (esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
      return configuration_reply(request, {"500 Internal Server Error", "restart_required", {}});
    resetting = mode;
  }
  return configuration_reply(request, result);
}

int receive_firmware(void *context, uint8_t *buffer, size_t length) {
  return httpd_req_recv(static_cast<httpd_req_t *>(context), reinterpret_cast<char *>(buffer), length);
}

std::array<char, 2 * FirmwareImage{}.elf_sha256.size() + 1> firmware_hash(const FirmwareImage &image) {
  constexpr char digits[] = "0123456789abcdef";
  std::array<char, 2 * FirmwareImage{}.elf_sha256.size() + 1> hash{};
  for (size_t index = 0; index < image.elf_sha256.size(); ++index) {
    const auto byte = image.elf_sha256[index];
    hash[2 * index] = digits[byte >> 4];
    hash[2 * index + 1] = digits[byte & 15];
  }
  return hash;
}

bool add_firmware_image(cJSON *document, const char *key, const FirmwareImage &image) {
  const auto hash = firmware_hash(image);
  auto *object = cJSON_AddObjectToObject(document, key);
  return object && cJSON_AddNumberToObject(object, "offset", image.offset) &&
         cJSON_AddStringToObject(object, "elf_sha256", hash.data());
}

esp_err_t firmware_status(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  const auto status = firmware_boot_status();
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(cJSON_CreateObject(), cJSON_Delete);
  if (!document || !cJSON_AddBoolToObject(document.get(), "boot_verified", status.boot_verified) ||
      (is_firmware_test_build() && !cJSON_AddBoolToObject(document.get(), "ota_test_build", true)) ||
      (status.running && !add_firmware_image(document.get(), "running", *status.running)) ||
      (status.rejected && !add_firmware_image(document.get(), "rejected", *status.rejected)))
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to read firmware status.");
  return send_configuration(request, document.get());
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
  FirmwareImage staged;
  switch (stage_firmware(request->content_len, receive_firmware, request, &staged)) {
    case Result::Staged: {
      if (esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "The update is ready. Restart the frame to boot it.");
      const auto hash = firmware_hash(staged);
      char body[128]{};
      std::snprintf(body, sizeof(body), "{\"staged\":true,\"target\":{\"offset\":%" PRIu32
                    ",\"elf_sha256\":\"%s\"}}", staged.offset, hash.data());
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(request, body);
    }
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
  config.recv_wait_timeout = 3;

  const httpd_uri_t routes[]{
      {.uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = &network},
      {.uri = "/settings", .method = HTTP_GET, .handler = device_tools, .user_ctx = &network},
      {.uri = "/espframe/app.js", .method = HTTP_GET, .handler = shared_app, .user_ctx = &network},
      {.uri = "/espframe/style.css", .method = HTTP_GET, .handler = shared_style, .user_ctx = &network},
      {.uri = "/espframe/native.css", .method = HTTP_GET, .handler = native_style, .user_ctx = &network},
      {.uri = "/settings.js", .method = HTTP_GET, .handler = script, .user_ctx = &network},
      {.uri = "/photo_source.mjs", .method = HTTP_GET, .handler = source_script, .user_ctx = &network},
      {.uri = "/backup.mjs", .method = HTTP_GET, .handler = backup_script, .user_ctx = &network},
      {.uri = "/settings.css", .method = HTTP_GET, .handler = style, .user_ctx = &network},
      {.uri = "/api/playback", .method = HTTP_GET, .handler = current, .user_ctx = &network},
      {.uri = "/api/playback", .method = HTTP_POST, .handler = update, .user_ctx = &network},
      {.uri = contract::CAPABILITIES_PATH, .method = HTTP_GET, .handler = capabilities, .user_ctx = &network},
      {.uri = frame_status_path, .method = HTTP_GET, .handler = status, .user_ctx = &network},
      {.uri = frame_identity_path, .method = HTTP_GET, .handler = identity, .user_ctx = &network},
      {.uri = frame_identity_path, .method = HTTP_POST, .handler = identity, .user_ctx = &network},
      {.uri = screen_control_path, .method = HTTP_POST, .handler = change_screen, .user_ctx = &network},
      {.uri = contract::CONFIGURATION_PATH, .method = HTTP_GET, .handler = configuration, .user_ctx = &network},
      {.uri = contract::CONFIGURATION_PATH, .method = HTTP_POST, .handler = save_configuration, .user_ctx = &network},
      {.uri = contract::RESET_PATH, .method = HTTP_GET, .handler = reset_status, .user_ctx = &network},
      {.uri = contract::RESET_PATH, .method = HTTP_POST, .handler = reset_settings, .user_ctx = &network},
      {.uri = "/api/firmware", .method = HTTP_GET, .handler = firmware_status, .user_ctx = &network},
      {.uri = "/api/firmware", .method = HTTP_POST, .handler = update_firmware, .user_ctx = &network},
      {.uri = "/recovery.js", .method = HTTP_GET, .handler = recovery_script, .user_ctx = &network},
      {.uri = "/api/recovery", .method = HTTP_GET, .handler = recovery_status, .user_ctx = &network},
      {.uri = "/api/recovery", .method = HTTP_POST, .handler = recover_wifi, .user_ctx = &network},
  };
  config.max_uri_handlers = std::size(routes);
  if (httpd_start(&server, &config) != ESP_OK) {
    esp_timer_delete(restart_timer);
    return false;
  }
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
