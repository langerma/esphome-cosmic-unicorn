#pragma once

#include "esphome/core/defines.h"

#if defined(USE_RP2) && defined(USE_COSMIC_UNICORN_GIF)

#include <cstdint>
#include <string>

#include "esphome/components/display/display.h"
#include "esphome/components/http_request/http_request.h"

class AnimatedGIF;
struct gif_draw_tag;

namespace esphome::cosmic_unicorn {

/// Downloads an animated GIF over HTTP and plays it, scaled to fit 32x32
/// (aspect ratio kept, centred).
class GifPlayer {
 public:
  static constexpr int W = 32;
  static constexpr int H = 32;

  void set_http(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void set_max_size(size_t max_size) { this->max_size_ = max_size; }

  /// Download and start playing. Blocks for the download. duration_ms 0 = loop until stopped.
  bool play(const std::string &url, uint32_t duration_ms);
  void stop();
  bool is_playing() const { return this->playing_; }

  /// Draw the current frame; returns false if nothing is playing.
  bool render(display::Display &it);

 protected:
  static void draw_cb_(gif_draw_tag *d);
  void free_();

  http_request::HttpRequestComponent *http_{nullptr};
  size_t max_size_{65536};

  AnimatedGIF *gif_{nullptr};
  uint8_t *data_{nullptr};
  size_t size_{0};

  uint8_t canvas_[H][W][3]{};
  int src_w_{0}, src_h_{0};
  int scale_den_{1}, off_x_{0}, off_y_{0};

  bool playing_{false};
  uint32_t next_frame_ms_{0};
  uint32_t until_ms_{0};
};

}  // namespace esphome::cosmic_unicorn

#endif
