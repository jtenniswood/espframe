#pragma once
#include <ArduinoJson.h>
#include <string>
// Only adapt ESPHome's allocator wrapper. All asset parsing remains production code.
namespace esphome::json {
inline JsonDocument parse_json(const std::string &body) {
  JsonDocument document;
  if (deserializeJson(document, body)) document.clear();
  return document;
}
}
