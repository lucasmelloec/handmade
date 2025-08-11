#pragma once

#include <cstdint>
#include <cmath>

/*
 * NOTE: Code made available by the game layer to the platform layer
*/

struct GameOffscreenBuffer {
  void *memory;
  int width;
  int height;
  int pitch;
};

struct GameSoundOutputBuffer {
  int samples_per_second;
  int sample_count;
  int16_t *samples;
};

void game_update_and_render(const GameOffscreenBuffer & buffer, const GameSoundOutputBuffer & sound_buffer);
