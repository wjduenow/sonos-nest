// See i2c_bus.h.
#include "i2c_bus.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_lock = nullptr;

void i2cBusInit() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

bool i2cBusLock(uint32_t timeoutMs) {
  // Not initialised yet means we are still in early single-threaded bring-up, before any other
  // task exists to race with. Succeeding here keeps drivers usable from boardInit() without every
  // call site needing a special case.
  if (!s_lock) return true;
  return xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void i2cBusUnlock() {
  if (s_lock) xSemaphoreGive(s_lock);
}
