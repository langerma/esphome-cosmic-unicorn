import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TYPE

from . import CONF_COSMIC_UNICORN_ID, CosmicUnicornDisplay, cosmic_unicorn_ns

DEPENDENCIES = ["rp2"]

BrightnessOutput = cosmic_unicorn_ns.class_("BrightnessOutput", output.FloatOutput)
VolumeOutput = cosmic_unicorn_ns.class_("VolumeOutput", output.FloatOutput)

TYPES = {"brightness": BrightnessOutput, "volume": VolumeOutput}

CONFIG_SCHEMA = cv.typed_schema(
    {
        key: output.FLOAT_OUTPUT_SCHEMA.extend(
            {
                cv.GenerateID(): cv.declare_id(cls),
                cv.GenerateID(CONF_COSMIC_UNICORN_ID): cv.use_id(CosmicUnicornDisplay),
            }
        )
        for key, cls in TYPES.items()
    },
    default_type="brightness",
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await output.register_output(var, config)
    await cg.register_parented(var, config[CONF_COSMIC_UNICORN_ID])
