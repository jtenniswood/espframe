#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

namespace espframe::native {

struct TouchSample {
  int x{};
  int y{};
  uint8_t contacts{};
};

class Touch {
 public:
  void init();
  esp_err_t read(TouchSample &sample) const;

 private:
  esp_err_t write_(uint8_t address, uint32_t value, bool one_byte = false) const;
  esp_err_t read_(uint8_t address, uint8_t *data, size_t size) const;

  i2c_master_dev_handle_t handle_{};
};

}  // namespace espframe::native
