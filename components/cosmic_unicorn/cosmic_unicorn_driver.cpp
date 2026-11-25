// Cosmic Unicorn low-level driver.
//
// Adapted from Pimoroni's pimoroni-pico library (libraries/cosmic_unicorn),
// MIT licensed, Copyright (c) Pimoroni Ltd. See cosmic_unicorn_driver.hpp for
// the list of changes.
#ifdef USE_RP2

#include <math.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "cosmic_unicorn.pio.h"
#include "audio_i2s.pio.h"

#include "cosmic_unicorn_driver.hpp"

// pixel data is stored as a stream of bits delivered in the
// order the PIO needs to manage the shift registers, row
// selects, delays, and latching/blanking
//
// the framebuffer data is structured like this:
//
// for each row:
//   for each bcd frame:
//            0: 00111111                           // row pixel count (minus one)
//            1: xxxxrrrr                           // row select bits
//      2  - 65: xxxxxbgr, xxxxxbgr, xxxxxbgr, ...  // pixel data
//      66 - 67: xxxxxxxx, xxxxxxxx,                // dummy bytes to dword align
//      68 - 71: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
//
//  .. and back to the start

static uint32_t dma_channel;
static uint32_t dma_ctrl_channel;
static uint32_t audio_dma_channel;

namespace pimoroni {

CosmicUnicorn *CosmicUnicorn::unicorn = nullptr;
PIO CosmicUnicorn::bitstream_pio = pio0;
uint CosmicUnicorn::bitstream_sm = 0;
uint CosmicUnicorn::bitstream_sm_offset = 0;
PIO CosmicUnicorn::audio_pio = pio0;
uint CosmicUnicorn::audio_sm = 0;
uint CosmicUnicorn::audio_sm_offset = 0;

void __isr CosmicUnicorn::dma_complete() {
  if (unicorn != nullptr && dma_channel_get_irq0_status(audio_dma_channel)) {
    unicorn->next_audio_sequence();
  }
}

bool CosmicUnicorn::claim_pio_(const pio_program_t *program, PIO &pio, uint &sm, uint &offset) {
  PIO candidates[] = {pio0, pio1};
  for (PIO candidate : candidates) {
    if (!pio_can_add_program(candidate, program))
      continue;
    int claimed = pio_claim_unused_sm(candidate, false);
    if (claimed < 0)
      continue;
    pio = candidate;
    sm = (uint) claimed;
    offset = pio_add_program(candidate, program);
    return true;
  }
  return false;
}

uint16_t CosmicUnicorn::light() {
  adc_select_input(2);
  return adc_read();
}

bool CosmicUnicorn::init() {
  if (unicorn != nullptr)
    return unicorn == this;

  // initialise the bcd timing values and row selects in the bitstream
  for (uint8_t row = 0; row < ROW_COUNT; row++) {
    for (uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
      uint8_t *p = &bitstream[row * ROW_BYTES + (BCD_FRAME_BYTES * frame)];

      p[0] = 64 - 1;  // row pixel count
      p[1] = row;     // row select

      uint32_t bcd_ticks = (1 << frame);
      p[68] = (bcd_ticks & 0xff) >> 0;
      p[69] = (bcd_ticks & 0xff00) >> 8;
      p[70] = (bcd_ticks & 0xff0000) >> 16;
      p[71] = (bcd_ticks & 0xff000000) >> 24;
    }
  }

  // light sensor adc
  if (!(adc_hw->cs & ADC_CS_EN_BITS))
    adc_init();
  adc_gpio_init(LIGHT_SENSOR);

  gpio_init(COLUMN_CLOCK);
  gpio_set_dir(COLUMN_CLOCK, GPIO_OUT);
  gpio_put(COLUMN_CLOCK, false);
  gpio_init(COLUMN_DATA);
  gpio_set_dir(COLUMN_DATA, GPIO_OUT);
  gpio_put(COLUMN_DATA, false);
  gpio_init(COLUMN_LATCH);
  gpio_set_dir(COLUMN_LATCH, GPIO_OUT);
  gpio_put(COLUMN_LATCH, false);
  gpio_init(COLUMN_BLANK);
  gpio_set_dir(COLUMN_BLANK, GPIO_OUT);
  gpio_put(COLUMN_BLANK, true);

  // row select to a non-visible row to avoid flashes during setup
  const uint8_t row_pins[] = {ROW_BIT_0, ROW_BIT_1, ROW_BIT_2, ROW_BIT_3};
  for (uint8_t pin : row_pins) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, true);
  }

  sleep_ms(100);

  // configure full output current in register 2 of the 12 driver chips
  uint16_t reg1 = 0b1111111111001110;
  for (int j = 0; j < 11; j++) {
    for (int i = 0; i < 16; i++) {
      gpio_put(COLUMN_DATA, (reg1 & (1U << (15 - i))) != 0);
      sleep_us(10);
      gpio_put(COLUMN_CLOCK, true);
      sleep_us(10);
      gpio_put(COLUMN_CLOCK, false);
    }
  }
  for (int i = 0; i < 16; i++) {
    gpio_put(COLUMN_DATA, (reg1 & (1U << (15 - i))) != 0);
    sleep_us(10);
    gpio_put(COLUMN_CLOCK, true);
    sleep_us(10);
    gpio_put(COLUMN_CLOCK, false);
    if (i == 4)
      gpio_put(COLUMN_LATCH, true);
  }
  gpio_put(COLUMN_LATCH, false);

  // reapply the blank as the above seems to cause a slight glow
  gpio_put(COLUMN_BLANK, false);
  sleep_us(10);
  gpio_put(COLUMN_BLANK, true);

  gpio_init(MUTE);
  gpio_set_dir(MUTE, GPIO_OUT);
  gpio_put(MUTE, true);

  // buttons (also usable from ESPHome gpio binary_sensors)
  const uint8_t switches[] = {SWITCH_A,         SWITCH_B,           SWITCH_C,
                              SWITCH_D,         SWITCH_SLEEP,       SWITCH_BRIGHTNESS_UP,
                              SWITCH_BRIGHTNESS_DOWN, SWITCH_VOLUME_UP, SWITCH_VOLUME_DOWN};
  for (uint8_t pin : switches) {
    gpio_init(pin);
    gpio_pull_up(pin);
  }

  // ---- display bitstream PIO + DMA ----
  if (!claim_pio_(&cosmic_unicorn_program, bitstream_pio, bitstream_sm, bitstream_sm_offset))
    return false;

  const uint8_t pio_pins[] = {COLUMN_CLOCK, COLUMN_DATA, COLUMN_LATCH, COLUMN_BLANK,
                              ROW_BIT_0,    ROW_BIT_1,   ROW_BIT_2,    ROW_BIT_3};
  for (uint8_t pin : pio_pins)
    pio_gpio_init(bitstream_pio, pin);

  // blank and row pins high first, then set directions (avoids a momentary flash)
  const uint pins_to_set = 1 << COLUMN_BLANK | 0b1111 << ROW_BIT_0;
  pio_sm_set_pins_with_mask(bitstream_pio, bitstream_sm, pins_to_set, pins_to_set);
  pio_sm_set_consecutive_pindirs(bitstream_pio, bitstream_sm, COLUMN_CLOCK, 8, true);

  pio_sm_config c = cosmic_unicorn_program_get_default_config(bitstream_sm_offset);
  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_out_pins(&c, ROW_BIT_0, 4);
  sm_config_set_set_pins(&c, COLUMN_DATA, 3);
  sm_config_set_sideset_pins(&c, COLUMN_CLOCK);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  int ch = dma_claim_unused_channel(false);
  int ctrl = dma_claim_unused_channel(false);
  if (ch < 0 || ctrl < 0)
    return false;
  dma_channel = ch;
  dma_ctrl_channel = ctrl;

  // control channel re-arms the data channel with the bitstream start address
  dma_channel_config ctrl_config = dma_channel_get_default_config(dma_ctrl_channel);
  channel_config_set_transfer_data_size(&ctrl_config, DMA_SIZE_32);
  channel_config_set_read_increment(&ctrl_config, false);
  channel_config_set_write_increment(&ctrl_config, false);
  channel_config_set_chain_to(&ctrl_config, dma_channel);
  dma_channel_configure(dma_ctrl_channel, &ctrl_config, &dma_hw->ch[dma_channel].read_addr, &bitstream_addr, 1,
                        false);

  dma_channel_config config = dma_channel_get_default_config(dma_channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
  channel_config_set_bswap(&config, false);
  channel_config_set_dreq(&config, pio_get_dreq(bitstream_pio, bitstream_sm, true));
  channel_config_set_chain_to(&config, dma_ctrl_channel);
  dma_channel_configure(dma_channel, &config, &bitstream_pio->txf[bitstream_sm], NULL, BITSTREAM_LENGTH / 4, false);

  pio_sm_init(bitstream_pio, bitstream_sm, bitstream_sm_offset, &c);
  pio_sm_set_enabled(bitstream_pio, bitstream_sm, true);
  dma_start_channel_mask(1u << dma_ctrl_channel);

  // ---- audio I2S PIO + DMA (optional: display keeps working without it) ----
  unicorn = this;
  if (!claim_pio_(&audio_i2s_program, audio_pio, audio_sm, audio_sm_offset))
    return true;

  pio_gpio_init(audio_pio, I2S_DATA);
  pio_gpio_init(audio_pio, I2S_BCLK);
  pio_gpio_init(audio_pio, I2S_LRCLK);

  audio_i2s_program_init(audio_pio, audio_sm, audio_sm_offset, I2S_DATA, I2S_BCLK);
  uint32_t system_clock_frequency = clock_get_hz(clk_sys);
  uint32_t divider = system_clock_frequency * 4 / SYSTEM_FREQ;  // avoid arithmetic overflow
  pio_sm_set_clkdiv_int_frac(audio_pio, audio_sm, divider >> 8u, divider & 0xffu);

  int audio_ch = dma_claim_unused_channel(false);
  if (audio_ch < 0)
    return true;
  audio_dma_channel = audio_ch;
  dma_channel_config audio_config = dma_channel_get_default_config(audio_dma_channel);
  channel_config_set_transfer_data_size(&audio_config, DMA_SIZE_16);
  channel_config_set_dreq(&audio_config, pio_get_dreq(audio_pio, audio_sm, true));
  dma_channel_configure(audio_dma_channel, &audio_config, &audio_pio->txf[audio_sm], NULL, 0, false);

  dma_channel_set_irq0_enabled(audio_dma_channel, true);
  irq_add_shared_handler(DMA_IRQ_0, dma_complete, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  irq_set_enabled(DMA_IRQ_0, true);

  return true;
}

void CosmicUnicorn::clear() {
  for (uint8_t y = 0; y < HEIGHT; y++)
    for (uint8_t x = 0; x < WIDTH; x++)
      set_pixel(x, y, 0, 0, 0);
}

void CosmicUnicorn::dma_safe_abort(uint channel) {
  // Tear down the DMA channel.
  // Copied from: https://github.com/raspberrypi/pico-sdk/pull/744/commits/5e0e8004dd790f0155426e6689a66e08a83cd9fc
  uint32_t irq0_save = dma_hw->inte0 & (1u << channel);
  hw_clear_bits(&dma_hw->inte0, irq0_save);

  dma_hw->abort = 1u << channel;

  // poll BUSY rather than ABORT, ABORT can clear prematurely
  while (dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS)
    tight_loop_contents();

  dma_hw->ints0 = 1u << channel;
  hw_set_bits(&dma_hw->inte0, irq0_save);
}

void CosmicUnicorn::play_sample(uint8_t *data, uint32_t length) {
  stop_playing();
  if (unicorn == this) {
    pio_sm_set_enabled(audio_pio, audio_sm, true);
    dma_channel_transfer_from_buffer_now(audio_dma_channel, data, length / 2);
    play_mode = PLAYING_BUFFER;
  }
}

void CosmicUnicorn::play_synth() {
  if (play_mode != PLAYING_SYNTH)
    stop_playing();

  if (unicorn == this && play_mode == NOT_PLAYING) {
    current_buffer = 0;
    populate_next_synth();
    pio_sm_set_enabled(audio_pio, audio_sm, true);
    play_mode = PLAYING_SYNTH;
    next_audio_sequence();
  }
}

void CosmicUnicorn::next_audio_sequence() {
  // acknowledge the irq for our channel
  dma_hw->ints0 = 1u << audio_dma_channel;

  if (play_mode == PLAYING_SYNTH) {
    dma_channel_transfer_from_buffer_now(audio_dma_channel, tone_buffers[current_buffer], TONE_BUFFER_SIZE);
    current_buffer = (current_buffer + 1) % NUM_TONE_BUFFERS;
    populate_next_synth();
  } else {
    play_mode = NOT_PLAYING;
  }
}

void CosmicUnicorn::populate_next_synth() {
  int16_t *samples = tone_buffers[current_buffer];
  for (uint i = 0; i < TONE_BUFFER_SIZE; i++)
    samples[i] = synth.get_audio_frame();
}

void CosmicUnicorn::stop_playing() {
  if (unicorn == this) {
    pio_sm_set_enabled(audio_pio, audio_sm, false);

    // reset the I2S pins to avoid popping when audio is suddenly stopped
    const uint pins_to_clear = 1 << I2S_DATA | 1 << I2S_BCLK | 1 << I2S_LRCLK;
    pio_sm_set_pins_with_mask(audio_pio, audio_sm, 0, pins_to_clear);

    dma_safe_abort(audio_dma_channel);
    play_mode = NOT_PLAYING;
  }
}

AudioChannel &CosmicUnicorn::synth_channel(uint channel) { return synth.channels[channel % PicoSynth::CHANNEL_COUNT]; }

void CosmicUnicorn::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    return;

  x = (WIDTH - 1) - x;
  y = (HEIGHT - 1) - y;

  // map coordinates into display space
  if (y < 16) {
    // top half of the display is the right half of the framebuffer
    x += 32;
  } else {
    y -= 16;
  }

  r = (r * this->brightness) >> 8;
  g = (g * this->brightness) >> 8;
  b = (b * this->brightness) >> 8;

  uint16_t gamma_r = GAMMA_14BIT[r];
  uint16_t gamma_g = GAMMA_14BIT[g];
  uint16_t gamma_b = GAMMA_14BIT[b];

  // set the appropriate bits in the separate bcd frames
  for (uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
    uint8_t *p = &bitstream[y * ROW_BYTES + (BCD_FRAME_BYTES * frame) + 2 + x];

    uint8_t red_bit = gamma_r & 0b1;
    uint8_t green_bit = gamma_g & 0b1;
    uint8_t blue_bit = gamma_b & 0b1;

    *p = (blue_bit << 0) | (green_bit << 1) | (red_bit << 2);

    gamma_r >>= 1;
    gamma_g >>= 1;
    gamma_b >>= 1;
  }
}

void CosmicUnicorn::set_brightness(float value) {
  value = value < 0.0f ? 0.0f : value;
  value = value > 1.0f ? 1.0f : value;
  // max is 256 since it's applied with (channel * brightness) >> 8
  this->brightness = floor(value * 256.0f);
}

float CosmicUnicorn::get_brightness() { return this->brightness / 256.0f; }

void CosmicUnicorn::set_volume(float value) {
  value = value < 0.0f ? 0.0f : value;
  value = value > 1.0f ? 1.0f : value;
  this->volume = floor(value * 255.0f);
  this->synth.volume = this->volume * 255.0f;
}

float CosmicUnicorn::get_volume() { return this->volume / 255.0f; }

bool CosmicUnicorn::is_pressed(uint8_t button) { return !gpio_get(button); }

}  // namespace pimoroni

#endif  // USE_RP2
