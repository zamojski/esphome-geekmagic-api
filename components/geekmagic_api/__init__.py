# SPDX-FileCopyrightText: 2026 Adam Zamojski
#
# SPDX-License-Identifier: GPL-3.0-only

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, light
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["display", "network", "light"]

geekmagic_api_ns = cg.esphome_ns.namespace("geekmagic_api")
GeekMagicApi = geekmagic_api_ns.class_("GeekMagicApi", cg.Component)

CONF_DISPLAY_ID = "display_id"
CONF_BACKLIGHT_ID = "backlight_id"
CONF_MODEL = "model"
CONF_VERSION = "version"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeekMagicApi),
        cv.Required(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Optional(CONF_BACKLIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_PORT, default=80): cv.port,
        cv.Optional(CONF_MODEL, default="SmallTV-Ultra"): cv.string,
        cv.Optional(CONF_VERSION, default="Ultra-V9.0.33"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    try:
        from esphome.components.esp32 import include_builtin_idf_component
    except ImportError:
        # Older ESPHome versions included built-in IDF components by default.
        pass
    else:
        include_builtin_idf_component("esp_http_server")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(await cg.get_variable(config[CONF_DISPLAY_ID])))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_model(config[CONF_MODEL]))
    cg.add(var.set_version(config[CONF_VERSION]))
    if CONF_BACKLIGHT_ID in config:
        cg.add(var.set_backlight(await cg.get_variable(config[CONF_BACKLIGHT_ID])))