#pragma once

#include "FreeRTOS.h"

struct MockSemaphore;
using SemaphoreHandle_t = MockSemaphore *;

SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
void xSemaphoreGive(SemaphoreHandle_t semaphore);
