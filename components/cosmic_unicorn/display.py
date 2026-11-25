import esphome.codegen as cg
from esphome.components import display
import esphome.config_validation as cv
from esphome.const import CONF_BRIGHTNESS, CONF_ID, CONF_LAMBDA, CONF_PAGES, CONF_VOLUME

from . import CosmicUnicornDisplay

DEPENDENCIES = ["rp2"]

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(CosmicUnicornDisplay),
            cv.Optional(CONF_BRIGHTNESS, default=0.5): cv.percentage,
            cv.Optional(CONF_VOLUME, default=0.5): cv.percentage,
        }
    ).extend(cv.polling_component_schema("50ms")),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
    cv.only_on_rp2,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
    cg.add(var.set_volume(config[CONF_VOLUME]))
    await display.add_writer(var, config)
