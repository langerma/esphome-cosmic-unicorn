// Cosmic Unicorn low-level driver.
//
// Adapted from Pimoroni's pimoroni-pico library (libraries/cosmic_unicorn),
// MIT licensed, Copyright (c) Pimoroni Ltd. Changes for ESPHome:
//  - no PicoGraphics dependency (pixels are pushed with set_pixel)
//  - PIO block is chosen at runtime (pio0 or pio1), so it coexists with the
//    Pico W's CYW43 Wi-Fi driver, which also uses a PIO block
//  - init() reports failure instead of panicking
#pragma once

#include "hardware/pio.h"
#include "pico_synth.hpp"

namespace pimoroni {

class CosmicUnicorn {
 public:
  static const int WIDTH = 32;
  static const int HEIGHT = 32;

  // pin assignments
  static const uint8_t COLUMN_CLOCK = 13;
  static const uint8_t COLUMN_DATA = 14;
  static const uint8_t COLUMN_LATCH = 15;
  static const uint8_t COLUMN_BLANK = 16;

  static const uint8_t ROW_BIT_0 = 17;
  static const uint8_t ROW_BIT_1 = 18;
  static const uint8_t ROW_BIT_2 = 19;
  static const uint8_t ROW_BIT_3 = 20;

  static const uint8_t LIGHT_SENSOR = 28;

  static const uint8_t MUTE = 22;

  static const uint8_t I2S_DATA = 9;
  static const uint8_t I2S_BCLK = 10;
  static const uint8_t I2S_LRCLK = 11;

  static const uint8_t SWITCH_A = 0;
  static const uint8_t SWITCH_B = 1;
  static const uint8_t SWITCH_C = 3;
  static const uint8_t SWITCH_D = 6;
  static const uint8_t SWITCH_SLEEP = 27;
  static const uint8_t SWITCH_VOLUME_UP = 7;
  static const uint8_t SWITCH_VOLUME_DOWN = 8;
  static const uint8_t SWITCH_BRIGHTNESS_UP = 21;
  static const uint8_t SWITCH_BRIGHTNESS_DOWN = 26;

 private:
  static const uint32_t ROW_COUNT = 16;
  static const uint32_t BCD_FRAME_COUNT = 14;
  static const uint32_t BCD_FRAME_BYTES = 72;
  static const uint32_t ROW_BYTES = BCD_FRAME_COUNT * BCD_FRAME_BYTES;
  static const uint32_t BITSTREAM_LENGTH = (ROW_COUNT * ROW_BYTES);
  static const uint SYSTEM_FREQ = 22050;

  static PIO bitstream_pio;
  static uint bitstream_sm;
  static uint bitstream_sm_offset;

  static PIO audio_pio;
  static uint audio_sm;
  static uint audio_sm_offset;

  uint16_t brightness = 256;
  uint16_t volume = 127;

  // must be aligned for 32bit dma transfer
  alignas(4) uint8_t bitstream[BITSTREAM_LENGTH] = {0};
  const uint32_t bitstream_addr = (uint32_t) bitstream;
  static CosmicUnicorn *unicorn;
  static void dma_complete();

  static const uint NUM_TONE_BUFFERS = 2;
  static const uint TONE_BUFFER_SIZE = 4;
  int16_t tone_buffers[NUM_TONE_BUFFERS][TONE_BUFFER_SIZE] = {0};
  uint current_buffer = 0;

  PicoSynth synth;

  enum PlayMode { PLAYING_BUFFER, PLAYING_SYNTH, NOT_PLAYING };
  volatile PlayMode play_mode = NOT_PLAYING;

 public:
  // Returns false if no PIO block / DMA channel was available.
  bool init();

  void clear();

  void set_brightness(float value);
  float get_brightness();

  void set_volume(float value);
  float get_volume();

  // x/y in display coordinates (0,0 = top left), r/g/b 0..255.
  // Global brightness and gamma are applied here.
  void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

  uint16_t light();

  bool is_pressed(uint8_t button);

  void play_sample(uint8_t *data, uint32_t length);
  void play_synth();
  void stop_playing();
  bool is_playing() { return play_mode != NOT_PLAYING; }
  AudioChannel &synth_channel(uint channel);
  bool synth_is_audio_playing() { return synth.is_audio_playing(); }

  int bitstream_pio_index() { return pio_get_index(bitstream_pio); }

 private:
  static bool claim_pio_(const pio_program_t *program, PIO &pio, uint &sm, uint &offset);
  void dma_safe_abort(uint channel);
  void next_audio_sequence();
  void populate_next_synth();
};

}  // namespace pimoroni
