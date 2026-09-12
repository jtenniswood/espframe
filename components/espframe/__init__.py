"""ESPHome helper component for ESPFrame shared C++ utilities."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_MEMORY_DIAGNOSTICS = "memory_diagnostics"
CONF_MEMORY_BEFORE_ID = "memory_before_id"
CONF_MEMORY_AFTER_ID = "memory_after_id"

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["json", "web_server_base"]

espframe_ns = cg.esphome_ns.namespace("espframe")
EspFrameComponent = espframe_ns.class_("EspFrameComponent", cg.Component)

MemorySetupProbe = espframe_ns.class_("MemorySetupProbe", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspFrameComponent),
        cv.Optional(CONF_MEMORY_DIAGNOSTICS, default=False): cv.boolean,
        cv.GenerateID(CONF_MEMORY_BEFORE_ID): cv.declare_id(MemorySetupProbe),
        cv.GenerateID(CONF_MEMORY_AFTER_ID): cv.declare_id(MemorySetupProbe),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_component.h"'))

    if config[CONF_MEMORY_DIAGNOSTICS]:
        cg.add_define("ESPFRAME_MEMORY_DIAGNOSTICS")
    cg.add_build_flag("-Wl,--wrap=heap_caps_aligned_alloc")
    for key, before in ((CONF_MEMORY_BEFORE_ID, True), (CONF_MEMORY_AFTER_ID, False)):
        probe = cg.new_Pvariable(config[key], before)
        await cg.register_component(probe, config)
