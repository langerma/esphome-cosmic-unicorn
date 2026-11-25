#pragma once

#ifdef USE_RP2

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/output/float_output.h"

#include "cosmic_unicorn_driver.hpp"
#include "cosmic_unicorn_effects.h"

namespace esphome::cosmic_unicorn {

enum class ToneWaveform : uint8_t { SINE, SQUARE, TRIANGLE, SAW, NOISE };

class CosmicUnicornDisplay : public display::DisplayBuffer {
 public:
  static constexpr int WIDTH = pimoroni::CosmicUnicorn::WIDTH;
  static constexpr int HEIGHT = pimoroni::CosmicUnicorn::HEIGHT;

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  void fill(Color color) override;

  /// Global brightness, 0.0 - 1.0 (applied on top of per-pixel values).
  void set_brightness(float brightness);
  float get_brightness() const { return this->brightness_; }

  /// Speaker volume, 0.0 - 1.0.
  void set_volume(float volume);
  float get_volume() const { return this->volume_; }

  /// Raw light sensor reading (0 - 4095).
  uint16_t get_light_level();

  /// Auto brightness: scales the global brightness with the ambient light.
  /// The light sensor is mapped logarithmically from dark_level..bright_level
  /// onto min_factor..max_factor (dark_level may be above bright_level if the
  /// sensor reads inverted).
  void set_auto_brightness(bool enabled);
  bool get_auto_brightness() const { return this->auto_enabled_; }
  void set_auto_brightness_config(uint16_t dark_level, uint16_t bright_level, float min_factor, float max_factor) {
    this->dark_level_ = dark_level;
    this->bright_level_ = bright_level;
    this->min_factor_ = min_factor;
    this->max_factor_ = max_factor;
  }
  /// Current ambient factor (1.0 when auto brightness is off).
  float get_ambient_factor() const { return this->auto_enabled_ ? this->ambient_factor_ : 1.0f; }
  /// Smoothed light sensor reading used by auto brightness.
  float get_smoothed_light_level() const { return this->smoothed_light_; }

  /// Play a tone on the built-in speaker. Non-blocking.
  void play_tone(float frequency, uint32_t duration_ms, ToneWaveform waveform = ToneWaveform::SQUARE);
  void stop_sound();

  /// Draw one frame of a built-in full-screen effect (see EffectRenderer::Effect for the order:
  /// Plasma, Fire, Lava lamp, Snow, Rain, Starfield, Stars, Game of Life, Lines, Circles, Aurora).
  /// Call it first in the display lambda; anything drawn afterwards goes on top.
  void render_effect(display::Display &it, size_t effect, float speed = 1.0f) {
    this->effects_.render(it, effect, speed);
  }
  static size_t effect_count() { return EffectRenderer::effect_count(); }
  static const char *effect_name(size_t effect) { return EffectRenderer::effect_name(effect); }

  /// Read a button directly (true = pressed). Pin numbers as in pimoroni::CosmicUnicorn::SWITCH_*.
  bool is_pressed(uint8_t pin) { return this->unicorn_.is_pressed(pin); }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_width_internal() override { return WIDTH; }
  int get_height_internal() override { return HEIGHT; }

  void flush_();
  void apply_brightness_();
  void update_ambient_();

  pimoroni::CosmicUnicorn unicorn_;
  EffectRenderer effects_;
  // shadow framebuffer: drawing happens here, then the whole frame is pushed at once
  // so the panel never shows a half-drawn (e.g. just cleared) frame
  uint8_t frame_[HEIGHT][WIDTH][3]{};
  bool dirty_{true};
  bool ready_{false};

  float brightness_{0.5f};
  float volume_{0.5f};

  bool auto_enabled_{false};
  uint16_t dark_level_{10};
  uint16_t bright_level_{1500};
  float min_factor_{0.1f};
  float max_factor_{1.0f};
  float smoothed_light_{-1.0f};
  float target_factor_{1.0f};   // from the latest (smoothed) reading
  float ambient_factor_{1.0f};  // currently applied to the panel
  uint32_t last_ambient_ms_{0};

  bool tone_active_{false};
  bool tone_releasing_{false};
  uint32_t tone_until_{0};
};

class BrightnessOutput : public output::FloatOutput, public Parented<CosmicUnicornDisplay> {
 protected:
  void write_state(float state) override { this->parent_->set_brightness(state); }
};

class VolumeOutput : public output::FloatOutput, public Parented<CosmicUnicornDisplay> {
 protected:
  void write_state(float state) override { this->parent_->set_volume(state); }
};

template<typename... Ts> class PlayToneAction : public Action<Ts...>, public Parented<CosmicUnicornDisplay> {
 public:
  TEMPLATABLE_VALUE(float, frequency)
  TEMPLATABLE_VALUE(uint32_t, duration)
  void set_waveform(ToneWaveform waveform) { this->waveform_ = waveform; }

  void play(const Ts &...x) override {
    this->parent_->play_tone(this->frequency_.value(x...), this->duration_.value(x...), this->waveform_);
  }

 protected:
  ToneWaveform waveform_{ToneWaveform::SQUARE};
};

template<typename... Ts> class StopSoundAction : public Action<Ts...>, public Parented<CosmicUnicornDisplay> {
 public:
  void play(const Ts &...x) override { this->parent_->stop_sound(); }
};

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
