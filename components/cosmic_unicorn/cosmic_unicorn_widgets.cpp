#include "cosmic_unicorn_widgets.h"

#ifdef USE_RP2

#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"

namespace esphome::cosmic_unicorn {

// ---------- 3x5 font ----------

struct Glyph {
  char c;
  uint8_t width;
  uint8_t rows[5];  // bit 2 = left column for 3-wide glyphs
};

// clang-format off
static const Glyph GLYPHS[] = {
  {'0', 3, {7, 5, 5, 5, 7}}, {'1', 3, {2, 6, 2, 2, 7}}, {'2', 3, {7, 1, 7, 4, 7}}, {'3', 3, {7, 1, 7, 1, 7}},
  {'4', 3, {5, 5, 7, 1, 1}}, {'5', 3, {7, 4, 7, 1, 7}}, {'6', 3, {7, 4, 7, 5, 7}}, {'7', 3, {7, 1, 2, 2, 2}},
  {'8', 3, {7, 5, 7, 5, 7}}, {'9', 3, {7, 5, 7, 1, 7}},
  {'A', 3, {2, 5, 7, 5, 5}}, {'B', 3, {6, 5, 6, 5, 6}}, {'C', 3, {3, 4, 4, 4, 3}}, {'D', 3, {6, 5, 5, 5, 6}},
  {'E', 3, {7, 4, 6, 4, 7}}, {'F', 3, {7, 4, 6, 4, 4}}, {'G', 3, {3, 4, 5, 5, 3}}, {'H', 3, {5, 5, 7, 5, 5}},
  {'I', 3, {7, 2, 2, 2, 7}}, {'J', 3, {1, 1, 1, 5, 2}}, {'K', 3, {5, 5, 6, 5, 5}}, {'L', 3, {4, 4, 4, 4, 7}},
  {'M', 3, {5, 7, 7, 5, 5}}, {'N', 3, {6, 5, 5, 5, 5}}, {'O', 3, {2, 5, 5, 5, 2}}, {'P', 3, {6, 5, 6, 4, 4}},
  {'Q', 3, {2, 5, 5, 6, 3}}, {'R', 3, {6, 5, 6, 5, 5}}, {'S', 3, {3, 4, 2, 1, 6}}, {'T', 3, {7, 2, 2, 2, 2}},
  {'U', 3, {5, 5, 5, 5, 7}}, {'V', 3, {5, 5, 5, 5, 2}}, {'W', 3, {5, 5, 7, 7, 5}}, {'X', 3, {5, 5, 2, 5, 5}},
  {'Y', 3, {5, 5, 2, 2, 2}}, {'Z', 3, {7, 1, 2, 4, 7}},
  {'-', 2, {0, 0, 3, 0, 0}}, {'+', 3, {0, 2, 7, 2, 0}}, {'/', 3, {1, 1, 2, 4, 4}}, {'%', 3, {5, 1, 2, 4, 5}},
  {':', 1, {0, 1, 0, 1, 0}}, {'.', 1, {0, 0, 0, 0, 1}}, {' ', 2, {0, 0, 0, 0, 0}},
  {'\xb0', 3, {2, 5, 2, 0, 0}},  // degree sign (Latin-1 / second byte of UTF-8)
};
// clang-format on

static const Glyph *find_glyph(char c) {
  if (c >= 'a' && c <= 'z')
    c = c - 'a' + 'A';
  for (const auto &g : GLYPHS)
    if (g.c == c)
      return &g;
  return nullptr;
}

int text_width(const char *text, int scale) {
  int w = 0;
  for (const char *p = text; *p; p++) {
    if ((uint8_t) *p == 0xC2)
      continue;  // UTF-8 lead byte of the degree sign
    const Glyph *g = find_glyph(*p);
    if (g == nullptr)
      continue;
    w += (g->width + 1) * scale;
  }
  return w > 0 ? w - scale : 0;
}

int draw_text(display::Display &it, int x, int y, const char *text, Color color, int scale) {
  for (const char *p = text; *p; p++) {
    if ((uint8_t) *p == 0xC2)
      continue;
    const Glyph *g = find_glyph(*p);
    if (g == nullptr)
      continue;
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < g->width; col++) {
        if (!(g->rows[row] & (1 << (g->width - 1 - col))))
          continue;
        if (scale == 1) {
          it.draw_pixel_at(x + col, y + row, color);
        } else {
          it.filled_rectangle(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += (g->width + 1) * scale;
  }
  return x - scale;
}

// ---------- icons ----------

static const Color SUN(255, 190, 30);
static const Color SUN_RAY(255, 140, 0);
static const Color CLOUD_HI(235, 240, 255);
static const Color CLOUD_LO(140, 150, 175);
static const Color DARK_HI(150, 155, 175);
static const Color DARK_LO(80, 85, 105);
static const Color RAIN(40, 130, 255);
static const Color SNOW(230, 245, 255);
static const Color BOLT(255, 230, 40);
static const Color MOON(240, 225, 150);
static const Color FOG(150, 160, 170);

static inline Color mix(Color a, Color b, float t) {
  return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}

static void sun(display::Display &it, float cx, float cy, float r, uint32_t t) {
  const float spin = t * 0.0006f;
  for (int k = 0; k < 8; k++) {
    const float a = spin + k * (float) M_PI / 4;
    const float c = cosf(a), s = sinf(a);
    it.draw_pixel_at((int) lroundf(cx + c * (r + 1.6f)), (int) lroundf(cy + s * (r + 1.6f)), SUN_RAY);
    it.draw_pixel_at((int) lroundf(cx + c * (r + 2.6f)), (int) lroundf(cy + s * (r + 2.6f)), SUN_RAY);
  }
  const int x0 = (int) floorf(cx - r), x1 = (int) ceilf(cx + r);
  const int y0 = (int) floorf(cy - r), y1 = (int) ceilf(cy + r);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const float dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= r * r + 0.5f)
        it.draw_pixel_at(x, y, SUN);
    }
}

// cloud in a 12x12 box: three bumps on a flat base, shaded light at the top
static void cloud(display::Display &it, int ox, int oy, Color hi, Color lo) {
  struct C {
    float x, y, r;
  };
  static const C BUMPS[] = {{3.2f, 7.4f, 2.6f}, {6.4f, 5.2f, 3.4f}, {9.0f, 7.3f, 2.5f}};
  for (int y = 1; y <= 10; y++) {
    for (int x = 0; x <= 11; x++) {
      bool in = (y >= 7 && y <= 9 && x >= 3 && x <= 9);
      for (const auto &b : BUMPS) {
        const float dx = x - b.x, dy = y - b.y;
        if (dx * dx + dy * dy <= b.r * b.r)
          in = true;
      }
      if (in && y <= 9)
        it.draw_pixel_at(ox + x, oy + y, mix(hi, lo, (y - 1) / 9.0f));
    }
  }
}

static void moon(display::Display &it, int ox, int oy, uint32_t t) {
  // crescent: disc minus an offset disc
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 12; x++) {
      const float dx = x - 5.5f, dy = y - 5.5f;
      const float ex = x - 8.0f, ey = y - 4.0f;
      if (dx * dx + dy * dy <= 20.0f && ex * ex + ey * ey > 14.0f)
        it.draw_pixel_at(ox + x, oy + y, MOON);
    }
  // twinkling star
  const uint8_t v = 120 + (uint8_t) (120 * (0.5f + 0.5f * sinf(t * 0.004f)));
  it.draw_pixel_at(ox + 10, oy + 1, Color(v, v, v));
}

static void precipitation(display::Display &it, int ox, int oy, uint32_t t, int columns, bool rain, bool snow) {
  static const int XS[] = {2, 4, 6, 8, 10};
  const int start = columns >= 5 ? 0 : 1;
  const int step = columns >= 5 ? 1 : 2;
  int i = 0;
  for (int c = start; c < 5; c += step, i++) {
    const bool is_snow = snow && (!rain || (i % 2 == 1));
    const uint32_t speed = is_snow ? 140 : 70;
    // two drops per column, falling through rows 8..11 below the cloud
    for (int d = 0; d < 2; d++) {
      const int phase = (int) ((t / speed + i * 3 + d * 2) % 4);  // 0..3
      const int x = ox + XS[c] + (is_snow ? (int) ((t / 400 + i + d) % 2) : 0);
      const int y = oy + 8 + phase;
      if (is_snow) {
        it.draw_pixel_at(x, y, SNOW);
      } else {
        it.draw_pixel_at(x, y, RAIN);
        if (y - 1 >= oy + 8)
          it.draw_pixel_at(x, y - 1, Color(20, 70, 140));
      }
    }
  }
}

static void big_icon(display::Display &it, const char *c, int x, int y, uint32_t t) {
  auto is = [c](const char *s) { return strcmp(c, s) == 0; };
  if (is("sunny")) {
    sun(it, x + 5.5f, y + 5.5f, 3.0f, t);
  } else if (is("clear-night")) {
    moon(it, x, y, t);
  } else if (is("partlycloudy")) {
    sun(it, x + 4.0f, y + 3.5f, 2.2f, t);
    cloud(it, x + 1, y + 2, CLOUD_HI, CLOUD_LO);
  } else if (is("cloudy")) {
    cloud(it, x, y + 1, CLOUD_HI, CLOUD_LO);
  } else if (is("rainy") || is("pouring")) {
    cloud(it, x, y - 2, DARK_HI, DARK_LO);
    precipitation(it, x, y, t, is("pouring") ? 5 : 3, true, false);
  } else if (is("snowy") || is("hail")) {
    cloud(it, x, y - 2, CLOUD_HI, CLOUD_LO);
    precipitation(it, x, y, t, 3, false, true);
  } else if (is("snowy-rainy")) {
    cloud(it, x, y - 2, DARK_HI, DARK_LO);
    precipitation(it, x, y, t, 5, true, true);
  } else if (is("lightning") || is("lightning-rainy")) {
    cloud(it, x, y - 2, DARK_HI, DARK_LO);
    if (is("lightning-rainy"))
      precipitation(it, x, y, t, 3, true, false);
    if ((t / 150) % 10 < 7) {  // flicker
      static const int8_t BOLT_PX[][2] = {{6, 7}, {5, 8}, {4, 9}, {5, 9}, {6, 9}, {5, 10}, {4, 11}};
      for (const auto &p : BOLT_PX)
        it.draw_pixel_at(x + p[0], y + p[1], BOLT);
    }
  } else if (is("fog")) {
    for (int band = 0; band < 4; band++) {
      const int shift = (int) lroundf(1.5f * sinf(t * 0.002f + band));
      const int yy = y + 2 + band * 3;
      it.horizontal_line(x + 1 + shift + (band % 2), yy, 9, mix(FOG, Color(60, 65, 75), band / 4.0f));
    }
  } else if (is("windy") || is("windy-variant")) {
    const int off = (int) ((t / 120) % 12);
    for (int band = 0; band < 3; band++) {
      const int yy = y + 3 + band * 3;
      for (int k = 0; k < 10 - band * 2; k++) {
        const bool gap = ((k + off + band * 4) % 12) < 2;
        if (!gap)
          it.draw_pixel_at(x + 1 + k, yy, CLOUD_HI);
      }
      it.draw_pixel_at(x + 11 - band * 2, yy - 1, CLOUD_HI);  // curl
    }
    if (is("windy-variant"))
      cloud(it, x + 3, y - 4, DARK_HI, DARK_LO);
  } else {  // exceptional / unknown
    const Color red(255, 50, 40);
    it.filled_rectangle(x + 5, y + 1, 2, 7, red);
    it.filled_rectangle(x + 5, y + 9, 2, 2, red);
  }
}

// 5x5 icons: one char per pixel. . = off, Y sun, O ray/orange, W white, G grey, D dark grey, B blue, M moon, R red
struct SmallIcon {
  const char *name;
  const char *px;
};
// clang-format off
static const SmallIcon SMALL[] = {
  {"sunny",           "O.Y.O" ".YYY." "YYYYY" ".YYY." "O.Y.O"},
  {"clear-night",     ".MMM." "MM..." "MM..." "MM..." ".MMM."},
  {"partlycloudy",    "Y.Y.." ".YY.." "YWWW." "WWWWW" "....."},
  {"cloudy",          "....." ".WW.." "WWWW." "GGGGG" "....."},
  {"rainy",           ".GGG." "GGGGG" "....." "B.B.B" ".B.B."},
  {"pouring",         ".DDD." "DDDDD" "B.B.B" ".B.B." "B.B.B"},
  {"snowy",           ".WWW." "GGGGG" "....." "W.W.W" ".W.W."},
  {"hail",            ".WWW." "GGGGG" "....." "W.W.W" ".W.W."},
  {"snowy-rainy",     ".GGG." "GGGGG" "....." "B.W.B" ".W.B."},
  {"lightning",       ".DDD." "DDDDD" "..Y.." ".Y..." "..Y.."},
  {"lightning-rainy", ".DDD." "DDDDD" "B.Y.B" ".Y..." "..Y.."},
  {"fog",             "GGGG." "....." ".GGGG" "....." "GGGG."},
  {"windy",           "WWW.." "...W." "WWWW." "....W" "WWW.."},
  {"windy-variant",   "WWW.." "...W." "WWWW." "....W" "WWW.."},
};
// clang-format on

static Color small_color(char ch) {
  switch (ch) {
    case 'Y':
      return SUN;
    case 'O':
      return SUN_RAY;
    case 'W':
      return CLOUD_HI;
    case 'G':
      return CLOUD_LO;
    case 'D':
      return DARK_LO;
    case 'B':
      return RAIN;
    case 'M':
      return MOON;
    default:
      return Color(255, 50, 40);
  }
}

void draw_weather_icon(display::Display &it, const char *condition, int x, int y, int size) {
  if (condition == nullptr)
    condition = "";
  if (size >= 10) {
    big_icon(it, condition, x, y, millis());
    return;
  }
  const char *px = "..R..""..R..""..R..""....."  "..R..";  // unknown: "!"
  for (const auto &s : SMALL)
    if (strcmp(s.name, condition) == 0)
      px = s.px;
  for (int i = 0; i < 25; i++)
    if (px[i] != '.')
      it.draw_pixel_at(x + i % 5, y + i / 5, small_color(px[i]));
}

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
