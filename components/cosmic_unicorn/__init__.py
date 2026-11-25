"""ESPHome component for the Pimoroni Cosmic Unicorn (32x32 RGB LED matrix, Raspberry Pi Pico W)."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import display
import esphome.config_validation as cv
from esphome.const import CONF_FREQUENCY, CONF_ID

CODEOWNERS = ["@langerma"]

CONF_COSMIC_UNICORN_ID = "cosmic_unicorn_id"
CONF_DURATION = "duration"
CONF_WAVEFORM = "waveform"

cosmic_unicorn_ns = cg.esphome_ns.namespace("cosmic_unicorn")
CosmicUnicornDisplay = cosmic_unicorn_ns.class_(
    "CosmicUnicornDisplay", display.DisplayBuffer
)
PlayToneAction = cosmic_unicorn_ns.class_("PlayToneAction", automation.Action)
StopSoundAction = cosmic_unicorn_ns.class_("StopSoundAction", automation.Action)

ToneWaveform = cosmic_unicorn_ns.enum("ToneWaveform", is_class=True)
WAVEFORMS = {
    "sine": ToneWaveform.SINE,
    "square": ToneWaveform.SQUARE,
    "triangle": ToneWaveform.TRIANGLE,
    "saw": ToneWaveform.SAW,
    "noise": ToneWaveform.NOISE,
}


@automation.register_action(
    "cosmic_unicorn.play_tone",
    PlayToneAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(CosmicUnicornDisplay),
            cv.Required(CONF_FREQUENCY): cv.templatable(cv.positive_float),
            cv.Optional(
                CONF_DURATION, default="200ms"
            ): cv.templatable(cv.positive_time_period_milliseconds),
            cv.Optional(CONF_WAVEFORM, default="square"): cv.enum(WAVEFORMS, lower=True),
        }
    ),
    synchronous=True,
)
async def play_tone_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    freq = await cg.templatable(config[CONF_FREQUENCY], args, cg.float_)
    cg.add(var.set_frequency(freq))
    duration = await cg.templatable(config[CONF_DURATION], args, cg.uint32)
    cg.add(var.set_duration(duration))
    cg.add(var.set_waveform(config[CONF_WAVEFORM]))
    return var


@automation.register_action(
    "cosmic_unicorn.stop_sound",
    StopSoundAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(CosmicUnicornDisplay)}),
    synchronous=True,
)
async def stop_sound_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
