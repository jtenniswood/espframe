#pragma once

#include <cstdint>

using TickType_t = uint32_t;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline constexpr int pdTRUE = 1;
inline constexpr int pdPASS = 1;
constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
