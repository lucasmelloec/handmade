#include "handmade.h"

static void game_output_sound(const GameSoundOutputBuffer &sound_buffer) {
  static float t_sine;
  const int16_t amplitude = 3000;
  int frequency = 256;
  int wave_period = sound_buffer.samples_per_second / frequency;

  int16_t *sample_out = sound_buffer.samples;

  for (auto i = 0; i < sound_buffer.sample_count; ++i) {
    float sine_value = sinf(t_sine);
    int16_t sample_value = (int16_t)(sine_value * amplitude);
    *sample_out++ = sample_value;
    *sample_out++ = sample_value;

    t_sine += 2.0f*M_PI*1.0f/(float)wave_period;
  }
}

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

void game_update_and_render(const GameOffscreenBuffer &buffer, const GameSoundOutputBuffer & sound_buffer) {
  game_output_sound(sound_buffer);
  render_weird_gradient(buffer, 0, 0);
}
