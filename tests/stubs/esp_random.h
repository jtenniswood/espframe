#pragma once

#include <cstdint>

inline uint32_t esp_random_test_value = 0;
inline uint32_t esp_random() { return esp_random_test_value; }
