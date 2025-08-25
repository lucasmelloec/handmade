#include "linux_handmade.h"
#include "handmade.h"

#include "handmade.cpp"

#include <X11/Xutil.h>
#include <algorithm>
#include <alsa/asoundlib.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <x86intrin.h>

static constexpr uint32_t CHANNELS = 2;

static bool running;
static LinuxX11OffscreenBuffer global_backbuffer;
static snd_pcm_t *pcm_handle;

static void linux_alsa_init(uint32_t samples_per_second,
                            uint32_t samples_per_write) {
  const char *PCM_DEVICE = "default";

  snd_pcm_hw_params_t *hw_params;

  if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK,
                   SND_PCM_NONBLOCK) >= 0) {
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);

    snd_pcm_hw_params_set_access(pcm_handle, hw_params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, CHANNELS);
    snd_pcm_hw_params_set_rate(pcm_handle, hw_params, samples_per_second, 0);
    snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, samples_per_write);
    snd_pcm_hw_params_set_period_time(pcm_handle, hw_params,
                                      samples_per_write / 2, 0);

    if (snd_pcm_hw_params(pcm_handle, hw_params) >= 0) {
    }
  }
}

static LinuxWindowDimension
linux_x11_get_window_dimension(Display *const display, const Window window) {
  XWindowAttributes window_attrs;
  if (XGetWindowAttributes(display, window, &window_attrs) != 0) {
    return LinuxWindowDimension{(uint32_t)window_attrs.width,
                                (uint32_t)window_attrs.height};
  } else {
    // TODO: Log error
    return LinuxWindowDimension{};
  }
}

static void linux_x11_resize_bitmap(LinuxX11OffscreenBuffer &buffer,
                                    const uint32_t width,
                                    const uint32_t height) {
  // TODO: maybe only destroy after successfully creating another one, and if
  // failed, destroy first
  if (buffer.memory) {
    munmap(buffer.memory, buffer.memory_size);
  }

  buffer.width = width;
  buffer.height = height;
  buffer.bytes_per_pixel = 4;

  buffer.memory_size = width * height * buffer.bytes_per_pixel;
  buffer.memory = mmap(NULL, buffer.memory_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  buffer.pitch = width * buffer.bytes_per_pixel;

  buffer.image.width = (int)width;
  buffer.image.height = (int)height;
  buffer.image.xoffset = 0;
  buffer.image.format = ZPixmap;
  buffer.image.data = (char *)buffer.memory;
  buffer.image.byte_order = LSBFirst;
  buffer.image.bitmap_unit = 32;
  buffer.image.bitmap_bit_order = LSBFirst;
  buffer.image.bitmap_pad = 32;
  buffer.image.depth = 24;
  buffer.image.bytes_per_line = (int)(width * buffer.bytes_per_pixel);
  buffer.image.bits_per_pixel = 32;
  buffer.image.red_mask = 0x00FF0000;
  buffer.image.green_mask = 0x0000FF00;
  buffer.image.blue_mask = 0x000000FF;

  if (XInitImage(&buffer.image) == 0) {
    // TODO: Log error and maybe do something about it
  }
}

static void linux_x11_display_buffer_in_window(Display *const display,
                                               const Window window, const GC gc,
                                               LinuxX11OffscreenBuffer buffer,
                                               const uint32_t window_width,
                                               const uint32_t window_height) {
  XPutImage(display, window, gc, &buffer.image, 0, 0, 0, 0, window_width,
            window_height);
}

static void linux_process_keyboard_message(GameButtonState *new_state,
                                           bool is_down) {
  ASSERT(new_state->ended_down != is_down);
  new_state->ended_down = is_down;
  ++new_state->half_transition_count;
}

static void linux_process_evdev_digital_button(input_event input_event,
                                               GameButtonState *old_state,
                                               int button_code,
                                               GameButtonState *new_state) {
  new_state->ended_down =
      input_event.code == button_code && input_event.value == 1;
  new_state->half_transition_count =
      (old_state->ended_down != new_state->ended_down) ? 1 : 0;
}

static float linux_process_evdev_stick_value(int32_t value,
                                             const int32_t deadzone) {
  float result = 0.0f;
  if (value < 128 - deadzone || value > 128 + deadzone) {
    result = (float)(value - 128);
    if (result < 0) {
      result /= 128.0f;
    } else {
      result /= 127.0f;
    }
  }

  return result;
}

static uint32_t linux_alsa_get_samples_to_write(uint32_t sample_count) {
  snd_pcm_sframes_t delay = 0;
  snd_pcm_sframes_t available = (snd_pcm_sframes_t)sample_count;
  uint32_t result;

  if (pcm_handle) {
    snd_pcm_avail_delay(pcm_handle, &available, &delay);
  }
  result = sample_count - (uint32_t)delay;
  return std::min(result, (uint32_t)available);
}

static void linux_alsa_fill_sound_buffer(int16_t *samples,
                                         uint32_t sample_count) {
  if (pcm_handle) {
    snd_pcm_sframes_t result =
        snd_pcm_writei(pcm_handle, samples, sample_count);
    if (result == -EAGAIN) {
      // Just skip this frame
    } else if (result < 0) {
      snd_pcm_prepare(pcm_handle);
    }
  }
}

static DEBUGReadFileResult
DEBUG_platform_read_entire_file(const char *filename) {
  DEBUGReadFileResult result = {};
  int fd = open(filename, O_RDONLY);
  if (fd > 0) {
    struct stat file_status;
    if (fstat(fd, &file_status) == 0) {
      result.content_size = SAFE_TRUNCATE_U64((uint64_t)file_status.st_size);
      result.content =
          mmap(NULL, result.content_size * sizeof(uint32_t),
               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (result.content) {
        if (read(fd, result.content, result.content_size) ==
            (ssize_t)result.content_size) {
          // File read successfully
        } else {
          DEBUG_platform_free_file_memory(result);
          // TODO: Log
        }
      } else {
        // TODO: Log
      }
    } else {
      // TODO: Log
    }

    close(fd);
  } else {
    // TODO: Log
  }

  return result;
}

static bool DEBUG_platform_write_entire_file(const char *filename,
                                             uint32_t size, void *content) {
  bool result = false;

  int fd =
      open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd > 0) {
    if (write(fd, content, size) == (ssize_t)size) {
      result = true;
    } else {
      // TODO: Log
    }
    close(fd);
  } else {
    // TODO: Log
  }

  return result;
}

static void
DEBUG_platform_free_file_memory(DEBUGReadFileResult &read_file_result) {
  munmap(read_file_result.content, read_file_result.content_size);
  read_file_result.content = NULL;
}

void linux_x11_process_pending_messages(
    Display *const display, const Window window, const GC gc,
    GameControllerInput *keyboard_controller) {
  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    switch (event.type) {
    case ConfigureNotify: {
      linux_x11_resize_bitmap(global_backbuffer,
                              (uint32_t)event.xconfigure.width,
                              (uint32_t)event.xconfigure.height);
    } break;
    case Expose: {
      const LinuxWindowDimension dimension =
          linux_x11_get_window_dimension(display, window);
      linux_x11_display_buffer_in_window(display, window, gc, global_backbuffer,
                                         dimension.width, dimension.height);
    } break;
    case ClientMessage: {
      running = false;
    } break;
    case DestroyNotify: {
      running = false;
    } break;
    case KeyPress:
    case KeyRelease: {
      const bool just_released = event.xkey.type == KeyRelease;
      bool is_down = event.xkey.type == KeyPress;
      if (event.xkey.type == KeyRelease && XPending(display)) {
        XEvent next_event;
        XPeekEvent(display, &next_event);
        if (next_event.xkey.type == KeyPress &&
            next_event.xkey.time == event.xkey.time &&
            next_event.xkey.keycode == event.xkey.keycode) {
          XNextEvent(display, &next_event);
          is_down = true;
        }
      }
      if (is_down != just_released) {
        switch (XLookupKeysym(&event.xkey, 0)) {
        case 'w': {
          linux_process_keyboard_message(&keyboard_controller->move_up,
                                         is_down);
        } break;
        case 'a': {
          linux_process_keyboard_message(&keyboard_controller->move_left,
                                         is_down);
        } break;
        case 's': {
          linux_process_keyboard_message(&keyboard_controller->move_down,
                                         is_down);
        } break;
        case 'd': {
          linux_process_keyboard_message(&keyboard_controller->move_right,
                                         is_down);
        } break;
        case 'q': {
          linux_process_keyboard_message(&keyboard_controller->left_shoulder,
                                         is_down);
        } break;
        case 'e': {
          linux_process_keyboard_message(&keyboard_controller->right_shoulder,
                                         is_down);
        } break;
        case XK_Up: {
          linux_process_keyboard_message(&keyboard_controller->action_up,
                                         is_down);
        } break;
        case XK_Left: {
          linux_process_keyboard_message(&keyboard_controller->action_left,
                                         is_down);
        } break;
        case XK_Down: {
          linux_process_keyboard_message(&keyboard_controller->action_down,
                                         is_down);
        } break;
        case XK_Right: {
          linux_process_keyboard_message(&keyboard_controller->action_right,
                                         is_down);
        } break;
        case XK_Escape: {
        } break;
        case XK_space: {
        } break;
        }
      }
    } break;
    default:
      break;
    }
  }
}

static timespec linux_get_wall_clock() {
  timespec result;
  clock_gettime(CLOCK_MONOTONIC_RAW, &result);
  return result;
}

static float linux_get_seconds_elapsed(timespec start, timespec end) {
  float result =
      (float)((end.tv_sec - start.tv_sec)) +
      ((float)(end.tv_nsec - start.tv_nsec) / (1000.0f * 1000.0f * 1000.0f));
  return result;
}

int main() {
  Display *const display = XOpenDisplay(NULL);
  if (display) {
    const int screen = DefaultScreen(display);
    const Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 0, 0, 800, 600, 1,
        BlackPixel(display, screen), WhitePixel(display, screen));
    if (XSelectInput(display, window,
                     ExposureMask | StructureNotifyMask | KeyPressMask |
                         KeyReleaseMask) == 0) {
      // TODO: Log error
      // TODO: Handle error nicely
      return 1;
    }

    {
      Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      if (XSetWMProtocols(display, window, &wm_delete, 1) == 0) {
        // TODO: Log error
      }
    }

    // TODO: Reliably query this on Linux
    constexpr uint32_t monitor_refresh_hz = 60;
    constexpr uint32_t game_update_hz = monitor_refresh_hz / 2;
    constexpr float target_seconds_per_frame = 1.0f / (float)game_update_hz;

    if (XMapWindow(display, window) == 0) {
      // TODO: Log error
      // TODO: Handle error nicely
      return 1;
    }
    const GC gc = XCreateGC(display, window, 0, NULL);

    linux_x11_resize_bitmap(global_backbuffer, 800, 600);

    libevdev *controller_evdev = NULL;
    int controller_found = -1;

    {
      const char *evdev_path = "/dev/input/by-id";
      const char *joystick_suffix = "event-joystick";
      char joystick_filename[273];
      DIR *const dir = opendir(evdev_path);
      if (dir) {
        const struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
          const char *filename = entry->d_name;
          const size_t filename_len = strlen(filename);
          const size_t suffix_len = strlen(joystick_suffix);

          if (filename_len >= suffix_len &&
              strcmp(filename + filename_len - suffix_len, joystick_suffix) ==
                  0) {
            snprintf(joystick_filename, sizeof(joystick_filename), "%s/%s",
                     evdev_path, filename);
            // TODO: Log if the controller was found
            fprintf(stdout, "Controller found at %s\n", joystick_filename);
            break;
          }
        }
        closedir(dir);

        const int controller_fd =
            open(joystick_filename, O_RDONLY | O_NONBLOCK);
        controller_found =
            libevdev_new_from_fd(controller_fd, &controller_evdev);
      }
    }

    LinuxSoundOutput sound_output{
        48000,
        48000 / 15,
    };

    linux_alsa_init((uint32_t)sound_output.samples_per_second,
                    (uint32_t)sound_output.samples_per_write);

    // TODO: Pool with the bitmap image allocation
    int16_t *samples = static_cast<int16_t *>(
        mmap(NULL, sound_output.samples_per_write * CHANNELS * sizeof(int16_t),
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    GameMemory game_memory{
        false, MEGABYTES(64), NULL, GIGABYTES(1), NULL,
    };

#if HANDMADE_INTERNAL
    void *address = (void *)TERABYTES(1);
#else
    void *address = 0;
#endif

    uint64_t total_size =
        game_memory.permanent_storage_size + game_memory.transient_storage_size;
    game_memory.permanent_storage =
        mmap(address, (size_t)total_size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    game_memory.transient_storage = (uint8_t *)game_memory.permanent_storage +
                                    game_memory.permanent_storage_size;

    if (samples && game_memory.permanent_storage &&
        game_memory.transient_storage) {

      linux_alsa_fill_sound_buffer(samples, sound_output.samples_per_write);

      GameInput input[2] = {};
      GameInput *new_input = &input[0];
      GameInput *old_input = &input[1];

      bool dpad_x_active = false;
      bool dpad_y_active = false;

      running = true;

      timespec last_counter = linux_get_wall_clock();
      uint64_t last_cycle_count = __rdtsc();

      while (running) {
        GameControllerInput *keyboard_controller = get_controller(new_input, 0);
        *keyboard_controller = {};
        keyboard_controller->is_connected = true;
        for (uint32_t controller_index = 0;
             controller_index < new_input->controllers.size();
             ++controller_index) {
          GameControllerInput *const new_controller =
              get_controller(new_input, controller_index);
          const GameControllerInput *const old_controller =
              get_controller(old_input, controller_index);

          for (uint32_t button_index = 0;
               button_index < new_controller->buttons.size(); ++button_index) {
            new_controller->buttons[button_index].ended_down =
                old_controller->buttons[button_index].ended_down;
            new_controller->stick_average_x = old_controller->stick_average_x;
            new_controller->stick_average_y = old_controller->stick_average_y;
          }
        }

        linux_x11_process_pending_messages(display, window, gc,
                                           keyboard_controller);

        if (controller_found >= 0) {
          constexpr uint32_t controller_index = 1; // 0 is the keyboard
          input_event input_event;
          GameControllerInput *old_controller =
              get_controller(old_input, controller_index);
          GameControllerInput *new_controller =
              get_controller(new_input, controller_index);
          new_controller->is_analog = true;
          new_controller->is_connected = true;
          // TODO: Maybe use (rc == 1 || rc == 0 || rc == -EAGAIN) and check
          // if(rc == o) before using the event
          while (libevdev_next_event(controller_evdev,
                                     LIBEVDEV_READ_FLAG_NORMAL,
                                     &input_event) == 0) {
            if (input_event.type == EV_KEY) {
              linux_process_evdev_digital_button(
                  input_event, &old_controller->action_left, BTN_WEST,
                  &new_controller->action_left);
              linux_process_evdev_digital_button(
                  input_event, &old_controller->action_right, BTN_EAST,
                  &new_controller->action_right);
              linux_process_evdev_digital_button(
                  input_event, &old_controller->action_up, BTN_NORTH,
                  &new_controller->action_up);
              linux_process_evdev_digital_button(
                  input_event, &old_controller->action_down, BTN_SOUTH,
                  &new_controller->action_down);
              linux_process_evdev_digital_button(
                  input_event, &old_controller->left_shoulder, BTN_TL,
                  &new_controller->left_shoulder);
              linux_process_evdev_digital_button(
                  input_event, &old_controller->right_shoulder, BTN_TR,
                  &new_controller->right_shoulder);
            } else if (input_event.type == EV_ABS) {
              constexpr int32_t DEADZONE = 3;
              switch (input_event.code) {
              case ABS_X: {
                if (!dpad_x_active) {
                  new_controller->stick_average_x =
                      linux_process_evdev_stick_value(input_event.value,
                                                      DEADZONE);
                }
              } break;
              case ABS_HAT0X: {
                if (input_event.value == 0) {
                  dpad_x_active = false;
                } else {
                  dpad_x_active = true;
                }
                new_controller->stick_average_x = (float)input_event.value;
              } break;
              case ABS_Y: {
                if (!dpad_y_active) {
                  new_controller->stick_average_y =
                      linux_process_evdev_stick_value(input_event.value,
                                                      DEADZONE);
                }
              } break;
              case ABS_HAT0Y: {
                if (input_event.value == 0) {
                  dpad_y_active = false;
                } else {
                  dpad_y_active = true;
                }
                new_controller->stick_average_y = (float)input_event.value;
              } break;
              }
            }
          }
          constexpr float THRESHOLD = 0.5;
          struct input_event fake_move_event = {
              {}, 0, 0, (new_controller->stick_average_x < -THRESHOLD) ? 1 : 0};
          linux_process_evdev_digital_button(fake_move_event,
                                             &old_controller->move_left, 0,
                                             &new_controller->move_left);
          fake_move_event.value =
              (new_controller->stick_average_x > THRESHOLD) ? 1 : 0;
          linux_process_evdev_digital_button(fake_move_event,
                                             &old_controller->move_right, 0,
                                             &new_controller->move_right);
          fake_move_event.value =
              (new_controller->stick_average_y < -THRESHOLD) ? 1 : 0;
          linux_process_evdev_digital_button(fake_move_event,
                                             &old_controller->move_up, 0,
                                             &new_controller->move_up);
          fake_move_event.value =
              (new_controller->stick_average_y > THRESHOLD) ? 1 : 0;
          linux_process_evdev_digital_button(fake_move_event,
                                             &old_controller->move_down, 0,
                                             &new_controller->move_down);
        }

        const GameOffscreenBuffer buffer{
            global_backbuffer.memory, global_backbuffer.width,
            global_backbuffer.height, global_backbuffer.pitch};
        const GameSoundOutputBuffer sound_buffer{
            sound_output.samples_per_second,
            linux_alsa_get_samples_to_write(sound_output.samples_per_write),
            samples,
        };
        game_update_and_render(new_input, buffer, sound_buffer, game_memory);

        linux_alsa_fill_sound_buffer(sound_buffer.samples,
                                     sound_buffer.sample_count);

        const LinuxWindowDimension dimension =
            linux_x11_get_window_dimension(display, window);
        linux_x11_display_buffer_in_window(display, window, gc,
                                           global_backbuffer, dimension.width,
                                           dimension.height);

        timespec work_counter = linux_get_wall_clock();

        float work_seconds_elapsed =
            linux_get_seconds_elapsed(last_counter, work_counter);

        float seconds_elapsed_for_frame = work_seconds_elapsed;
        if (seconds_elapsed_for_frame < target_seconds_per_frame) {
          while (seconds_elapsed_for_frame < target_seconds_per_frame) {
            uint32_t sleep_us = (uint32_t)(1000.0f * 1000.0f *
                                           (target_seconds_per_frame -
                                            seconds_elapsed_for_frame));
            usleep(sleep_us);
            seconds_elapsed_for_frame =
                linux_get_seconds_elapsed(last_counter, linux_get_wall_clock());
          }
        } else {
          // TODO: Log missed frame rate
        }

        std::swap(old_input, new_input);

        uint64_t end_cycle_count = __rdtsc();
        [[maybe_unused]] float cycles_elapsed =
            (float)(end_cycle_count - last_cycle_count) / (1000.0f * 1000.0f);

        timespec end_counter = linux_get_wall_clock();
        last_counter = end_counter;

        last_cycle_count = end_cycle_count;
      }
    } else {
      // TODO: log
    }
  } else {
    // TODO: log
  }

  return 0;
}
