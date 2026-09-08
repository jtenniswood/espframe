# Vendored ESPHome LEDC component

This directory contains the LEDC output component from ESPHome 2026.8.2.

Local change: ESP32-P4 uses the RCC-protected low-level LEDC reset available in
ESP-IDF 5.5 instead of the deprecated, nonfunctional `periph_module_reset`
compatibility API. Other targets retain the upstream implementation.
