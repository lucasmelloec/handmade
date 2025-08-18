#include "handmade.h"

constexpr float PI_32 = 3.14159265359f;

static void game_output_sound(const GameSoundOutputBuffer &sound_buffer,
                              const int32_t frequency) {
  static float t_sine;
  const int16_t amplitude = 3000;
  int32_t wave_period = (int32_t)sound_buffer.samples_per_second / frequency;

  int16_t *sample_out = sound_buffer.samples;

  for (uint32_t i = 0; i < sound_buffer.sample_count; ++i) {
    float sine_value = sinf(t_sine);
    int16_t sample_value = (int16_t)(sine_value * amplitude);
    *sample_out++ = sample_value;
    *sample_out++ = sample_value;

    t_sine += 2.0f * PI_32 * 1.0f / (float)wave_period;
  }
}

static void render_weird_gradient(const GameOffscreenBuffer &buffer,
                                  const int blue_offset,
                                  const int green_offset) {
  uint8_t *row = (uint8_t *)buffer.memory;

  for (uint32_t y = 0; y < buffer.height; ++y) {
    uint32_t *pixel = (uint32_t *)row;
    for (uint32_t x = 0; x < buffer.width; ++x) {
      uint8_t blue = (uint8_t)((int)x + blue_offset);
      uint8_t green = (uint8_t)((int)y + green_offset);

      *pixel++ = (uint32_t)((green << 8) | blue);
    }

    row += buffer.pitch;
  }
}

void game_update_and_render(const GameInput *input,
                            const GameOffscreenBuffer &buffer,
                            const GameSoundOutputBuffer &sound_buffer,
                            GameMemory &memory) {
  ASSERT(sizeof(GameState) <= memory.permanent_storage_size);
  GameState *game_state = (GameState *)memory.permanent_storage;
  if (!memory.is_initialized) {
    const char *filename = __FILE__;
    DEBUGReadFileResult file = DEBUG_platform_read_entire_file(filename);
    if (file.content) {
      DEBUG_platform_write_entire_file("text.txt", file.content_size, file.content);
      DEBUG_platform_free_file_memory(file);
    }

    game_state->frequency = 256;

    // TODO: Maybe it's more appropriate to do this in the platform layer
    memory.is_initialized = true;
  }

  const GameControllerInput &input0 = input->controllers[0];
  if (input0.is_analog) {
    game_state->blue_offset += (int)(4.0f * input0.end_x);
    game_state->frequency = 256 + (int)(128.0f * input0.end_y);
  } else {
  }

  if (input0.down.ended_down) {
    game_state->green_offset += 1;
  }

  game_output_sound(sound_buffer, game_state->frequency);
  render_weird_gradient(buffer, game_state->blue_offset,
                        game_state->green_offset);
}
