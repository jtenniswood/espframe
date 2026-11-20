#pragma once

#include "FreeRTOS.h"

struct StaticSemaphore_t {
  UBaseType_t maximum;
  UBaseType_t count;
};
using SemaphoreHandle_t = StaticSemaphore_t *;

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t maximum, UBaseType_t count, StaticSemaphore_t *);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *);
