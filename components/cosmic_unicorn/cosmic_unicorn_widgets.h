#pragma once

#include "esphome/core/defines.h"

#ifdef USE_RP2

#include <cstdint>

#include "esphome/components/display/display.h"

// Small drawing helpers for 32x32 panels: a crisp 3x5 pixel font and weather icons.
// Usable from any display lambda, e.g. cosmic_unicorn::draw_text(it, 0, 0, "23:41", Color(255, 255, 255));
namespace esphome::cosmic_unicorn {

/// Width in pixels of text in the 3x5 font (1 px gap between glyphs, times scale).
/// Supported: 0-9 A-Z (lower case is mapped to upper case) space - : . / % + and the degree sign.
int text_width(const char *text, int scale = 1);

/// Draw text in the 3x5 font with its top-left corner at x/y. Returns the x after the last glyph.
int draw_text(display::Display &it, int x, int y, const char *text, Color color, int scale = 1);

/// Weather icon for a Home Assistant condition (sunny, clear-night, partlycloudy, cloudy, rainy,
/// pouring, snowy, snowy-rainy, hail, lightning, lightning-rainy, fog, windy, windy-variant, exceptional).
/// size 12 draws a large, gently animated icon (x/y = top-left of a 12x12 box);
/// size 5 draws a small static 5x5 icon.
void draw_weather_icon(display::Display &it, const char *condition, int x, int y, int size = 12);

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
