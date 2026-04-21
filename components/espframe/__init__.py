"""ESPHome helper component for ESPFrame shared C++ utilities."""

import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@jtenniswood"]
DEPENDENCIES = ["json"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Make the helper functions and structs available to YAML lambdas generated
    # for the device package.
    cg.add_global(cg.RawStatement('#include "esphome/components/espframe/espframe_helpers.h"'))
