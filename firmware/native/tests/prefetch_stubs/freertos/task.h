#pragma once

#include "FreeRTOS.h"

struct MockTask;
using TaskHandle_t = MockTask *;
using TaskFunction_t = void (*)(void *);

int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, unsigned stack, void *context, unsigned priority,
                            TaskHandle_t *task, int core);
int xPortGetCoreID();
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear, TickType_t timeout);
