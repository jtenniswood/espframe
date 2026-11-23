#include "immich_http.h"

#include "esp_crt_bundle.h"
#include "esp_timer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>

using espframe::native::ImmichHttp;

namespace {

constexpr auto base = "https://photos.example.test/immich/api";
constexpr auto preview_path = "/assets/photo/thumbnail?size=preview";
constexpr auto search_path = "/search/random";
constexpr auto api_key = "test-key";
constexpr auto query = "{\"size\":2}";
std::string_view expected_base = base;
const char *expected_api_key = api_key;

enum class Fault {
  None, Open, Headers, Truncated, Cancel, Expire, SlowHeaders, SlowBody, StalledBody,
  CancelDuringHeaders, CancelDuringBody, CancelAtEnd
};

struct Response {
  std::string body;
  bool persistent{true};
  bool chunked{};
  int status{200};
  Fault fault{Fault::None};
};

struct Request {
  unsigned socket;
  esp_http_client_method_t method;
  std::string url;
  std::string body;
  int open_timeout_ms;
};

std::atomic<bool> cancelled;
std::deque<Response> responses;
std::vector<Request> requests;
unsigned sockets{};
int64_t now_us{1'000'000};

}  // namespace

struct esp_http_client {
  unsigned socket{};
  esp_http_client_method_t method{};
  std::string url;
  Response response;
  size_t position{};
  int write_length{};
  int timeout_ms{};
  unsigned waits{};
  bool drained{true};
  std::string api_key;
};

esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }
int64_t esp_timer_get_time() { return now_us; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  assert(std::string_view(config->url).starts_with(expected_base));
  assert(config->disable_auto_redirect && config->crt_bundle_attach == esp_crt_bundle_attach);
  return new esp_http_client;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
  delete client;
  return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value) {
  if (std::string_view(name) == "x-api-key")
    client->api_key = value;
  else
    assert(std::string_view(name) == "Content-Type" && std::string_view(value) == "application/json");
  return ESP_OK;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url) {
  assert(std::string_view(url).starts_with(expected_base));
  client->url = url;
  return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method) {
  client->method = method;
  return ESP_OK;
}

esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout) {
  assert(timeout > 0 && timeout <= 3000);
  client->timeout_ms = timeout;
  return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length) {
  assert(client->drained && !responses.empty());
  assert(client->api_key == expected_api_key);
  client->response = std::move(responses.front());
  responses.pop_front();
  client->position = 0;
  client->waits = 0;
  client->drained = false;
  client->write_length = length;
  if (client->socket == 0)
    client->socket = ++sockets;
  requests.push_back({client->socket, client->method, client->url, {}, client->timeout_ms});
  assert((length > 0) == (client->method == HTTP_METHOD_POST));
  return client->response.fault == Fault::Open ? ESP_FAIL : ESP_OK;
}

int esp_http_client_write(esp_http_client_handle_t client, const char *body, int length) {
  assert(client->write_length == length && requests.back().body.empty());
  assert(client->timeout_ms == requests.back().open_timeout_ms);
  requests.back().body.assign(body, length);
  return length;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
  assert(client->timeout_ms <= 250);
  const auto &response = client->response;
  if (response.fault == Fault::Expire) {
    now_us += 2'000'000;
    return ESP_FAIL;
  }
  if (response.fault == Fault::Headers)
    return ESP_FAIL;
  if ((response.fault == Fault::SlowHeaders && client->waits < 3) || response.fault == Fault::CancelDuringHeaders) {
    ++client->waits;
    cancelled = response.fault == Fault::CancelDuringHeaders;
    now_us += client->timeout_ms * 1000;
    return -ESP_ERR_HTTP_EAGAIN;
  }
  return response.chunked ? 0 : response.body.size() + (response.fault == Fault::Truncated ? 1 : 0);
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->response.status; }

int esp_http_client_read(esp_http_client_handle_t client, char *target, int limit) {
  assert(client->timeout_ms <= 250);
  const auto &response = client->response;
  const bool cancelled_body = response.fault == Fault::CancelDuringBody && client->position != 0;
  if (response.fault == Fault::StalledBody || cancelled_body ||
      (response.fault == Fault::SlowBody && client->position != 0 && client->waits == 0)) {
    ++client->waits;
    cancelled = cancelled_body;
    now_us += client->timeout_ms * 1000;
    return -ESP_ERR_HTTP_EAGAIN;
  }
  const auto count = std::min({response.body.size() - client->position, static_cast<size_t>(limit), size_t{3}});
  std::memcpy(target, response.body.data() + client->position, count);
  client->position += count;
  client->drained = count == 0 || (!response.chunked && response.fault != Fault::Truncated &&
                                  client->position == response.body.size());
  if (response.fault == Fault::Cancel || (response.fault == Fault::CancelAtEnd && client->drained))
    cancelled = true;
  return static_cast<int>(count);
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) {
  assert(client->drained);
  return client->response.fault != Fault::Truncated;
}

bool esp_http_client_is_persistent_connection(esp_http_client_handle_t client) {
  assert(client->drained);
  return client->response.persistent;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client) {
  client->socket = 0;
  return ESP_OK;
}

esp_err_t esp_http_client_clear_response_buffer(esp_http_client_handle_t client) {
  assert(client->socket == 0);
  client->drained = true;
  return ESP_OK;
}

int main() {
  ImmichHttp client{cancelled};
  std::array<uint8_t, 5> output{};
  size_t received = 0;
  auto read = [&](std::string_view path = preview_path, std::string_view body = "", int64_t deadline = 0) {
    return client.read(std::string(expected_base) + std::string(path), expected_api_key, body, output, received,
                       deadline);
  };
  auto text = [&] { return std::string_view(reinterpret_cast<const char *>(output.data()), received); };

  // Both response formats can fill the caller's limit exactly and keep the
  // same connection. Switching methods must send only the search POST body.
  responses = {{"photo"}, {"query", true, true}, {"next"}};
  assert(read() == 200 && text() == "photo");
  assert(read(search_path, query) == 200 && text() == "query");
  assert(read() == 200 && text() == "next");
  assert(sockets == 1 && requests.size() == 3);
  assert(requests[0].open_timeout_ms == 3000);
  assert(requests[1].url.ends_with(search_path) && requests[1].body == query && requests[2].body.empty());

  responses = {{"", true, false, 200, Fault::Headers}, {"new"}};
  assert(read(search_path, query) == 200 && text() == "new");
  assert(sockets == 2 && requests.size() == 5);
  assert(requests[3].socket == 1 && requests[4].socket == 2 && requests[4].body == query);
  responses = {{"", true, false, 200, Fault::Open}, {"close", false}};
  assert(read() == 200 && text() == "close" && sockets == 3);

  // A semantic error, size limit, or incomplete body closes the stream.
  // A subsequent request never consumes leftover bytes from that response.
  responses = {{"warm"}, {"denied", true, false, 401}, {"bigger"}, {"bigger", true, true},
               {"cut", true, false, 200, Fault::Truncated}, {"ready"}};
  assert(read() == 200 && sockets == 4);
  assert(read() == 401 && received == 0 && sockets == 4);
  assert(read() == 413 && received == 0 && sockets == 5);
  assert(read() == 413 && received == 0 && sockets == 6);
  assert(read() == 0 && received == 0 && sockets == 7);
  assert(read() == 200 && text() == "ready" && sockets == 8);

  responses = {{"break", true, true, 200, Fault::Cancel}};
  assert(read() == 0 && received == 0 && sockets == 8);
  assert(read() == 0 && received == 0 && sockets == 8);
  cancelled = false;
  responses = {{"again"}, {"", true, false, 200, Fault::Expire}};
  assert(read() == 200 && sockets == 9);
  assert(read(preview_path, "", now_us + 400'000) == 0 && received == 0 && sockets == 9);
  assert(requests.back().open_timeout_ms == 400);

  responses = {{"warm"}, {"", true, false, 200, Fault::Headers}, {"", true, false, 200, Fault::Headers}, {"last"}};
  assert(read() == 200 && sockets == 10);
  assert(read() == 0 && received == 0 && sockets == 11 && responses.size() == 1);
  assert(read() == 200 && text() == "last" && sockets == 12);
  client.disconnect();
  responses = {{"fresh"}};
  assert(read() == 200 && sockets == 13 && responses.empty());

  // A short stall keeps the active request and bytes already received.
  responses = {{"heads", true, false, 200, Fault::SlowHeaders}, {"bytes", true, true, 200, Fault::SlowBody}};
  const auto before_slow = requests.size();
  const auto slow_started = now_us;
  assert(read(search_path, query) == 200 && text() == "heads");
  assert(read() == 200 && text() == "bytes");
  assert(sockets == 13 && requests.size() == before_slow + 2);
  assert(now_us - slow_started == 1'000'000);

  // The short optional budget still expires without a new request.
  responses = {{"stuck", true, false, 200, Fault::StalledBody}, {"next"}};
  const auto deadline = now_us + 2'300'000;
  assert(read(preview_path, "", deadline) == 0 && received == 0 && now_us == deadline);
  assert(requests.back().open_timeout_ms == 1000);
  assert(responses.size() == 1 && read() == 200 && text() == "next" && sockets == 14);

  // A receive timeout returns to the loader after one wait if the owner
  // has cancelled, even when some bytes of the obsolete body were copied.
  auto wait_started = now_us;
  responses = {{"halt", true, false, 200, Fault::CancelDuringHeaders}, {"after"}};
  assert(read() == 0 && received == 0 && cancelled && responses.size() == 1);
  assert(now_us - wait_started == 250'000);
  cancelled = false;
  assert(read() == 200 && text() == "after" && sockets == 15);
  wait_started = now_us;
  responses = {{"halt", true, false, 200, Fault::CancelDuringBody}, {"after"}};
  assert(read() == 0 && received == 0 && cancelled && responses.size() == 1);
  assert(now_us - wait_started == 250'000);
  cancelled = false;
  assert(read() == 200 && text() == "after" && sockets == 16);

  // Cancellation can race with a fully received fixed-length preview.
  // Return its bytes so the loader can keep them for a later swipe.
  responses = {{"whole", true, false, 200, Fault::CancelAtEnd}, {"after"}};
  assert(read() == 200 && text() == "whole" && cancelled);
  assert(read() == 0 && received == 0 && responses.size() == 1);
  cancelled = false;
  assert(read() == 200 && text() == "after" && sockets == 16);

  wait_started = now_us;
  responses = {{"stuck", true, false, 200, Fault::StalledBody}, {"next"}};
  assert(read() == 0 && received == 0 && now_us - wait_started == 12'000'000);
  assert(read() == 200 && text() == "next" && sockets == 17);

  client.reset();
  expected_base = "https://other-photos.test/api";
  expected_api_key = "replacement-key";
  responses = {{"new"}};
  assert(read() == 200 && text() == "new" && sockets == 18);
}
