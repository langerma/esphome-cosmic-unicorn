# esphome-cosmic-unicorn

ESPHome external component for the [Pimoroni Cosmic Unicorn](https://shop.pimoroni.com/products/space-unicorns?variant=40842632953939)
(32×32 RGB LED matrix with a Raspberry Pi Pico W).

The LED driver is Pimoroni's own PIO/DMA driver from
[pimoroni-pico](https://github.com/pimoroni/pimoroni-pico), wrapped as a normal ESPHome `display:` platform.
Every pixel is full RGB with 14-bit gamma-corrected brightness, done by the PIO hardware.

## Features

- `display: platform: cosmic_unicorn`, a regular ESPHome display: `lambda`, `pages`, fonts, images, `rotation`
  - drawing goes to a shadow framebuffer that is pushed to the panel once per update, so there is no tearing or
    flicker from clearing
- `output: platform: cosmic_unicorn` with `type: brightness` (global brightness, use with a `monochromatic` light)
  and `type: volume`
- speaker: `cosmic_unicorn.play_tone` / `cosmic_unicorn.stop_sound` actions (tones from Pimoroni's synth)
- light sensor: `id(unicorn).get_light_level()` (0–4095), e.g. in a template sensor
- buttons: plain `gpio` binary sensors (pins below)
- the PIO block is picked at runtime, so it coexists with the Pico W's Wi-Fi driver (which also uses a PIO)

## Usage

See [`example.yaml`](example.yaml) for a complete config (clock, brightness light, volume, light sensor, all buttons).

```yaml
rp2:
  board: rpipicow

external_components:
  - source:
      type: git
      url: https://github.com/langerma/esphome-cosmic-unicorn
      ref: main
    components: [cosmic_unicorn]

display:
  - platform: cosmic_unicorn
    id: unicorn
    update_interval: 50ms
    brightness: 0.5   # optional, 0..1
    volume: 0.5       # optional, 0..1
    lambda: |-
      it.fill(Color(0, 0, 40));
      it.line(0, 0, 31, 31, Color(255, 0, 0));
```

### Actions

```yaml
- cosmic_unicorn.play_tone:
    id: unicorn
    frequency: 880        # Hz, templatable
    duration: 200ms       # templatable, default 200ms
    waveform: sine        # sine | square | triangle | saw | noise, default square
- cosmic_unicorn.stop_sound: unicorn
```

### Pins

| Function          | GPIO |
|-------------------|------|
| A / B / C / D     | 0 / 1 / 3 / 6 |
| Sleep (Zzz)       | 27 |
| Brightness + / −  | 21 / 26 |
| Volume + / −      | 7 / 8 |
| Light sensor (ADC2) | 28 |
| Qw/ST I²C SDA / SCL | 4 / 5 |

Buttons are active low, use `inverted: true` with `pullup: true`.
GPIO 9–11, 13–20 and 22 are used by the matrix and the audio amp; don't configure them.

### First flash

Build in the ESPHome dashboard, choose **Download → UF2**, hold **BOOTSEL** on the Pico while plugging in USB, and
copy the `.uf2` onto the `RPI-RP2` drive. After that, OTA works as usual.

## Notes

- `components/esphome_galactic_unicorn` is the original Galactic Unicorn proof of concept by
  [hackcasual](https://github.com/hackcasual/esphome-galactic-unicorn), kept for reference. It isn't used by this
  component.
- Built and tested against ESPHome 2026.11 (arduino-pico 6.1.1).

## Credits and licenses

- LED matrix driver, synth and gamma tables: [Pimoroni pimoroni-pico](https://github.com/pimoroni/pimoroni-pico), MIT.
- `audio_i2s` PIO program: Raspberry Pi (Trading) Ltd., BSD-3-Clause.
- ESPHome integration and original proof of concept: MIT, see [LICENSE](LICENSE).
