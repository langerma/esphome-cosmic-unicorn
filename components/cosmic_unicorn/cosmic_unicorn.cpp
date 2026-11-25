#ifdef USE_RP2

#include "cosmic_unicorn.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cosmic_unicorn {

static const char *const TAG = "cosmic_unicorn";

// release time of the tone envelope; sound is stopped after this
static const uint16_t TONE_RELEASE_MS = 30;

void CosmicUnicornDisplay::setup() {
  if (!this->unicorn_.init()) {
    ESP_LOGE(TAG, "No free PIO state machine / DMA channel for the LED matrix");
    this->mark_failed();
    return;
  }
  this->unicorn_.set_brightness(this->brightness_);
  this->unicorn_.set_volume(this->volume_);
  this->unicorn_.clear();
  this->ready_ = true;
}

void CosmicUnicornDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Cosmic Unicorn:\n"
                "  Size: %dx%d\n"
                "  Display PIO: pio%d\n"
                "  Brightness: %.2f\n"
                "  Volume: %.2f",
                WIDTH, HEIGHT, this->unicorn_.bitstream_pio_index(), this->brightness_, this->volume_);
  LOG_UPDATE_INTERVAL(this);
}

void CosmicUnicornDisplay::update() {
  if (!this->ready_)
    return;
  this->do_update_();
  this->flush_();
}

void CosmicUnicornDisplay::loop() {
  if (!this->tone_active_)
    return;
  const uint32_t now = millis();
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
  if (!this->ready_)
    return;
  this->unicorn_.set_brightness(this->brightness_);
  // brightness is baked into the bitstream per pixel, so re-push the frame
  this->dirty_ = true;
  this->flush_();
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
