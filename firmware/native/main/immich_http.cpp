#include "immich_http.h"

#include "live_trial.h"
#include "memory_trace.h"
#if NATIVE_TAILSCALE
#include "native_tailscale.h"
#include "tailscale_url.h"
#include "lwip/sockets.h"

#include <cstring>
#endif

#include "esp_crt_bundle.h"
#include "esp_timer.h"

#include <algorithm>

namespace espframe::native {
namespace {

constexpr int buffer_size = 4096;
constexpr int transport_timeout_ms = 3000;
constexpr int optional_timeout_ms = 1000;
constexpr int receive_wait_ms = 250;

}  // namespace

ImmichHttp::~ImmichHttp() { reset(); }

bool ImmichHttp::init_(const char *url, const char *api_key) {
  esp_http_client_config_t config{};
  config.url = url;
  config.disable_auto_redirect = true;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = transport_timeout_ms;
  config.buffer_size = buffer_size;
#if NATIVE_TAILSCALE
  struct ifreq tunnel{};
  if (tailnet_generation_) {
    std::strcpy(tunnel.ifr_name, "wg253");
    config.if_name = &tunnel;
    config.common_name = tailnet_hostname_.c_str();
  }
#endif
  client_ = esp_http_client_init(&config);
  if (!client_)
    return false;
  if (esp_http_client_set_header(client_, auth_header_, api_key) == ESP_OK &&
      esp_http_client_set_header(client_, "Content-Type", "application/json") == ESP_OK)
    return true;
  esp_http_client_cleanup(client_);
  client_ = nullptr;
  return false;
}

void ImmichHttp::disconnect() {
  reusable_ = false;
  if (client_) {
    esp_http_client_close(client_);
    esp_http_client_clear_response_buffer(client_);
  }
}

void ImmichHttp::reset() {
  reusable_ = false;
  if (client_) {
    esp_http_client_cleanup(client_);
    client_ = nullptr;
  }
}

int ImmichHttp::read(const std::string &url, const char *api_key, std::string_view query, std::span<uint8_t> output,
                      size_t &received, int64_t deadline_us) {
  received = 0;
  const bool optional = deadline_us != 0;
  const int64_t now = esp_timer_get_time();
  if (!optional)
    deadline_us = now + transfer_budget_us;
  if (cancelled_ || now >= deadline_us)
    return 0;
  const char *target = url.c_str();
#if NATIVE_TAILSCALE
  const auto route = tailscale_route_for(url);
  std::string routed_url;
  std::string http_host;
  if (route.required) {
    if (!route.generation) {
      reset();
      return 0;
    }
    const auto host = tailnet_host(url).hostname;
    const auto tls_name = host.ends_with('.') ? host.substr(0, host.size() - 1) : host;
    if (tailnet_generation_ != route.generation || tailnet_hostname_ != tls_name || tailnet_address_ != route.address) {
      reset();
      tailnet_generation_ = route.generation;
      tailnet_hostname_ = tls_name;
      tailnet_address_ = route.address;
    }
    // Host and TLS identity still refer to the server's name. The socket
    // connects by address through the bound WireGuard interface.
    const size_t offset = host.data() - url.data();
    http_host = url.substr(offset, url.find_first_of("/?#", offset) - offset);
    routed_url = url;
    routed_url.replace(offset, host.size(), route.address);
    target = routed_url.c_str();
  } else if (tailnet_generation_) {
    reset();
    tailnet_generation_ = 0;
  }
#endif
  if (!client_ && !init_(target, api_key))
    return 0;
  if (esp_http_client_set_url(client_, target) != ESP_OK ||
#if NATIVE_TAILSCALE
      (route.required && (route.generation != tailscale_generation() ||
                         esp_http_client_set_header(client_, "Host", http_host.c_str()) != ESP_OK)) ||
#endif
      esp_http_client_set_method(client_, query.empty() ? HTTP_METHOD_GET : HTTP_METHOD_POST) != ESP_OK) {
    disconnect();
    return 0;
  }

  const auto exchange = [&]() {
    const auto reply = read_once_(query, output, received, deadline_us,
                                  optional ? optional_timeout_ms : transport_timeout_ms);
    if (reply.status == 200) {
#if NATIVE_TAILSCALE
      if (tailnet_generation_)
        trace_memory("tailnet-http-success");
      else
#endif
        trace_memory("photo-http-success");
    }
    reusable_ = reply.status == 200 && esp_http_client_is_persistent_connection(client_);
    if (!reusable_)
      disconnect();
    return reply;
  };
  const bool reused = reusable_;
  const auto reply = exchange();
  if (reused && !reply.headers_received && !cancelled_ && esp_timer_get_time() < deadline_us)
    return exchange().status;
  return reply.status;
}

ImmichHttp::Reply ImmichHttp::read_once_(std::string_view query, std::span<uint8_t> output, size_t &received,
                                        int64_t deadline_us, int request_timeout_ms) {
  const auto set_timeout = [&](int timeout_ms) {
    const auto remaining = deadline_us - esp_timer_get_time();
    return !cancelled_ && remaining > 0 &&
#if NATIVE_TAILSCALE
           (!tailnet_generation_ || tailnet_generation_ == tailscale_generation()) &&
#endif
           esp_http_client_set_timeout_ms(client_, std::min<int64_t>(timeout_ms, (remaining + 999) / 1000)) == ESP_OK;
  };
  if (!set_timeout(request_timeout_ms))
    return {};
#if NATIVE_TAILSCALE
  const bool tailnet = tailnet_generation_ != 0;
#else
  constexpr bool tailnet = false;
#endif
  const MemoryTraceScope trace{tailnet ? "tailnet-http-start" : "photo-http-start",
                               tailnet ? "tailnet-http-end" : "photo-http-end"};
  if (const auto error = esp_http_client_open(client_, query.size()); error != ESP_OK) {
#if SWIPE_TRIAL_IMMICH
    record_live_trial_http_open_error(error);
#endif
    return {};
  }
  if (!query.empty() && (!set_timeout(request_timeout_ms) ||
                        esp_http_client_write(client_, query.data(), query.size()) != static_cast<int>(query.size())))
    return {};
  int64_t content_length = ESP_FAIL;
  // ESP-IDF preserves its parser across EAGAIN. Short receive waits let
  // the worker check for a newer swipe without expiring a slow response.
  while (set_timeout(receive_wait_ms)) {
    content_length = esp_http_client_fetch_headers(client_);
    trace_memory(tailnet ? "tailnet-http-headers" : "photo-http-headers");
    if (content_length != -ESP_ERR_HTTP_EAGAIN)
      break;
#if SWIPE_TRIAL_IMMICH
    record_live_trial_http_read_timeout(true);
#endif
  }
  if (content_length < 0)
    return {};
  const int status = esp_http_client_get_status_code(client_);
  if (status != 200)
    return {status, true};
  if (content_length > static_cast<int64_t>(output.size()))
    return {413, true};

  // A fixed-length body is ready after all declared bytes reach the caller.
  // A chunked body still needs the final read to drain buffered data.
  size_t length = 0;
  while (set_timeout(receive_wait_ms)) {
    char overflow;
    const bool full = length == output.size();
    char *target = full ? &overflow : reinterpret_cast<char *>(output.data() + length);
    const int count = esp_http_client_read(client_, target, full ? 1 : std::min<size_t>(output.size() - length, buffer_size));
    if (count == -ESP_ERR_HTTP_EAGAIN) {
#if SWIPE_TRIAL_IMMICH
      record_live_trial_http_read_timeout(false);
#endif
      continue;
    }
    if (count < 0)
      break;
    if (count > 0) {
      if (full)
        return {413, true};
      length += count;
      if (content_length == 0 || length != static_cast<size_t>(content_length))
        continue;
    }
    const bool matches_length = content_length == 0 || length == static_cast<size_t>(content_length);
    if (length != 0 && matches_length && esp_http_client_is_complete_data_received(client_)) {
      received = length;
      return {status, true};
    }
    if (count == 0)
      break;
  }
  return {0, true};
}

}  // namespace espframe::native
