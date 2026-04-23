"""ESPHome helper component for ESPFrame shared C++ utilities."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["json"]

espframe_ns = cg.esphome_ns.namespace("espframe")
EspFrameComponent = espframe_ns.class_("EspFrameComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspFrameComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_component.h"'))
    # ESPHome 2026.4 accelerates ESP32-P4 LVGL software rotation with PPA,
    # which uses DMA2D underneath. The 10-inch frame has shown repeated crashes
    # in the DMA2D interrupt handler, so force LVGL onto its CPU rotation
    # fallback until that stack is stable.
    cg.add_build_flag("-Wl,--wrap=ppa_register_client")
