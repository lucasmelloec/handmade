#include "handmade.h"
#include <cstdio>

static void game_output_sound(const GameSoundOutputBuffer &sound_buffer,
                              const int frequency) {
  static float t_sine;
  const int16_t amplitude = 3000;
  int wave_period = sound_buffer.samples_per_second / frequency;

  int16_t *sample_out = sound_buffer.samples;

  for (auto i = 0; i < sound_buffer.sample_count; ++i) {
    float sine_value = sinf(t_sine);
    int16_t sample_value = (int16_t)(sine_value * amplitude);
    *sample_out++ = sample_value;
    *sample_out++ = sample_value;

    t_sine += 2.0f * M_PI * 1.0f / (float)wave_period;
  }
}

static void render_weird_gradient(const GameOffscreenBuffer &buffer,
                                  const int blue_offset,
                                  const int green_offset) {
  uint8_t *row = (uint8_t *)buffer.memory;

  for (auto y = 0; y < buffer.height; ++y) {
    uint32_t *pixel = (uint32_t *)row;
    for (auto x = 0; x < buffer.width; ++x) {
      auto blue = (x + blue_offset);
      auto green = (y + green_offset);

      *pixel++ = ((green << 8) | blue);
    }

    row += buffer.pitch;
  }
}

void game_update_and_render(const GameInput *input,
                            const GameOffscreenBuffer &buffer,
                            const GameSoundOutputBuffer &sound_buffer) {
  static int green_offset, blue_offset = 0;
  static int frequency = 256;

  const GameControllerInput &input0 = input->controllers[0];
  if (input0.is_analog) {
    blue_offset += (int)4.0f * (input0.end_x);
    frequency = 256 + (int)(128.0f * (input0.end_y));
  } else {
  }

  if (input0.down.ended_down) {
    green_offset += 1;
  }

  game_output_sound(sound_buffer, frequency);
  render_weird_gradient(buffer, blue_offset, green_offset);
}
