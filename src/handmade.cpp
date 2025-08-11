#include "handmade.h"

static void render_weird_gradient(const GameOffscreenBuffer &buffer,
                                  const int blue_offset,
                                  const int green_offset) {
  uint8_t *row = (uint8_t *)buffer.memory;

  for (int y = 0; y < buffer.height; ++y) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < buffer.width; ++x) {
      auto blue = (x + blue_offset);
      auto green = (y + green_offset);

      *pixel++ = ((green << 8) | blue);
    }

    row += buffer.pitch;
  }
}

void game_update_and_render(const GameOffscreenBuffer &buffer) {
  render_weird_gradient(buffer, 0, 0);
}
