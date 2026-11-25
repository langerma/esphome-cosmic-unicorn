import esphome.codegen as cg
from esphome.components import display
import esphome.config_validation as cv
from esphome.const import CONF_BRIGHTNESS, CONF_ID, CONF_LAMBDA, CONF_PAGES, CONF_VOLUME

from . import CosmicUnicornDisplay

CONF_AUTO_BRIGHTNESS = "auto_brightness"
CONF_DARK_LEVEL = "dark_level"
CONF_BRIGHT_LEVEL = "bright_level"
CONF_MIN_BRIGHTNESS = "min_brightness"
CONF_MAX_BRIGHTNESS = "max_brightness"
CONF_ENABLED = "enabled"

AUTO_BRIGHTNESS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        # raw light sensor readings (0-4095) for "dark room" and "bright room"
        cv.Optional(CONF_DARK_LEVEL, default=10): cv.int_range(min=0, max=4095),
        cv.Optional(CONF_BRIGHT_LEVEL, default=1500): cv.int_range(min=0, max=4095),
        # factor applied to the configured / light brightness at those levels
        cv.Optional(CONF_MIN_BRIGHTNESS, default="10%"): cv.percentage,
        cv.Optional(CONF_MAX_BRIGHTNESS, default="100%"): cv.percentage,
    }
)

DEPENDENCIES = ["rp2"]

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(CosmicUnicornDisplay),
            cv.Optional(CONF_BRIGHTNESS, default=0.5): cv.percentage,
            cv.Optional(CONF_VOLUME, default=0.5): cv.percentage,
            cv.Optional(CONF_AUTO_BRIGHTNESS): AUTO_BRIGHTNESS_SCHEMA,
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
    if (auto := config.get(CONF_AUTO_BRIGHTNESS)) is not None:
        cg.add(
            var.set_auto_brightness_config(
                auto[CONF_DARK_LEVEL],
                auto[CONF_BRIGHT_LEVEL],
                auto[CONF_MIN_BRIGHTNESS],
                auto[CONF_MAX_BRIGHTNESS],
            )
        )
        cg.add(var.set_auto_brightness(auto[CONF_ENABLED]))
    await display.add_writer(var, config)
