#pragma once

#include <cmath>
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

struct GameSoundOutputBuffer {
  int samples_per_second;
  int sample_count;
  int16_t *samples;
};

struct GameButtonState{
  int half_transition_count;
  bool ended_down;
};

struct GameControllerInput {
  bool is_analog;

  float start_x;
  float start_y;

  float min_x;
  float min_y;

  float max_x;
  float max_y;

  float end_x;
  float end_y;

  union {
    GameButtonState buttons[6];
    struct {
      GameButtonState up;
      GameButtonState down;
      GameButtonState left;
      GameButtonState right;
      GameButtonState left_shoulder;
      GameButtonState right_shoulder;
    };
  };
};

struct GameInput {
  GameControllerInput controllers[1];
};

void game_update_and_render(const GameInput &input,
                            const GameOffscreenBuffer &buffer,
                            const GameSoundOutputBuffer &sound_buffer);
