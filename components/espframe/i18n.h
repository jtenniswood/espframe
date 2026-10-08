#pragma once

#include <cstring>
#include <string>
#include "i18n_generated.h"

// The saved language select sets this before the first display refresh.
inline size_t &espframe_language_index() {
  static size_t index = 0;
  return index;
}

inline void set_espframe_language(const std::string &code) {
  espframe_language_index() = 0;  // Unsupported codes safely use English.
  for (size_t i = 0; i < espframe_i18n_catalogue::LANGUAGE_COUNT; ++i) {
    if (code == espframe_i18n_catalogue::LANGUAGES[i]) {
      espframe_language_index() = i;
      return;
    }
  }
}

inline const char *espframe_i18n_key(const char *key) {
  if (key == nullptr) return "";
  for (const auto &entry : espframe_i18n_catalogue::STRINGS) {
    if (std::strcmp(key, entry.key) == 0) return entry.values[espframe_language_index()];
  }
  return key;
}

// Use only for known English firmware literals, never for Immich content or
// entity option values. Keys distinguish text where context matters.
inline const char *espframe_i18n(const char *text) {
  if (text == nullptr) return "";
  for (const auto &entry : espframe_i18n_catalogue::STRINGS) {
    if (std::strcmp(text, entry.values[0]) == 0) return entry.values[espframe_language_index()];
  }
  return text;
}

// Preserve whichever static status/error is already displayed during a live
// language switch. Catalogue validation rejects ambiguous reverse matches.
inline const char *espframe_i18n_retranslate(const char *text) {
  if (text == nullptr) return "";
  for (const auto &entry : espframe_i18n_catalogue::STRINGS) {
    for (const char *value : entry.values) {
      if (std::strcmp(text, value) == 0) return entry.values[espframe_language_index()];
    }
  }
  return text;
}

inline std::string espframe_replace_placeholder(std::string text, const std::string &placeholder,
                                               const std::string &value) {
  if (placeholder.empty()) return text;
  size_t position = 0;
  while ((position = text.find(placeholder, position)) != std::string::npos) {
    text.replace(position, placeholder.size(), value);
    position += value.size();
  }
  return text;
}

inline std::string espframe_i18n_count(const char *singular, const char *plural, int count) {
  return espframe_replace_placeholder(espframe_i18n_key(count == 1 ? singular : plural),
                                     "{count}", std::to_string(count));
}

inline std::string espframe_wifi_instructions(const std::string &ssid, const std::string &address) {
  // Insert network-provided content last so placeholder-like SSIDs stay literal.
  std::string text = espframe_replace_placeholder(espframe_i18n_key("wifi_instructions"), "{address}", address);
  return espframe_replace_placeholder(text, "{hotspot}", ssid.empty() ? "" : "'" + ssid + "'\n");
}
