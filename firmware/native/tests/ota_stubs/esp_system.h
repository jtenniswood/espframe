#pragma once

inline const char *esp_err_to_name(int) { return "mock result"; }
void esp_restart();
