#pragma once

#include "esp_http_client.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace espframe::native {

// Owned by the Immich worker. Both GETs and the JSON search POSTs are
// read-only. A stale connection can be retried before a response arrives.
class ImmichHttp {
 public:
  static constexpr int64_t transfer_budget_us = 12'000'000;

  explicit ImmichHttp(const std::atomic<bool> &cancelled, const char *auth_header = "x-api-key")
      : cancelled_(cancelled), auth_header_(auth_header) {}
  ~ImmichHttp();
  ImmichHttp(const ImmichHttp &) = delete;
  ImmichHttp &operator=(const ImmichHttp &) = delete;

  int read(const std::string &url, const char *api_key, std::string_view query, std::span<uint8_t> output,
           size_t &received, int64_t deadline_us = 0);
  void disconnect();
  void reset();

 private:
  struct Reply {
    int status{};
    bool headers_received{};
  };

  bool init_(const char *url, const char *api_key);
  Reply read_once_(std::string_view query, std::span<uint8_t> output, size_t &received,
                   int64_t deadline_us, int request_timeout_ms);

  const std::atomic<bool> &cancelled_;
  const char *auth_header_;
  esp_http_client_handle_t client_{};
  bool reusable_{};
};

}  // namespace espframe::native
