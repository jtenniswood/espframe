#pragma once

#include "nvs.h"

esp_err_t nvs_flash_init_partition(const char *);
esp_err_t nvs_flash_deinit_partition(const char *);
