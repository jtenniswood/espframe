"""ESPHome helper component for ESPFrame shared C++ utilities.

During code generation this module includes espframe_helpers.h and regenerates
the compact timezone lookup table used by the sunrise/sunset code.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from pathlib import Path

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["json"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_helpers.h"'))

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
