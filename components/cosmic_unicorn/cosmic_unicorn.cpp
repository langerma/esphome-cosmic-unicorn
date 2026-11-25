#ifdef USE_RP2

#include "cosmic_unicorn.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cosmic_unicorn {

static const char *const TAG = "cosmic_unicorn";

// release time of the tone envelope; sound is stopped after this
static const uint16_t TONE_RELEASE_MS = 30;
// auto brightness: sample interval and smoothing (time constant ~ interval / alpha = 2s)
static const uint32_t AMBIENT_INTERVAL_MS = 200;
static const float AMBIENT_ALPHA = 0.1f;
// only re-push the frame when the factor moved by more than this
static const float AMBIENT_HYSTERESIS = 0.01f;

void CosmicUnicornDisplay::setup() {
  if (!this->unicorn_.init()) {
    ESP_LOGE(TAG, "No free PIO state machine / DMA channel for the LED matrix");
    this->mark_failed();
    return;
  }
  this->unicorn_.set_volume(this->volume_);
  this->ready_ = true;
  if (this->auto_enabled_) {
    this->update_ambient_();
    this->ambient_factor_ = this->target_factor_;
  }
  this->unicorn_.set_brightness(this->brightness_ * this->get_ambient_factor());
  this->unicorn_.clear();
}

void CosmicUnicornDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Cosmic Unicorn:\n"
                "  Size: %dx%d\n"
                "  Display PIO: pio%d\n"
                "  Brightness: %.2f\n"
                "  Volume: %.2f\n"
                "  Auto brightness: %s (dark %u, bright %u, factor %.2f-%.2f)",
                WIDTH, HEIGHT, this->unicorn_.bitstream_pio_index(), this->brightness_, this->volume_,
                YESNO(this->auto_enabled_), this->dark_level_, this->bright_level_, this->min_factor_,
                this->max_factor_);
  LOG_UPDATE_INTERVAL(this);
}

void CosmicUnicornDisplay::update() {
  if (!this->ready_)
    return;
  this->do_update_();
  this->flush_();
}

void CosmicUnicornDisplay::loop() {
  const uint32_t now = millis();
  if (this->auto_enabled_ && this->ready_ && now - this->last_ambient_ms_ >= AMBIENT_INTERVAL_MS) {
    this->last_ambient_ms_ = now;
    this->update_ambient_();
    const float target = this->target_factor_;
    const bool at_limit = target == this->min_factor_ || target == this->max_factor_;
    if (fabsf(target - this->ambient_factor_) > AMBIENT_HYSTERESIS || (at_limit && target != this->ambient_factor_)) {
      this->ambient_factor_ = target;
      this->apply_brightness_();
    }
  }

  if (!this->tone_active_)
    return;
  if (!this->tone_releasing_ && (int32_t) (now - this->tone_until_) >= 0) {
    this->unicorn_.synth_channel(0).trigger_release();
    this->tone_releasing_ = true;
    this->tone_until_ = now + TONE_RELEASE_MS + 10;
  } else if (this->tone_releasing_ && (int32_t) (now - this->tone_until_) >= 0) {
    this->stop_sound();
  }
}

void CosmicUnicornDisplay::fill(Color color) {
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      this->frame_[y][x][0] = color.r;
      this->frame_[y][x][1] = color.g;
      this->frame_[y][x][2] = color.b;
    }
  }
  this->dirty_ = true;
}

void CosmicUnicornDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    return;
  uint8_t *px = this->frame_[y][x];
  if (px[0] == color.r && px[1] == color.g && px[2] == color.b)
    return;
  px[0] = color.r;
  px[1] = color.g;
  px[2] = color.b;
  this->dirty_ = true;
}

void CosmicUnicornDisplay::flush_() {
  if (!this->dirty_)
    return;
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      const uint8_t *px = this->frame_[y][x];
      this->unicorn_.set_pixel(x, y, px[0], px[1], px[2]);
    }
  }
  this->dirty_ = false;
}

void CosmicUnicornDisplay::set_brightness(float brightness) {
  this->brightness_ = clamp(brightness, 0.0f, 1.0f);
  this->apply_brightness_();
}

void CosmicUnicornDisplay::apply_brightness_() {
  if (!this->ready_)
    return;
  this->unicorn_.set_brightness(this->brightness_ * this->get_ambient_factor());
  // brightness is baked into the bitstream per pixel, so re-push the frame
  this->dirty_ = true;
  this->flush_();
}

void CosmicUnicornDisplay::set_auto_brightness(bool enabled) {
  if (enabled == this->auto_enabled_)
    return;
  this->auto_enabled_ = enabled;
  if (!this->ready_)
    return;  // setup() takes the first reading once the ADC is initialised
  if (enabled) {
    this->smoothed_light_ = -1.0f;  // start from the current reading, not an old average
    this->update_ambient_();
    this->ambient_factor_ = this->target_factor_;
  }
  this->apply_brightness_();
}

void CosmicUnicornDisplay::update_ambient_() {
  float raw = this->unicorn_.light();
  if (this->smoothed_light_ < 0.0f) {
    this->smoothed_light_ = raw;
  } else {
    this->smoothed_light_ += AMBIENT_ALPHA * (raw - this->smoothed_light_);
  }

  // log mapping: the eye (and this sensor) span several decades of light
  float lo = logf(std::max<float>(this->dark_level_, 1.0f));
  float hi = logf(std::max<float>(this->bright_level_, 1.0f));
  float v = logf(std::max(this->smoothed_light_, 1.0f));
  float t = (hi == lo) ? 1.0f : clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
  this->target_factor_ = this->min_factor_ + (this->max_factor_ - this->min_factor_) * t;
}

void CosmicUnicornDisplay::set_volume(float volume) {
  this->volume_ = clamp(volume, 0.0f, 1.0f);
  if (this->ready_)
    this->unicorn_.set_volume(this->volume_);
}

uint16_t CosmicUnicornDisplay::get_light_level() {
  if (!this->ready_)
    return 0;
  return this->unicorn_.light();
}

void CosmicUnicornDisplay::play_tone(float frequency, uint32_t duration_ms, ToneWaveform waveform) {
  if (!this->ready_)
    return;
  auto &ch = this->unicorn_.synth_channel(0);
  switch (waveform) {
    case ToneWaveform::SINE:
      ch.waveforms = pimoroni::Waveform::SINE;
      break;
    case ToneWaveform::TRIANGLE:
      ch.waveforms = pimoroni::Waveform::TRIANGLE;
      break;
    case ToneWaveform::SAW:
      ch.waveforms = pimoroni::Waveform::SAW;
      break;
    case ToneWaveform::NOISE:
      ch.waveforms = pimoroni::Waveform::NOISE;
      break;
    case ToneWaveform::SQUARE:
    default:
      ch.waveforms = pimoroni::Waveform::SQUARE;
      break;
  }
  ch.frequency = (uint16_t) clamp(frequency, 20.0f, 10000.0f);
  ch.volume = UINT16_MAX;
  ch.attack_ms = 5;
  ch.decay_ms = 10;
  ch.sustain = UINT16_MAX;
  ch.release_ms = TONE_RELEASE_MS;

  this->unicorn_.play_synth();
  ch.trigger_attack();
  this->tone_active_ = true;
  this->tone_releasing_ = false;
  this->tone_until_ = millis() + duration_ms;
}

void CosmicUnicornDisplay::stop_sound() {
  if (!this->ready_)
    return;
  this->unicorn_.synth_channel(0).off();
  this->unicorn_.stop_playing();
  this->tone_active_ = false;
  this->tone_releasing_ = false;
}

}  // namespace esphome::cosmic_unicorn

#endif  // USE_RP2
