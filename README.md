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
- auto brightness from the light sensor, on the device (see below)
- animated GIFs downloaded at runtime (`cosmic_unicorn.play_gif`), scaled to fit; pictures (PNG/JPEG) via ESPHome's
  `online_image`, both pushable from Home Assistant
- 11 built-in full-screen effects (Plasma, Fire, Lava lamp, Snow, Rain, Starfield, Stars, Game of Life, Lines,
  Circles, Aurora), with afterglow trails, drawn with `id(unicorn).render_effect(it, index, speed)`
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

### Auto brightness

```yaml
display:
  - platform: cosmic_unicorn
    id: unicorn
    auto_brightness:
      enabled: true        # default true; toggle at runtime with id(unicorn).set_auto_brightness(bool)
      dark_level: 25       # raw sensor reading treated as "dark"
      bright_level: 800    # raw sensor reading treated as "bright"
      min_brightness: 25%  # factor at dark_level
      max_brightness: 100% # factor at bright_level
```

The sensor is sampled every 200 ms and smoothed (about 2 s), mapped logarithmically between `dark_level` and
`bright_level`, and multiplied with the configured / light brightness. Changes under 1% are ignored, so the panel
doesn't flicker. `id(unicorn).get_ambient_factor()` and `get_smoothed_light_level()` are there for template sensors.

The on-board sensor is a plain phototransistor without calibration. On one unit it read about 21 both covered and
in evening lamp light, and about 1600 with a phone flashlight right in front of it. Use the light level sensor from
the example to find good levels for your room. If your unit reads inverted, set `dark_level` above `bright_level`.

Global brightness is applied after gamma at 16-bit precision, so colours stay clean at low brightness.

### Effects

```yaml
display:
  - platform: cosmic_unicorn
    id: unicorn
    lambda: |-
      id(unicorn).render_effect(it, 0);   // 0 = Plasma; anything drawn afterwards goes on top
```

Index order: 0 Plasma, 1 Fire, 2 Lava lamp, 3 Snow, 4 Rain, 5 Starfield, 6 Stars, 7 Game of Life, 8 Lines,
9 Circles, 10 Aurora (`CosmicUnicornDisplay::effect_name(i)` and `effect_count()` give names and the count).
The optional third argument scales the speed. Effects keep their own frame buffer, so trails and afterglow work
even if the lambda clears the screen. All per-pixel work is integer math, because the RP2040 has no FPU.

[`example.yaml`](example.yaml) puts the clock and all effects into a **Mode** select, adds an **Effect speed** slider,
and gives every button a short animation (B/C switch modes, brightness and volume show a level bar). Which
animation A–D play is configurable from Home Assistant ("Button X animation" config selects).

### GIFs and pictures from Home Assistant

```yaml
http_request:
  verify_ssl: false     # required on RP2040

display:
  - platform: cosmic_unicorn
    id: unicorn
    gif:
      max_size: 65536   # bytes, the whole GIF is held in RAM
    lambda: |-
      if (id(unicorn).render_gif(it)) return;   // a GIF is playing
      // ... normal content
```

`cosmic_unicorn.play_gif` (`url`, optional `duration`, 0 = loop until `cosmic_unicorn.stop_gif`) downloads the GIF and
plays it with its own frame timing. Any size works as long as the file fits in `max_size`: it is scaled to 32×32
keeping the aspect ratio. Pictures use ESPHome's `image: platform: online_image` with `format: AUTO`.

[`example.yaml`](example.yaml) exposes three Home Assistant actions:

```yaml
action: esphome.cosmic_unicorn_show_gif
data:
  url: https://raw.githubusercontent.com/langerma/esphome-cosmic-unicorn/main/examples/demo.gif
  seconds: 15        # 0 = until clear_media or the next picture/GIF
---
action: esphome.cosmic_unicorn_show_image
data:
  url: http://192.168.1.10:8123/local/picture.png
  seconds: 15
---
action: esphome.cosmic_unicorn_clear_media
```

Files in Home Assistant's `config/www/` folder are served at `http://<ha-ip>:8123/local/`. Plain `http://` on the
LAN is the most reliable; HTTPS works but without certificate checks.

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
- Built against ESPHome 2026.11 (arduino-pico 6.1.1) and tested on hardware (display, brightness, buttons, speaker, light sensor).

## Credits and licenses

- LED matrix driver, synth and gamma tables: [Pimoroni pimoroni-pico](https://github.com/pimoroni/pimoroni-pico), MIT.
- `audio_i2s` PIO program: Raspberry Pi (Trading) Ltd., BSD-3-Clause.
- GIF decoder: [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) 2.2.0, Apache-2.0
  (vendored with a small platform-detection patch, see `components/cosmic_unicorn/LICENSE-AnimatedGIF`).
- ESPHome integration and original proof of concept: MIT, see [LICENSE](LICENSE).
