#pragma once

#include <cstdint>

/*
 * NOTE: Code made available by the game layer to the platform layer
*/
struct GameOffscreenBuffer {
  void *memory;
  int width;
  int height;
  int pitch;
};

void game_update_and_render(const GameOffscreenBuffer & buffer);
