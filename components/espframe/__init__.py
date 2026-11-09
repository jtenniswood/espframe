"""ESPHome helper component for ESPFrame shared C++ utilities."""

import os
import shutil

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_SETUP_PRIORITY
from esphome.core import CORE

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


def validate_lvgl_priority(config):
    # The allocation window is pinned to LVGL's default PROCESSOR priority.
    full_config = fv.full_config.get()
    if "psram" not in full_config:
        return config
    for lvgl in full_config.get("lvgl", []):
        if lvgl.get(CONF_SETUP_PRIORITY, 400.0) != 400.0:
            raise cv.Invalid(
                "espframe requires lvgl.setup_priority to remain at 400 so its "
                "PSRAM allocation probes bracket LVGL setup"
            )
    return config


FINAL_VALIDATE_SCHEMA = validate_lvgl_priority


def _register_psram_bss_component():
    # The linker fragment only takes effect when the device enables
    # CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY.
    from esphome.components.esp32 import add_idf_component

    # Native ESPHome builds otherwise omit libsrc.a from linker generation.
    # This fragment needs it to keep generated state out of internal RAM.
    os.environ["ESPHOME_LDGEN_FULL_DEPS"] = "1"

    dest_path = str(CORE.relative_build_path("components", "espframe_psram_bss"))
    shutil.copytree(
        os.path.join(os.path.dirname(__file__), "psram_bss"),
        dest_path,
        dirs_exist_ok=True,
    )
    add_idf_component(name="espframe_psram_bss", path=dest_path)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_component.h"'))

    if config[CONF_MEMORY_DIAGNOSTICS]:
        cg.add_define("ESPFRAME_MEMORY_DIAGNOSTICS")
    if "psram" in CORE.config:
        _register_psram_bss_component()
    if "lvgl" not in CORE.config or "psram" not in CORE.config:
        return

    cg.add_build_flag("-Wl,--wrap=heap_caps_aligned_alloc")
    # The probes must bracket LVGL even when the parent overrides its priority.
    probe_config = {key: value for key, value in config.items() if key != CONF_SETUP_PRIORITY}
    for key, before in ((CONF_MEMORY_BEFORE_ID, True), (CONF_MEMORY_AFTER_ID, False)):
        probe = cg.new_Pvariable(config[key], before)
        await cg.register_component(probe, probe_config)
