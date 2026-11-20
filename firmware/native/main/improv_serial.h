#pragma once

#include "improv_wire.h"
#include "native_network.h"

namespace espframe::native {

class ImprovSerial {
 public:
  explicit ImprovSerial(NativeNetwork &network) : network_(network) {}

  bool consume(uint8_t byte, bool line_start, int64_t now_us);
  void poll(int64_t now_us);
  bool busy() const { return pending_.has_value(); }

 private:
  void command_(std::span<const uint8_t> payload);
  void state_();
  void send_url_(improv::Command command);

  NativeNetwork &network_;
  improv::Reader reader_;
  std::optional<NativeNetwork::Request> pending_;
  int64_t last_byte_us_{};
};

}  // namespace espframe::native
