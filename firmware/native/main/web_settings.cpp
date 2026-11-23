#include "web_settings.h"
#include "c6_firmware_update.h"
#include "configuration_contract_generated.h"
#include "firmware_update.h"
#include "frame_identity.h"
#include "frame_status.h"
#include "native_configuration.h"
#include "native_network.h"
#include "native_release_updates.h"
#include "screen_control.h"
#include "web_logs.h"
#include "native_wireguard.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

#include <array>
#include <atomic>
#include <charconv>
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
std::atomic<bool> guarded_restart{};
std::optional<ResetMode> resetting;
std::string authorization;

extern const char page_start[] asm("_binary_settings_html_start");
extern const char page_end[] asm("_binary_settings_html_end");
extern const char admin_preview_start[] asm("_binary_admin_preview_html_start");
extern const char admin_preview_end[] asm("_binary_admin_preview_html_end");
extern const char admin_cards_start[] asm("_binary_admin_cards_js_start");
extern const char admin_cards_end[] asm("_binary_admin_cards_js_end");
extern const char admin_app_start[] asm("_binary_app_js_start");
extern const char admin_app_end[] asm("_binary_app_js_end");
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

constexpr auto default_content_policy = "default-src 'self'; frame-ancestors 'none'; form-action 'none'";
constexpr auto shared_admin_content_policy =
    "default-src 'self'; img-src 'self' data:; frame-ancestors 'none'; form-action 'none'";

void set_response_headers(httpd_req_t *request, const char *content_policy = default_content_policy) {
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy", content_policy);
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

bool matches_station_host(httpd_req_t *request, uint32_t station) {
  char header[80]{};
  if (httpd_req_get_hdr_value_str(request, "Host", header, sizeof(header)) != ESP_OK)
    return false;
  std::string_view host(header);
  if (host.ends_with(":80"))
    host.remove_suffix(3);
  char ipv4[INET_ADDRSTRLEN]{};
  const in_addr address{station};
  const auto &hostname = network(request).running_identity().hostname;
  return (inet_ntop(AF_INET, &address, ipv4, sizeof(ipv4)) && host == ipv4) ||
         (!hostname.empty() && host == hostname + ".local");
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

bool authorize(httpd_req_t *request, const char *content_policy = default_content_policy) {
  set_response_headers(request, content_policy);
  const auto station = network(request).address().ipv4;
  if (!station || local_address(request) != station || hotspot_socket(request) ||
      (authorization.empty() && !matches_station_host(request, station))) {
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Settings are unavailable on this connection.");
    return false;
  }
  if (authorization.empty()) {
    if (request->method != HTTP_POST || same_origin(request))
      return true;
    httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open settings on the frame to make changes.");
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

esp_err_t admin_page(httpd_req_t *request) {
  if (!authorize(request, shared_admin_content_policy))
    return ESP_OK;
  return asset(request, "text/html; charset=utf-8", admin_preview_start, admin_preview_end);
}

esp_err_t admin_cards(httpd_req_t *request) {
  return serve(request, "text/javascript", admin_cards_start, admin_cards_end);
}

esp_err_t home_page(httpd_req_t *request) {
  if (hotspot_socket(request))
    return recovery_asset(request, "text/html; charset=utf-8", recovery_start, recovery_end);
  return admin_page(request);
}

esp_err_t native_settings(httpd_req_t *request) {
  return serve(request, "text/html; charset=utf-8", shared_page_start, shared_page_end);
}

esp_err_t admin_app(httpd_req_t *request) {
  return serve(request, "text/javascript", admin_app_start, admin_app_end);
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
  const auto previous_ntp_revision = ntp_settings_revision.load(std::memory_order_relaxed);
  bool restart_required = false;
  if (std::memchr(body.get(), '\0', received) || !Settings::save_playback(body.get(), &restart_required))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Check the Immich connection and playback settings.");
  if (restart_required && esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Saved. Restart the frame to apply settings.");
  if (!restart_required && previous_ntp_revision != ntp_settings_revision.load(std::memory_order_relaxed))
    static_cast<NativeNetwork *>(request->user_ctx)->request_time_sync();
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

esp_err_t wireguard_state(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  const auto state = wireguard_status();
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
  if (!body || !cJSON_AddBoolToObject(body.get(), "running", state.running) ||
      !cJSON_AddBoolToObject(body.get(), "connected", state.connected) ||
      (state.handshake_age && !cJSON_AddNumberToObject(body.get(), "handshake_age", *state.handshake_age)))
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to read WireGuard status.");
  return send_configuration(request, body.get());
}

esp_err_t wifi_firmware(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (request->method == HTTP_GET) {
    const auto status = c6_firmware_status();
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
    if (!body || !cJSON_AddStringToObject(body.get(), "current", status.current.c_str()) ||
        !cJSON_AddStringToObject(body.get(), "latest", status.latest.c_str()) ||
        !cJSON_AddStringToObject(body.get(), "status", status.state))
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to read Wi-Fi firmware status.");
    return send_configuration(request, body.get());
  }
  if (!same_origin(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open settings on the frame to make changes.");
  if (!header_is(request, "Content-Type", "text/plain") ||
      (request->content_len != 5 && request->content_len != 7))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose check or install.");
  std::array<char, 7> data{};
  size_t offset = 0;
  while (offset < request->content_len) {
    const auto bytes = httpd_req_recv(request, data.data() + offset, request->content_len - offset);
    if (bytes <= 0)
      return ESP_FAIL;
    offset += bytes;
  }
  const std::string_view action(data.data(), offset);
  if (action != "check" && action != "install")
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose check or install.");
  if (esp_timer_is_active(restart_timer) || network(request).busy() ||
      !request_c6_firmware_update(action == "install")) {
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "The frame is busy.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  return httpd_resp_sendstr(request, "Wi-Fi firmware request accepted.");
}

esp_err_t configuration(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  static const auto boot = esp_random();
  const auto revision = Settings::storage_revision();
  const auto search_api = active_immich_search_api.load(std::memory_order_relaxed);
  const auto solar_times = frame_status.solar_times();
  char etag[44]{};
  std::snprintf(etag, sizeof(etag), "W/\"%08" PRIx32 "-%08" PRIx32 "-%u-%d-%d\"", boot, revision,
                static_cast<unsigned>(search_api), solar_times ? solar_times->sunrise : -1,
                solar_times ? solar_times->sunset : -1);
  if (header_is(request, "If-None-Match", etag)) {
    httpd_resp_set_hdr(request, "ETag", etag);
    httpd_resp_set_status(request, "304 Not Modified");
    return httpd_resp_send(request, nullptr, 0);
  }
  Settings settings;
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(
      settings.read() ? configuration_snapshot(settings, solar_times) : nullptr, cJSON_Delete);
  if (document)
    httpd_resp_set_hdr(request, "ETag", etag);
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

esp_err_t logs(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;

  uint32_t after = 0;
  if (const auto length = httpd_req_get_url_query_len(request)) {
    constexpr std::string_view prefix = "after=";
    std::array<char, prefix.size() + 10 + 1> query{};
    if (length >= query.size() || httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK ||
        !std::string_view(query.data(), length).starts_with(prefix))
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid log position.");
    const auto end = query.data() + length;
    const auto parsed = std::from_chars(query.data() + prefix.size(), end, after);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid log position.");
  }

  std::array<WebLogLine, 12> lines{};
  const auto page = read_web_logs(after, lines);
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> body(cJSON_CreateObject(), cJSON_Delete);
  if (!body || !cJSON_AddNumberToObject(body.get(), "boot", page.boot) ||
      !cJSON_AddBoolToObject(body.get(), "more", page.more))
    return send_configuration(request, nullptr);
  auto *entries = cJSON_AddArrayToObject(body.get(), "lines");
  if (!entries)
    return send_configuration(request, nullptr);
  for (size_t i = 0; i < page.count; ++i) {
    const auto &line = lines[i];
    auto *entry = cJSON_CreateObject();
    if (!entry || !cJSON_AddItemToArray(entries, entry)) {
      cJSON_Delete(entry);
      return send_configuration(request, nullptr);
    }
    if (!cJSON_AddNumberToObject(entry, "id", line.id) || !cJSON_AddNumberToObject(entry, "level", line.level) ||
        !cJSON_AddStringToObject(entry, "message", line.message.data()))
      return send_configuration(request, nullptr);
  }
  return send_configuration(request, body.get());
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
  const auto previous_ntp_revision = ntp_settings_revision.load(std::memory_order_relaxed);
  const auto result = update_configuration(json->c_str());
  if (!result.error && result.restart_required && esp_timer_start_once(restart_timer, 800'000) != ESP_OK)
    return configuration_reply(request, {"500 Internal Server Error", "restart_required", {}});
  if (!result.error && !result.restart_required &&
      previous_ntp_revision != ntp_settings_revision.load(std::memory_order_relaxed))
    static_cast<NativeNetwork *>(request->user_ctx)->request_time_sync();
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

esp_err_t reboot_device(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (!same_origin(request))
    return configuration_reply(request, {"403 Forbidden", "invalid_origin", {}});
  if (request->content_len != 0)
    return configuration_reply(request, {"400 Bad Request", "invalid_request", {}});
  if (esp_timer_is_active(restart_timer) || network(request).busy() || !frame_restart_available())
    return configuration_reply(request, {"409 Conflict", "operation_in_progress", {}});
  guarded_restart.store(true);
  if (esp_timer_start_once(restart_timer, 800'000) != ESP_OK) {
    guarded_restart.store(false);
    return configuration_reply(request, {"500 Internal Server Error", "restart_unavailable", {}});
  }
  return configuration_reply(request, {"202 Accepted", nullptr, {}, 0, {}, true});
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

esp_err_t release_firmware(httpd_req_t *request) {
  if (!authorize(request))
    return ESP_OK;
  if (request->method == HTTP_GET) {
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> document(native_release_status(), cJSON_Delete);
    return send_configuration(request, document.get());
  }
  if (!same_origin(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the frame settings to update firmware.");
  std::array<char, 32> version{};
  if (!header_is(request, "Content-Type", "text/plain") || request->content_len >= version.size())
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Choose a published preview version.");
  size_t received = 0;
  while (received < request->content_len) {
    const auto count = httpd_req_recv(request, version.data() + received, request->content_len - received);
    if (count <= 0)
      return ESP_FAIL;
    received += count;
  }
  if (!request_native_release({version.data(), received})) {
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "A release update is unavailable or already in progress.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  return httpd_resp_sendstr(request, "Accepted");
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

void restart(void *) {
  if (guarded_restart.exchange(false))
    request_frame_restart();
  else
    esp_restart();
}

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
  // Configuration writes validate and persist a full settings record on this task.
  config.stack_size = 12288;
  config.max_open_sockets = 4;
  config.recv_wait_timeout = 3;

  const httpd_uri_t routes[]{
      {.uri = "/", .method = HTTP_GET, .handler = home_page, .user_ctx = &network},
      {.uri = "/admin", .method = HTTP_GET, .handler = admin_page, .user_ctx = &network},
      {.uri = "/native-settings", .method = HTTP_GET, .handler = native_settings, .user_ctx = &network},
      {.uri = "/espframe/admin.js", .method = HTTP_GET, .handler = admin_app, .user_ctx = &network},
      {.uri = "/espframe/admin-cards.js", .method = HTTP_GET, .handler = admin_cards, .user_ctx = &network},
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
      {.uri = "/espframe/api/v1/logs", .method = HTTP_GET, .handler = logs, .user_ctx = &network},
      {.uri = "/espframe/api/v1/wireguard", .method = HTTP_GET, .handler = wireguard_state, .user_ctx = &network},
      {.uri = "/api/firmware/wifi", .method = HTTP_GET, .handler = wifi_firmware, .user_ctx = &network},
      {.uri = "/api/firmware/wifi", .method = HTTP_POST, .handler = wifi_firmware, .user_ctx = &network},
      {.uri = "/espframe/api/v1/reboot", .method = HTTP_POST, .handler = reboot_device, .user_ctx = &network},
      {.uri = frame_identity_path, .method = HTTP_GET, .handler = identity, .user_ctx = &network},
      {.uri = frame_identity_path, .method = HTTP_POST, .handler = identity, .user_ctx = &network},
      {.uri = screen_control_path, .method = HTTP_POST, .handler = change_screen, .user_ctx = &network},
      {.uri = contract::CONFIGURATION_PATH, .method = HTTP_GET, .handler = configuration, .user_ctx = &network},
      {.uri = contract::CONFIGURATION_PATH, .method = HTTP_POST, .handler = save_configuration, .user_ctx = &network},
      {.uri = contract::RESET_PATH, .method = HTTP_GET, .handler = reset_status, .user_ctx = &network},
      {.uri = contract::RESET_PATH, .method = HTTP_POST, .handler = reset_settings, .user_ctx = &network},
      {.uri = "/api/firmware", .method = HTTP_GET, .handler = firmware_status, .user_ctx = &network},
      {.uri = "/api/firmware", .method = HTTP_POST, .handler = update_firmware, .user_ctx = &network},
      {.uri = "/api/firmware/releases", .method = HTTP_GET, .handler = release_firmware, .user_ctx = &network},
      {.uri = "/api/firmware/releases", .method = HTTP_POST, .handler = release_firmware, .user_ctx = &network},
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
  if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip) == ESP_OK && ip.ip.addr)
    ESP_LOGI(tag, "Settings page listening at http://" IPSTR "/", IP2STR(&ip.ip));
  return true;
}

}  // namespace espframe::native
