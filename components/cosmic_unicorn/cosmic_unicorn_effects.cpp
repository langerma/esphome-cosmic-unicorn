#ifdef USE_RP2

#include "cosmic_unicorn_effects.h"

#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

// The RP2040 has no FPU, so per-pixel work uses integer math and lookup tables.
// Floats are only used for a handful of particles per frame.

namespace esphome::cosmic_unicorn {

static const char *const EFFECT_NAMES[] = {
    "Plasma", "Fire", "Lava lamp", "Snow", "Rain", "Starfield", "Stars", "Game of Life", "Lines", "Circles", "Aurora",
};

const char *EffectRenderer::effect_name(size_t index) {
  return index < EFFECT_COUNT ? EFFECT_NAMES[index] : "";
}

// ---------- integer helpers ----------

// 128 + 127 * sin(2*pi*i/256)
static uint8_t SIN8[256];
static bool sin8_ready = false;
static inline uint8_t sin8(int i) { return SIN8[i & 0xff]; }
static void init_sin8() {
  if (sin8_ready)
    return;
  for (int i = 0; i < 256; i++)
    SIN8[i] = (uint8_t) lroundf(128.0f + 127.0f * sinf(i * 2.0f * (float) M_PI / 256.0f));
  sin8_ready = true;
}

static inline uint32_t xorshift() {
  static uint32_t s = 0x9E3779B9u;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
static inline float frand() { return (xorshift() & 0xffffff) / 16777216.0f; }

// hue 0..255, sat 0..255, val 0..255
static Color hsv8(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0)
    return Color(v, v, v);
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0:
      return Color(v, t, p);
    case 1:
      return Color(q, v, p);
    case 2:
      return Color(p, v, t);
    case 3:
      return Color(p, q, v);
    case 4:
      return Color(t, p, v);
    default:
      return Color(v, p, q);
  }
}

static inline Color scale8(Color c, uint8_t f) { return Color((c.r * f) >> 8, (c.g * f) >> 8, (c.b * f) >> 8); }

// gradient through a list of (position 0..255, colour) stops
struct Stop {
  uint8_t pos;
  uint8_t r, g, b;
};
static Color gradient(const Stop *stops, int n, int v) {
  if (v <= stops[0].pos)
    return Color(stops[0].r, stops[0].g, stops[0].b);
  for (int i = 1; i < n; i++) {
    if (v <= stops[i].pos) {
      const Stop &a = stops[i - 1], &b = stops[i];
      int f = ((v - a.pos) << 8) / (b.pos - a.pos);
      return Color(a.r + (((b.r - a.r) * f) >> 8), a.g + (((b.g - a.g) * f) >> 8), a.b + (((b.b - a.b) * f) >> 8));
    }
  }
  const Stop &l = stops[n - 1];
  return Color(l.r, l.g, l.b);
}

// ---------- glow buffer ----------

void EffectRenderer::clear_() { memset(this->buf_, 0, sizeof(this->buf_)); }

void EffectRenderer::fade_(float factor) {
  const int f = (int) (factor * 256.0f);
  uint8_t *p = &this->buf_[0][0][0];
  for (size_t i = 0; i < sizeof(this->buf_); i++)
    p[i] = (p[i] * f) >> 8;
}

void EffectRenderer::set_(int x, int y, Color c) {
  if (x < 0 || x >= W || y < 0 || y >= H)
    return;
  uint8_t *px = this->buf_[y][x];
  px[0] = c.r;
  px[1] = c.g;
  px[2] = c.b;
}

void EffectRenderer::add_(int x, int y, Color c) {
  if (x < 0 || x >= W || y < 0 || y >= H)
    return;
  uint8_t *px = this->buf_[y][x];
  px[0] = std::min(255, px[0] + c.r);
  px[1] = std::min(255, px[1] + c.g);
  px[2] = std::min(255, px[2] + c.b);
}

void EffectRenderer::line_(int x0, int y0, int x1, int y1, Color c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int guard = 0; guard < 128; guard++) {
    this->add_(x0, y0, c);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void EffectRenderer::circle_(int cx, int cy, int r, Color c) {
  if (r <= 0) {
    this->add_(cx, cy, c);
    return;
  }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    this->add_(cx + x, cy + y, c);
    this->add_(cx + y, cy + x, c);
    this->add_(cx - y, cy + x, c);
    this->add_(cx - x, cy + y, c);
    this->add_(cx - x, cy - y, c);
    this->add_(cx - y, cy - x, c);
    this->add_(cx + y, cy - x, c);
    this->add_(cx + x, cy - y, c);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void EffectRenderer::blit_(display::Display &it) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t *px = this->buf_[y][x];
      it.draw_pixel_at(x, y, Color(px[0], px[1], px[2]));
    }
}

// ---------- main entry ----------

void EffectRenderer::render(display::Display &it, size_t effect, float speed) {
  init_sin8();
  if (effect >= EFFECT_COUNT)
    effect = 0;

  const uint32_t now = millis();
  float dt = this->last_ms_ == 0 ? 0.05f : (now - this->last_ms_) / 1000.0f;
  this->last_ms_ = now;
  if (dt > 0.2f || effect != this->last_effect_)
    dt = 0.05f;  // after a pause or an effect switch, don't jump
  if (speed <= 0.0f)
    speed = 1.0f;
  dt *= speed;
  this->time_ += dt;

  if (effect != this->last_effect_) {
    this->last_effect_ = effect;
    this->reset_ = true;
    this->clear_();
  }

  switch (effect) {
    case PLASMA:
      this->plasma_(this->time_);
      break;
    case FIRE:
      this->fire_(dt);
      break;
    case LAVA_LAMP:
      this->lava_(this->time_);
      break;
    case SNOW:
      this->snow_(dt);
      break;
    case RAIN:
      this->rain_(dt);
      break;
    case STARFIELD:
      this->starfield_(dt);
      break;
    case STARS:
      this->stars_(dt);
      break;
    case GAME_OF_LIFE:
      this->life_(dt);
      break;
    case LINES:
      this->lines_(this->time_, dt);
      break;
    case CIRCLES:
      this->circles_(this->time_, dt);
      break;
    case AURORA:
      this->aurora_(this->time_);
      break;
  }
  this->reset_ = false;
  this->blit_(it);
}

// ---------- effects ----------

// Plasma: four interfering sine fields, slowly rotating through the hue wheel.
void EffectRenderer::plasma_(float t) {
  const int t1 = (int) (t * 40), t2 = (int) (t * 27), t3 = (int) (t * 19), t4 = (int) (t * 53);
  // a slowly wandering centre for the radial term
  const int cx = 16 + ((sin8((int) (t * 11)) - 128) >> 3);
  const int cy = 16 + ((sin8((int) (t * 7) + 64) - 128) >> 3);
  const uint8_t hue_shift = (uint8_t) (t * 12);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int dx = x - cx, dy = y - cy;
      int v = sin8(x * 7 + t1) + sin8(y * 9 - t2) + sin8((x + y) * 5 + t3) + sin8(((dx * dx + dy * dy) >> 2) - t4);
      v >>= 2;  // 0..255
      const uint8_t val = 120 + (sin8(v * 2 + t1) >> 1);
      this->set_(x, y, hsv8(v + hue_shift, 230, val));
    }
  }
}

// Fire: heat diffuses upwards from random seeds at the bottom.
void EffectRenderer::fire_(float dt) {
  static const Stop PALETTE[] = {
      {0, 0, 0, 0}, {60, 60, 0, 0}, {120, 200, 20, 0}, {170, 255, 110, 0}, {215, 255, 210, 40}, {255, 255, 255, 200},
  };
  if (this->reset_) {
    memset(this->heat_, 0, sizeof(this->heat_));
    this->step_acc_ = 0;
  }
  this->step_acc_ += dt;
  int steps = 0;
  while (this->step_acc_ >= 1.0f / 30.0f && steps < 3) {
    this->step_acc_ -= 1.0f / 30.0f;
    steps++;
    // new heat sources in the two hidden rows below the panel
    for (int x = 0; x < W; x++) {
      this->heat_[H][x] = (this->heat_[H][x] * 3) >> 2;
      this->heat_[H + 1][x] = (this->heat_[H + 1][x] * 3) >> 2;
    }
    for (int s = 0; s < 5; s++) {
      int px = 1 + xorshift() % (W - 2);
      for (int dx = -1; dx <= 1; dx++) {
        this->heat_[H][px + dx] = 1023;
        this->heat_[H + 1][px + dx] = 1023;
      }
    }
    // each pixel becomes the damped average of the pixels below it
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        const int l = x > 0 ? x - 1 : x, r = x < W - 1 ? x + 1 : x;
        int sum = this->heat_[y][x] + this->heat_[y + 1][x] + this->heat_[y + 2][x] + this->heat_[y + 1][l] +
                  this->heat_[y + 1][r];
        // damping is stronger near the top so the flames taper off
        int damp = 251 - ((H - y) >> 2);
        this->heat_[y][x] = (uint16_t) (((sum / 5) * damp) >> 8);
      }
    }
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      this->set_(x, y, gradient(PALETTE, 6, this->heat_[y][x] >> 2));
}

// Lava lamp: metaballs drifting on slow Lissajous paths.
void EffectRenderer::lava_(float t) {
  static const Stop PALETTE[] = {
      {0, 8, 0, 20}, {90, 60, 0, 70}, {140, 200, 0, 90}, {190, 255, 80, 20}, {235, 255, 200, 60}, {255, 255, 240, 160},
  };
  constexpr int N = 5;
  int bx[N], by[N];
  const int ti = (int) (t * 16);
  for (int i = 0; i < N; i++) {
    bx[i] = 4 + ((sin8(ti * (i + 2) / 3 + i * 51) * 24) >> 8);
    by[i] = 2 + ((sin8(ti * (i + 3) / 4 + i * 97 + 64) * 28) >> 8);
  }
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int field = 0;
      for (int i = 0; i < N; i++) {
        const int dx = x - bx[i], dy = y - by[i];
        field += 2400 / (dx * dx + dy * dy + 8);
      }
      this->set_(x, y, gradient(PALETTE, 6, std::min(field, 255)));
    }
  }
}

// Snow: flakes on three depth layers, drifting, piling up and melting.
void EffectRenderer::snow_(float dt) {
  if (this->reset_) {
    for (auto &p : this->parts_) {
      p.x = frand() * W;
      p.y = -frand() * H;
      p.z = 0.3f + frand() * 0.7f;
      p.hue = frand() * 6.28f;
    }
    memset(this->pile_, 0, sizeof(this->pile_));
    this->step_acc_ = 0;
  }
  this->fade_(0.45f);
  // the pile slowly melts once it gets high
  int total = 0;
  for (int x = 0; x < W; x++)
    total += this->pile_[x];
  this->step_acc_ += dt;
  if (this->step_acc_ > (total > W * 5 ? 0.15f : 2.0f)) {
    this->step_acc_ = 0;
    for (int x = 0; x < W; x++)
      if (this->pile_[x] > 0 && (xorshift() & 3) == 0)
        this->pile_[x]--;
  }
  for (int i = 0; i < 40; i++) {
    Particle &p = this->parts_[i];
    p.y += (2.5f + 6.0f * p.z) * dt;
    p.hue += dt * (1.0f + p.z);
    p.x += sinf(p.hue) * 3.0f * p.z * dt;
    if (p.x < 0)
      p.x += W;
    if (p.x >= W)
      p.x -= W;
    const int gx = (int) p.x;
    if (p.z > 0.75f && p.y >= H - 1 - this->pile_[gx]) {
      if (this->pile_[gx] < 10)
        this->pile_[gx]++;
      p.y = -frand() * 8;
      continue;
    }
    if (p.y >= H) {
      p.y = -frand() * 8;
      continue;
    }
    const uint8_t v = 90 + (uint8_t) (p.z * 165);
    this->add_(gx, (int) p.y, Color(v * 7 / 8, v * 15 / 16, v));
  }
  for (int x = 0; x < W; x++)
    for (int k = 0; k < this->pile_[x]; k++) {
      const uint8_t v = 140 + k * 10;
      this->set_(x, H - 1 - k, Color(v * 3 / 4, v * 7 / 8, v));
    }
}

// Rain: fast streaks with fading trails and small splashes.
void EffectRenderer::rain_(float dt) {
  if (this->reset_) {
    for (auto &p : this->parts_) {
      p.x = (float) (xorshift() % W);
      p.y = -frand() * H;
      p.vy = 22 + frand() * 18;
      p.z = 0.4f + frand() * 0.6f;
    }
  }
  this->fade_(0.62f);
  for (int i = 0; i < 28; i++) {
    Particle &p = this->parts_[i];
    p.y += p.vy * dt;
    if (p.y >= H - 1) {
      const int x = (int) p.x;
      const Color splash(40, 90, 140);
      this->add_(x - 1, H - 1, splash);
      this->add_(x + 1, H - 1, splash);
      this->add_(x - 2, H - 2, scale8(splash, 120));
      this->add_(x + 2, H - 2, scale8(splash, 120));
      p.x = (float) (xorshift() % W);
      p.y = -frand() * 10;
      p.vy = 22 + frand() * 18;
      continue;
    }
    if (p.y >= 0) {
      const uint8_t v = (uint8_t) (p.z * 255);
      this->add_((int) p.x, (int) p.y, Color(v / 3, v * 2 / 3, v));
    }
  }
}

// Starfield: flying through space; stars streak, accelerate and slow down.
void EffectRenderer::starfield_(float dt) {
  if (this->reset_) {
    for (auto &p : this->parts_) {
      p.x = frand() * 2 - 1;
      p.y = frand() * 2 - 1;
      p.z = 0.05f + frand();
      p.hue = frand();
    }
  }
  const float warp = 0.35f + 0.3f * (sin8((int) (this->time_ * 20)) / 255.0f);
  this->fade_(0.55f + warp * 0.4f);
  for (auto &p : this->parts_) {
    p.z -= warp * dt;
    const int px = (int) lroundf(15.5f + p.x / p.z * 8.0f);
    const int py = (int) lroundf(15.5f + p.y / p.z * 8.0f);
    if (p.z <= 0.03f || px < 0 || px >= W || py < 0 || py >= H) {
      p.x = frand() * 2 - 1;
      p.y = frand() * 2 - 1;
      p.z = 1.0f;
      p.hue = frand();
      continue;
    }
    uint8_t v = (uint8_t) std::min(255.0f, (1.1f - p.z) * 255.0f);
    // mostly white, some bluish and some warm stars
    Color c = p.hue < 0.6f ? Color(v, v, v) : (p.hue < 0.8f ? Color(v * 3 / 4, v * 7 / 8, v) : Color(v, v * 7 / 8, v * 2 / 3));
    this->add_(px, py, c);
  }
}

// Stars: pastel stars twinkling in and out smoothly.
void EffectRenderer::stars_(float dt) {
  auto respawn = [](Particle &p) {
    p.x = (float) (xorshift() % W);
    p.y = (float) (xorshift() % H);
    p.z = 0.8f + frand() * 2.5f;  // lifetime in seconds
    p.life = 0;
    p.hue = frand();
  };
  if (this->reset_) {
    for (auto &p : this->parts_) {
      respawn(p);
      p.life = frand() * p.z;
    }
  }
  this->clear_();
  for (int i = 0; i < 36; i++) {
    Particle &p = this->parts_[i];
    p.life += dt;
    if (p.life >= p.z) {
      respawn(p);
      continue;
    }
    const uint8_t phase = (uint8_t) (p.life / p.z * 128.0f);  // 0..128 = 0..pi
    const uint8_t v = sin8(phase - 64) > 128 ? (sin8(phase - 64) - 128) * 2 : 0;
    const Color c = hsv8((uint8_t) (p.hue * 255), 110, v);
    const int x = (int) p.x, y = (int) p.y;
    this->add_(x, y, c);
    // brighter stars get a soft cross glow
    if (v > 150) {
      const Color g = scale8(c, 70);
      this->add_(x - 1, y, g);
      this->add_(x + 1, y, g);
      this->add_(x, y - 1, g);
      this->add_(x, y + 1, g);
    }
  }
}

// Game of Life: cells coloured by age, dead cells fade out.
void EffectRenderer::life_(float dt) {
  auto seed = [this]() {
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        this->grid_[y][x] = (xorshift() % 100) < 33 ? 1 : 0;
    this->gen_ = 0;
    this->stuck_ = 0;
    memset(this->hist_, 0, sizeof(this->hist_));
  };
  if (this->reset_) {
    seed();
    this->step_acc_ = 0;
  }
  this->fade_(0.80f);
  this->step_acc_ += dt;
  if (this->step_acc_ >= 0.18f) {
    this->step_acc_ = 0;
    uint8_t next[H][W];
    int alive = 0;
    uint32_t hash = 2166136261u;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
            if (dx || dy)
              n += this->grid_[(y + dy + H) % H][(x + dx + W) % W] ? 1 : 0;
        const uint8_t cur = this->grid_[y][x];
        uint8_t nv = 0;
        if (cur && (n == 2 || n == 3))
          nv = cur < 255 ? cur + 1 : 255;
        else if (!cur && n == 3)
          nv = 1;
        next[y][x] = nv;
        alive += nv ? 1 : 0;
        hash = (hash ^ (nv ? 1 : 0)) * 16777619u;
      }
    }
    memcpy(this->grid_, next, sizeof(this->grid_));
    this->gen_++;
    bool repeat = false;
    for (auto h : this->hist_)
      if (h == hash)
        repeat = true;
    for (int i = 3; i > 0; i--)
      this->hist_[i] = this->hist_[i - 1];
    this->hist_[0] = hash;
    this->stuck_ = repeat ? this->stuck_ + 1 : 0;
    if (alive == 0 || this->stuck_ > 15 || this->gen_ > 800)
      seed();
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t age = this->grid_[y][x];
      if (!age)
        continue;
      // newborn cells flash white, then cool from cyan through blue to violet
      if (age == 1)
        this->set_(x, y, Color(220, 255, 255));
      else
        this->set_(x, y, hsv8(128 + std::min<int>(age * 3, 80), 230, 255 - std::min<int>(age, 60)));
    }
}

// Lines: a bar spinning through the centre, leaving a rainbow fan of afterglow.
void EffectRenderer::lines_(float t, float dt) {
  this->fade_(0.86f);
  // the speed of rotation breathes
  const float a = t * 1.6f + 0.6f * sinf(t * 0.7f);
  const float ca = cosf(a) * 22.0f, sa = sinf(a) * 22.0f;
  const Color c = hsv8((uint8_t) (t * 25), 220, 255);
  this->line_((int) lroundf(15.5f - ca), (int) lroundf(15.5f - sa), (int) lroundf(15.5f + ca),
              (int) lroundf(15.5f + sa), c);
  // a second, dimmer bar at right angles
  const Color c2 = hsv8((uint8_t) (t * 25 + 128), 220, 110);
  this->line_((int) lroundf(15.5f + sa), (int) lroundf(15.5f - ca), (int) lroundf(15.5f - sa),
              (int) lroundf(15.5f + ca), c2);
}

// Circles: rings rippling out from wandering centres, each in its own colour.
void EffectRenderer::circles_(float t, float dt) {
  constexpr int RINGS = 6;
  if (this->reset_) {
    for (int i = 0; i < RINGS; i++)
      this->parts_[i].life = -1;
    this->step_acc_ = 1;
  }
  this->fade_(0.78f);
  this->step_acc_ += dt;
  if (this->step_acc_ > 0.55f) {
    this->step_acc_ = 0;
    for (int i = 0; i < RINGS; i++) {
      Particle &p = this->parts_[i];
      if (p.life < 0) {
        p.x = 16 + ((sin8((int) (t * 23)) - 128) >> 4);
        p.y = 16 + ((sin8((int) (t * 17) + 64) - 128) >> 4);
        p.z = 0;  // radius
        p.hue = t * 40;
        p.life = 0;
        break;
      }
    }
  }
  for (int i = 0; i < RINGS; i++) {
    Particle &p = this->parts_[i];
    if (p.life < 0)
      continue;
    p.z += 11.0f * dt;
    if (p.z > 26) {
      p.life = -1;
      continue;
    }
    const uint8_t v = (uint8_t) (255 - p.z * 8);
    this->circle_((int) p.x, (int) p.y, (int) p.z, hsv8((uint8_t) p.hue, 200, v));
  }
}

// Aurora: shimmering green-to-violet curtains waving across the sky.
void EffectRenderer::aurora_(float t) {
  const int t1 = (int) (t * 30), t2 = (int) (t * 13), t3 = (int) (t * 47);
  for (int x = 0; x < W; x++) {
    // curtain centre line and thickness vary along x and over time
    const int centre = 12 + ((sin8(x * 3 + t1) + sin8(x * 2 - t2)) >> 5);  // ~12..27
    const int thick = 8 + (sin8(x * 5 + t3) >> 5);
    const int bright = 120 + (sin8(x * 11 - t1 + (sin8(t2) >> 2)) >> 1);
    const uint8_t hue = 85 + (sin8(x * 4 + t2) >> 3);  // green .. teal
    for (int y = 0; y < H; y++) {
      int d = y - centre;
      int v;
      if (d < 0) {
        v = bright - (-d) * 255 / thick;  // fades out upwards
      } else {
        v = bright - d * 255 / (thick / 2 + 1);  // sharper edge below
      }
      if (v <= 0) {
        // night sky with a faint violet glow at the top
        this->set_(x, y, Color(y < 8 ? (8 - y) * 2 : 0, 0, y < 8 ? (8 - y) * 3 : 0));
        continue;
      }
      // the top of the curtain turns violet
      const uint8_t h = d < -3 ? hue + std::min(-d * 6, 110) : hue;
      this->set_(x, y, hsv8(h, 220, std::min(v, 255)));
    }
  }
}

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
