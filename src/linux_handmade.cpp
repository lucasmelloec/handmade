#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <alsa/asoundlib.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <libevdev/libevdev.h>
#include <math.h>
#include <sys/mman.h>
#include <x86intrin.h>

struct OffscreenBuffer {
  XImage image;
  void *memory;
  int memory_size;
  int width;
  int height;
  int pitch;
  int bytes_per_pixel;
};

struct WindowDimension {
  const int width;
  const int height;
};

struct SoundBuffer {
  void *memory;
  snd_pcm_t *pcm_handle;
  snd_pcm_uframes_t sound_frame_count;
};

struct SoundOutput {
  const int samples_per_second;
  const int frequency;
  const int16_t amplitude;
  uint32_t running_sample_index = 0;
};

static bool running;
static OffscreenBuffer global_backbuffer;
static SoundBuffer global_sound_buffer;

static void init_alsa(uint32_t samples_per_second) {
  const char *PCM_DEVICE = "default";
  const uint32_t CHANNELS = 2;

  snd_pcm_hw_params_t *hw_params;

  if (snd_pcm_open(&global_sound_buffer.pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(global_sound_buffer.pcm_handle, hw_params);

    snd_pcm_hw_params_set_access(global_sound_buffer.pcm_handle, hw_params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(global_sound_buffer.pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(global_sound_buffer.pcm_handle, hw_params, CHANNELS);
    snd_pcm_hw_params_set_rate_near(global_sound_buffer.pcm_handle, hw_params, &samples_per_second,
                                    0);

    if (snd_pcm_hw_params(global_sound_buffer.pcm_handle, hw_params) >= 0) {
      snd_pcm_hw_params_get_period_size(hw_params, &global_sound_buffer.sound_frame_count, 0);
      global_sound_buffer.memory =
          mmap(NULL, global_sound_buffer.sound_frame_count * CHANNELS * sizeof(int16_t),
               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
  }
}

WindowDimension get_window_dimension(Display *const display,
                                     const Window window) {
  XWindowAttributes window_attrs;
  if (XGetWindowAttributes(display, window, &window_attrs) != 0) {
    return WindowDimension{window_attrs.width, window_attrs.height};
  } else {
    // TODO: Log error
    return WindowDimension{};
  }
}

static void render_weird_gradient(const OffscreenBuffer buffer,
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

static void resize_bitmap(Display *const display, const int screen,
                          OffscreenBuffer &buffer, const int width,
                          const int height) {
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

  buffer.image.width = width;
  buffer.image.height = height;
  buffer.image.xoffset = 0;
  buffer.image.format = ZPixmap;
  buffer.image.data = (char *)buffer.memory;
  buffer.image.byte_order = LSBFirst;
  buffer.image.bitmap_unit = 32;
  buffer.image.bitmap_bit_order = LSBFirst;
  buffer.image.bitmap_pad = 32;
  buffer.image.depth = 24;
  buffer.image.bytes_per_line = width * buffer.bytes_per_pixel;
  buffer.image.bits_per_pixel = 32;
  buffer.image.red_mask = 0x00FF0000;
  buffer.image.green_mask = 0x0000FF00;
  buffer.image.blue_mask = 0x000000FF;

  if (XInitImage(&buffer.image) == 0) {
    // TODO: Log error and maybe do something about it
  }
}

static void display_buffer_in_window(Display *const display,
                                     const Window window, const GC gc,
                                     OffscreenBuffer buffer,
                                     const int window_width,
                                     const int window_height) {
  XPutImage(display, window, gc, &buffer.image, 0, 0, 0, 0, window_width,
            window_height);
}

static void fill_sound_buffer(const SoundBuffer sound_buffer, SoundOutput &sound_output) {
  int16_t *sample_out = (int16_t *)sound_buffer.memory;

  for (snd_pcm_uframes_t i = 0; i < sound_buffer.sound_frame_count; ++i) {
    float t =
        (float)(sound_output.running_sample_index * sound_buffer.sound_frame_count + i) /
        (float)sound_output.samples_per_second;
    float sine_value = sinf(2.0f * M_PI * sound_output.frequency * t);

    int16_t sample_value = (int16_t)(sine_value * sound_output.amplitude);
    *sample_out++ = sample_value;
    *sample_out++ = sample_value;
  }
  ++sound_output.running_sample_index;
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

    if (XMapWindow(display, window) == 0) {
      // TODO: Log error
      // TODO: Handle error nicely
      return 1;
    }
    const GC gc = XCreateGC(display, window, 0, NULL);

    resize_bitmap(display, screen, global_backbuffer, 800, 600);

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

    int xoffset = 0;
    int yoffset = 0;

    SoundOutput sound_output{
        48000,
        261,
        6000,
    };

    init_alsa(sound_output.samples_per_second);

    running = true;

    timespec last_counter;
    clock_gettime(CLOCK_MONOTONIC_RAW, &last_counter);
    int64_t last_cycle_count = __rdtsc();
    while (running) {
      while (XPending(display)) {
        XEvent event;
        XNextEvent(display, &event);
        switch (event.type) {
        case ConfigureNotify: {
          resize_bitmap(display, screen, global_backbuffer,
                        event.xconfigure.width, event.xconfigure.height);
        } break;
        case Expose: {
          const WindowDimension dimension =
              get_window_dimension(display, window);
          display_buffer_in_window(display, window, gc, global_backbuffer,
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
              fprintf(stdout, "W\n");
            } break;
            case 'a': {
              fprintf(stdout, "A\n");
            } break;
            case 's': {
              fprintf(stdout, "S\n");
            } break;
            case 'd': {
              fprintf(stdout, "D\n");
            } break;
            case 'q': {
              fprintf(stdout, "Q\n");
            } break;
            case 'e': {
              fprintf(stdout, "E\n");
            } break;
            case XK_Escape: {
              fprintf(stdout, "Escape\n");
            } break;
            case XK_space: {
              fprintf(stdout, "Space\n");
            } break;
            }
          }
        } break;
        default:
          break;
        }
      }

      if (controller_found >= 0) {
        input_event input_event;
        // TODO: Maybe use (rc == 1 || rc == 0 || rc == -EAGAIN) and check
        // if(rc == o) before using the event
        while (libevdev_next_event(controller_evdev, LIBEVDEV_READ_FLAG_NORMAL,
                                   &input_event) == 0) {
          if (input_event.type == EV_KEY) {
            const bool square =
                (input_event.code == BTN_WEST && input_event.value == 1);
            const bool circle =
                (input_event.code == BTN_EAST && input_event.value == 1);
            const bool triangle =
                (input_event.code == BTN_NORTH && input_event.value == 1);
            const bool cross =
                (input_event.code == BTN_SOUTH && input_event.value == 1);
            const bool left_shoulder =
                (input_event.code == BTN_TL && input_event.value == 1);
            const bool right_shoulder =
                (input_event.code == BTN_TR && input_event.value == 1);
            const bool start =
                (input_event.code == BTN_START && input_event.value == 1);
            const bool select =
                (input_event.code == BTN_SELECT && input_event.value == 1);
          } else if (input_event.type == EV_ABS) {
            switch (input_event.code) {
            case ABS_X: {
              const int8_t stick_x = input_event.value - 128;
            } break;
            case ABS_Y: {
              const int8_t stick_y = input_event.value - 128;
            } break;
            }
          }
        }
      }

      render_weird_gradient(global_backbuffer, xoffset, yoffset);

      if (global_sound_buffer.pcm_handle && global_sound_buffer.memory) {
        fill_sound_buffer(global_sound_buffer, sound_output);
        if (snd_pcm_writei(global_sound_buffer.pcm_handle, global_sound_buffer.memory,
                           global_sound_buffer.sound_frame_count) == -EPIPE) {
          snd_pcm_prepare(global_sound_buffer.pcm_handle);
        }
      }

      const WindowDimension dimension = get_window_dimension(display, window);
      display_buffer_in_window(display, window, gc, global_backbuffer,
                               dimension.width, dimension.height);

      timespec end_counter;
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_counter);

      int64_t end_cycle_count = __rdtsc();

      float cycles_elapsed =
          (float)(end_cycle_count - last_cycle_count) / (1000.0f * 1000.0f);

      float counter_elapsed =
          ((end_counter.tv_sec - last_counter.tv_sec) * 1000) +
          ((float)(end_counter.tv_nsec - last_counter.tv_nsec) /
           (1000.0f * 1000.0f));
      float fps = 1000.0f / (float)counter_elapsed;
      fprintf(stdout, "%.02f ms, %.02f FPS, %.02f Mcycles\n", counter_elapsed,
              fps, cycles_elapsed);

      last_counter = end_counter;
      last_cycle_count = end_cycle_count;

      usleep(1000);
    }
  } else {
    // TODO: log
  }

  return 0;
}
