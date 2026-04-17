"""ESPHome helper component for ESPFrame shared C++ utilities.

During code generation this module includes espframe_helpers.h and regenerates
the compact timezone lookup table used by the sunrise/sunset code.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from pathlib import Path

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["json"]

CONF_LVGL_ASPECT_COMPENSATION = "lvgl_aspect_compensation"
CONF_SCALE_X = "scale_x"
CONF_PANEL_WIDTH = "panel_width"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_LVGL_ASPECT_COMPENSATION): cv.Schema(
            {
                cv.Required(CONF_SCALE_X): cv.float_range(min=0.1, max=1.0),
                cv.Optional(CONF_PANEL_WIDTH, default=0): cv.positive_int,
            }
        )
    }
)


async def to_code(config):
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_helpers.h"'))

    if aspect_config := config.get(CONF_LVGL_ASPECT_COMPENSATION):
        script_path = Path(__file__).parent / "lvgl_aspect_compensation.py"
        cg.add_platformio_option("extra_scripts", [f"pre:{script_path}"])
        cg.add_build_flag(f"-DESPFRAME_DISPLAY_ASPECT_SCALE_X={aspect_config[CONF_SCALE_X]:.6f}f")
        cg.add_build_flag(f"-DESPFRAME_DISPLAY_ASPECT_PANEL_WIDTH={aspect_config[CONF_PANEL_WIDTH]}")

    from .timezones import TIMEZONES

    # Generate a C++ header instead of storing duplicate timezone data in both
    # Python and C++ sources.
    lines = ['#pragma once', '#include "sun_calc.h"', '', 'static const TzInfo TZ_DATA[] = {']
    for tz, _gmt, lat, lon, posix in TIMEZONES:
        lines.append(f'  {{"{tz}", {lat:>8.2f}f, {lon:>8.2f}f, "{posix}"}},')
    lines.append('};')
    lines.append('')
    lines.append(f'static constexpr int TZ_DATA_COUNT = {len(TIMEZONES)};')
    lines.append('')

    out_dir = Path(__file__).parent
    (out_dir / "tz_data_generated.h").write_text("\n".join(lines) + "\n")
