#pragma once

#ifdef USE_RP2

#include <cstdint>
#include <cstddef>

#include "esphome/components/display/display.h"

namespace esphome::cosmic_unicorn {

/// Full-screen animated effects for a 32x32 RGB matrix.
/// Super computer is based on Pimoroni's "eighties super computer" example (pimoroni-pico, MIT).
///
/// Effects draw into an internal "glow" buffer that persists between frames
/// and fades out over time, which gives trails and afterglow. The result is
/// copied to the display on every render() call, so the effect is drawn
/// first in a display lambda and other content can be drawn on top of it.
class EffectRenderer {
 public:
  static constexpr int W = 32;
  static constexpr int H = 32;

  enum Effect : uint8_t {
    PLASMA = 0,
    FIRE,
    LAVA_LAMP,
    SNOW,
    RAIN,
    STARFIELD,
    STARS,
    GAME_OF_LIFE,
    LINES,
    CIRCLES,
    AURORA,
    SUPER_COMPUTER,
    EFFECT_COUNT,
  };

  static const char *effect_name(size_t index);
  static size_t effect_count() { return EFFECT_COUNT; }

  /// Render one frame of the effect. speed scales the animation time (1.0 = normal).
  void render(display::Display &it, size_t effect, float speed = 1.0f);

 protected:
  // glow buffer (8-bit RGB), persists between frames
  uint8_t buf_[H][W][3]{};

  void fade_(float factor);
  void clear_();
  void set_(int x, int y, Color c);
  void add_(int x, int y, Color c);
  void blit_(display::Display &it);
  void line_(int x0, int y0, int x1, int y1, Color c);
  void circle_(int cx, int cy, int r, Color c);

  void plasma_(float t);
  void fire_(float dt);
  void lava_(float t);
  void snow_(float dt);
  void rain_(float dt);
  void starfield_(float dt);
  void stars_(float dt);
  void life_(float dt);
  void lines_(float t, float dt);
  void circles_(float t, float dt);
  void aurora_(float t);
  void super_computer_(float dt);

  size_t last_effect_{SIZE_MAX};
  bool reset_{true};
  uint32_t last_ms_{0};
  float time_{0};

  // effect state (shared memory is fine, only one effect runs at a time)
  uint16_t heat_[H + 2][W]{};  // fire: 0..1023
  uint8_t pile_[W]{};
  struct Particle {
    float x, y, z, vx, vy;
    float hue;
    float life;
  };
  static constexpr int MAX_PARTICLES = 64;
  Particle parts_[MAX_PARTICLES]{};
  uint8_t grid_[H][W]{};  // game of life: 0 dead, otherwise age
  float step_acc_{0};
  uint32_t gen_{0};
  uint32_t hist_[4]{};
  int stuck_{0};
  // super computer: per-LED age and lifetime in 1/1000 of a cycle
  uint16_t sc_age_[H][W]{};
  uint16_t sc_life_[H][W]{};
  float sc_acc_{0};
};

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
