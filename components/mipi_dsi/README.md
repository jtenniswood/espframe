# Temporary P4 V3 display fix

Vendored from ESPHome **2026.8.2**, as shipped in
`ghcr.io/esphome/esphome:2026.8.2`, with its original license in `LICENSE`.
Upstream: https://github.com/esphome/esphome/tree/2026.8.2/esphome/components/mipi_dsi

The sole source change is in `MipiDsi::setup()`: value-initialize `phy_clk_src`
instead of assigning `MIPI_DSI_PHY_CLK_SRC_DEFAULT`. ESP-IDF chooses the
silicon-appropriate PHY source for production ESP32-P4 v3.x. The deprecated
alias selects the legacy PLL clock and fails on production silicon. All display
models, timings, initialization and drawing code are otherwise unchanged.

Only the JC8012P4A1 V3 profile loads this copy; other devices continue to use
ESPHome's bundled driver. Keep it pinned to the tested ESPHome version and
remove the override when the fix is available upstream.
