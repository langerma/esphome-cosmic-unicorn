#include "cosmic_unicorn_gif.h"

#if defined(USE_RP2) && defined(USE_COSMIC_UNICORN_GIF)

#include "AnimatedGIF.h"

#include <cstdlib>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cosmic_unicorn {

static const char *const TAG = "cosmic_unicorn.gif";

void GifPlayer::free_() {
  if (this->gif_ != nullptr) {
    this->gif_->close();
    delete this->gif_;
    this->gif_ = nullptr;
  }
  if (this->data_ != nullptr) {
    free(this->data_);
    this->data_ = nullptr;
  }
  this->size_ = 0;
}

void GifPlayer::stop() {
  this->playing_ = false;
  this->free_();
}

bool GifPlayer::play(const std::string &url, uint32_t duration_ms) {
  this->stop();
  if (this->http_ == nullptr) {
    ESP_LOGE(TAG, "No http_request component");
    return false;
  }

  ESP_LOGD(TAG, "Downloading %s", url.c_str());
  auto container = this->http_->get(url);
  if (container == nullptr || !http_request::is_success(container->status_code)) {
    ESP_LOGE(TAG, "Download failed (HTTP %d)", container ? container->status_code : -1);
    if (container)
      container->end();
    return false;
  }

  size_t expected = container->content_length;
  if (expected > this->max_size_) {
    ESP_LOGE(TAG, "GIF too large: %u bytes (max_size %u)", (unsigned) expected, (unsigned) this->max_size_);
    container->end();
    return false;
  }
  const size_t alloc = expected > 0 ? expected : this->max_size_;
  this->data_ = (uint8_t *) malloc(alloc);
  if (this->data_ == nullptr) {
    ESP_LOGE(TAG, "Out of memory for %u bytes", (unsigned) alloc);
    container->end();
    return false;
  }
  auto result = http_request::http_read_fully(container.get(), this->data_, alloc, 1024, this->http_->get_timeout());
  this->size_ = container->get_bytes_read();
  container->end();
  if (result.status != http_request::HttpReadStatus::OK || this->size_ == 0) {
    ESP_LOGE(TAG, "Download incomplete (%u bytes)", (unsigned) this->size_);
    this->free_();
    return false;
  }

  this->gif_ = new AnimatedGIF();
  this->gif_->begin(GIF_PALETTE_RGB565_LE);
  if (!this->gif_->open(this->data_, (int) this->size_, (GIF_DRAW_CALLBACK *) &GifPlayer::draw_cb_)) {
    ESP_LOGE(TAG, "Not a valid GIF (error %d)", this->gif_->getLastError());
    this->free_();
    return false;
  }

  this->src_w_ = this->gif_->getCanvasWidth();
  this->src_h_ = this->gif_->getCanvasHeight();
  // fit into 32x32, keep aspect ratio, centre
  this->scale_den_ = std::max(this->src_w_, this->src_h_);
  if (this->scale_den_ <= 0)
    this->scale_den_ = 1;
  this->off_x_ = (W - this->src_w_ * W / this->scale_den_) / 2;
  this->off_y_ = (H - this->src_h_ * H / this->scale_den_) / 2;
  memset(this->canvas_, 0, sizeof(this->canvas_));

  ESP_LOGI(TAG, "Playing %ux%u GIF (%u bytes)", this->src_w_, this->src_h_, (unsigned) this->size_);
  const uint32_t now = millis();
  this->next_frame_ms_ = now;
  this->until_ms_ = duration_ms > 0 ? now + duration_ms : 0;
  this->playing_ = true;
  return true;
}

void GifPlayer::draw_cb_(gif_draw_tag *raw) {
  GIFDRAW *d = (GIFDRAW *) raw;
  auto *self = static_cast<GifPlayer *>(d->pUser);
  const int den = self->scale_den_;
  const int gy = d->iY + d->y;
  const int y0 = self->off_y_ + gy * H / den;
  const int y1 = std::max(y0 + 1, self->off_y_ + (gy + 1) * H / den);
  const uint8_t *src = d->pPixels;
  const uint16_t *pal = d->pPalette;

  for (int x = 0; x < d->iWidth; x++) {
    const uint8_t idx = src[x];
    if (d->ucHasTransparency && idx == d->ucTransparent)
      continue;  // keep what the previous frame left there
    const uint16_t c = pal[idx];
    const uint8_t r = ((c >> 11) & 0x1f) << 3, g = ((c >> 5) & 0x3f) << 2, b = (c & 0x1f) << 3;
    const int gx = d->iX + x;
    const int x0 = self->off_x_ + gx * W / den;
    const int x1 = std::max(x0 + 1, self->off_x_ + (gx + 1) * W / den);
    for (int yy = y0; yy < y1 && yy < H; yy++) {
      for (int xx = x0; xx < x1 && xx < W; xx++) {
        if (xx < 0 || yy < 0)
          continue;
        self->canvas_[yy][xx][0] = r | (r >> 5);
        self->canvas_[yy][xx][1] = g | (g >> 6);
        self->canvas_[yy][xx][2] = b | (b >> 5);
      }
    }
  }
}

bool GifPlayer::render(display::Display &it) {
  if (!this->playing_)
    return false;
  const uint32_t now = millis();
  if (this->until_ms_ != 0 && (int32_t) (now - this->until_ms_) >= 0) {
    this->stop();
    return false;
  }
  if ((int32_t) (now - this->next_frame_ms_) >= 0) {
    int delay_ms = 0;
    int r = this->gif_->playFrame(false, &delay_ms, this);
    if (r < 0) {
      ESP_LOGW(TAG, "Decode error %d, stopping", this->gif_->getLastError());
      this->stop();
      return false;
    }
    if (r == 0)
      this->gif_->reset();  // last frame shown, loop
    this->next_frame_ms_ = now + std::max(delay_ms, 20);
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      it.draw_pixel_at(x, y, Color(this->canvas_[y][x][0], this->canvas_[y][x][1], this->canvas_[y][x][2]));
  return true;
}

}  // namespace esphome::cosmic_unicorn

#endif
