#pragma once
#include "runtime.h"
#define ESP_LOGI(tag, format, ...) memory_test::log(format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) memory_test::log(format, ##__VA_ARGS__)
