# Temporary P4 V3 display fix

Vendored from ESPHome **2026.8.2**, as shipped in
`ghcr.io/esphome/esphome:2026.8.2`, with its original license in `LICENSE`.
Upstream: https://github.com/esphome/esphome/tree/2026.8.2/esphome/components/mipi_dsi

Local changes:

- `MipiDsi::setup()` value-initializes `phy_clk_src` instead of assigning
  `MIPI_DSI_PHY_CLK_SRC_DEFAULT`. ESP-IDF chooses the silicon-appropriate PHY
  source for production ESP32-P4 v3.x. The deprecated alias selects the legacy
  PLL clock and fails on production silicon.
- Optional `double_buffer: true` allocates two frame buffers. LVGL output is
  copied into the hidden buffer, and `loop()` swaps buffers after each complete
  LVGL refresh, waiting for the panel's next frame boundary before reusing the
  old one. Without it, LVGL writes into the buffer the panel is scanning and
  full-screen redraws visibly tear from top to bottom.
- Optional `frame_transitions: true` (requires `double_buffer`) adds a third
  frame buffer and two frame-sized PSRAM buffers. `start_transition()` keeps
  the frame on screen and the next frame LVGL renders, and the PPA composites
  cross-fades, fades through a colour, and slides between them, one frame per
  loop while the previous one waits for its frame boundary. If transition setup
  fails, its snapshot and target buffers are released before falling back to
  plain double buffering.

All display models, timings and initialization are otherwise unchanged. Every
JC8012P4A1 profile loads this copy. Keep it pinned to the tested ESPHome
version.
