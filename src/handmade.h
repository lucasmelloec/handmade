#pragma once

#include <array>
#include <cmath>
#include <cstdint>

/*
 * HANDMADE_SLOW: Whether to allow slow code to run
 * HANDMADE_INTERNAL: Wheter this is a public release or a developer release
 */

#if HANDMADE_SLOW
constexpr void ASSERT(bool expression) {
  if (!expression) {
    *(volatile int *)NULL = 0;
  }
}
#else
constexpr void ASSERT(bool expresion) {}
#endif

constexpr uint64_t KILOBYTES(const uint64_t value) { return value * 1024; }
constexpr uint64_t MEGABYTES(const uint64_t value) {
  return KILOBYTES(value) * 1024;
}
constexpr uint64_t GIGABYTES(const uint64_t value) {
  return MEGABYTES(value) * 1024;
}
constexpr uint64_t TERABYTES(const uint64_t value) {
  return GIGABYTES(value) * 1024;
}

static inline uint32_t SAFE_TRUNCATE_U64(uint64_t value) {
  ASSERT(value <= 0xffffffff);
  uint32_t result = (uint32_t)value;
  return result;
}

/*
 * NOTE: Code made available by the platform layer to the game layer
 */

#if HANDMADE_INTERNAL
struct DEBUGReadFileResult {
  uint32_t content_size;
  void *content;
};
static DEBUGReadFileResult
DEBUG_platform_read_entire_file(const char *filename);
static bool DEBUG_platform_write_entire_file(const char *filename,
                                             uint32_t size, void *content);
static void
DEBUG_platform_free_file_memory(DEBUGReadFileResult &read_file_result);
#endif

/*
 * NOTE: Code made available by the game layer to the platform layer
 */

struct GameOffscreenBuffer {
  void *memory;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
};

struct GameSoundOutputBuffer {
  uint32_t samples_per_second;
  uint32_t sample_count;
  int16_t *samples;
};

struct GameButtonState {
  int32_t half_transition_count;
  bool ended_down;
};

struct GameControllerInput {
  bool is_connected;
  bool is_analog;
  float stick_average_x;
  float stick_average_y;

  union {
    std::array<GameButtonState, 10> buttons;
    struct {
      GameButtonState move_up;
      GameButtonState move_down;
      GameButtonState move_left;
      GameButtonState move_right;
      GameButtonState action_up;
      GameButtonState action_down;
      GameButtonState action_left;
      GameButtonState action_right;
      GameButtonState left_shoulder;
      GameButtonState right_shoulder;
    };
  };
};

struct GameInput {
  std::array<GameControllerInput, 2> controllers;
};
static inline GameControllerInput *get_controller(GameInput *input,
                                                  const size_t controller_index) {
  ASSERT(controller_index < input->controllers.size());

  return &input->controllers[controller_index];
}

struct GameMemory {
  bool is_initialized;
  uint64_t permanent_storage_size;
  void *permanent_storage; // NOTE: This is REQUIRED to be initialized to zero
  uint64_t transient_storage_size;
  void *transient_storage; // NOTE: This is REQUIRED to be initialized to zero
};

void game_update_and_render(const GameInput &input,
                            const GameOffscreenBuffer &buffer,
                            const GameSoundOutputBuffer &sound_buffer);

// TODO: Don't know where to put it yet
struct GameState {
  int32_t green_offset;
  int32_t blue_offset;
  int32_t frequency;
};
