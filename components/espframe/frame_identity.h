#pragma once

#include <memory>
#include "frame_identity_model.h"

namespace esphome::espframe {
class FrameIdentity {
 public:
  void setup();
  bool save(const std::string &name);
  const std::string &saved_name() const { return saved_name_; }
  std::string suffix() const;
  std::string target_name() const { return saved_name_.empty() ? std::string(default_friendly_) : saved_name_; }
  std::string target_hostname() const { return saved_name_.empty() ? std::string(default_hostname_) : frame_hostname(saved_name_, suffix()); }
  bool restart_required() const { return saved_name_ != running_friendly_; }

 protected:
  std::string saved_name_;
  // Borrow the generated firmware buffers; they live for the entire boot.
  const char *default_hostname_{""};
  const char *default_friendly_{""};
  // Application and API StringRefs point here. Never change these after setup.
  std::unique_ptr<char[]> running_hostname_;
  std::string running_friendly_;
};
}  // namespace esphome::espframe
