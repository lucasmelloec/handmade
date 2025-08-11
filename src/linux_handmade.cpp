#include "handmade.h"

#include "handmade.cpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <alloca.h>
#include <alsa/asoundlib.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <libevdev/libevdev.h>
#include <sys/mman.h>
#include <x86intrin.h>

const uint32_t CHANNELS = 2;

struct LinuxOffscreenBuffer {
  XImage image;
  void *memory;
  int memory_size;
  int width;
  int height;
  int pitch;
  int bytes_per_pixel;
};

struct LinuxWindowDimension {
  const int width;
  const int height;
};

struct LinuxSoundBuffer {
  snd_pcm_t *pcm_handle;
  snd_pcm_uframes_t sound_frame_count;
};

struct LinuxSoundOutput {
  const int samples_per_second;
};

static bool running;
static LinuxOffscreenBuffer global_backbuffer;
static LinuxSoundBuffer global_sound_buffer;

static void linux_init_alsa(uint32_t samples_per_second) {
  const char *PCM_DEVICE = "default";

  snd_pcm_hw_params_t *hw_params;

  if (snd_pcm_open(&global_sound_buffer.pcm_handle, PCM_DEVICE,
                   SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(global_sound_buffer.pcm_handle, hw_params);

    snd_pcm_hw_params_set_access(global_sound_buffer.pcm_handle, hw_params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(global_sound_buffer.pcm_handle, hw_params,
                                 SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(global_sound_buffer.pcm_handle, hw_params,
                                   CHANNELS);
    snd_pcm_hw_params_set_rate_near(global_sound_buffer.pcm_handle, hw_params,
                                    &samples_per_second, 0);

    if (snd_pcm_hw_params(global_sound_buffer.pcm_handle, hw_params) >= 0) {
      snd_pcm_hw_params_get_period_size(
          hw_params, &global_sound_buffer.sound_frame_count, 0);
    }
  }
}

static LinuxWindowDimension linux_get_window_dimension(Display *const display,
                                                       const Window window) {
  XWindowAttributes window_attrs;
  if (XGetWindowAttributes(display, window, &window_attrs) != 0) {
    return LinuxWindowDimension{window_attrs.width, window_attrs.height};
  } else {
    // TODO: Log error
    return LinuxWindowDimension{};
  }
}

static void linux_resize_bitmap(Display *const display, const int screen,
                                LinuxOffscreenBuffer &buffer, const int width,
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

static void linux_display_buffer_in_window(Display *const display,
                                           const Window window, const GC gc,
                                           LinuxOffscreenBuffer buffer,
                                           const int window_width,
                                           const int window_height) {
  XPutImage(display, window, gc, &buffer.image, 0, 0, 0, 0, window_width,
            window_height);
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

    linux_resize_bitmap(display, screen, global_backbuffer, 800, 600);

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
    };

    linux_init_alsa(sound_output.samples_per_second);

    // TODO: Pool with the bitmap image allocation
    int16_t *samples = static_cast<int16_t *>(
        mmap(NULL,
             global_sound_buffer.sound_frame_count * CHANNELS * sizeof(int16_t),
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

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
          linux_resize_bitmap(display, screen, global_backbuffer,
                              event.xconfigure.width, event.xconfigure.height);
        } break;
        case Expose: {
          const LinuxWindowDimension dimension =
              linux_get_window_dimension(display, window);
          linux_display_buffer_in_window(display, window, gc, global_backbuffer,
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

      const GameSoundOutputBuffer sound_buffer{
          sound_output.samples_per_second,
          static_cast<int>(global_sound_buffer.sound_frame_count), samples};

      const GameOffscreenBuffer buffer{
          global_backbuffer.memory, global_backbuffer.width,
          global_backbuffer.height, global_backbuffer.pitch};

      game_update_and_render(buffer, sound_buffer);

      if (global_sound_buffer.pcm_handle) {
        auto frame_count = snd_pcm_avail_update(global_sound_buffer.pcm_handle);
        frame_count =
            std::min(frame_count,
                     (snd_pcm_sframes_t)global_sound_buffer.sound_frame_count);
        if (frame_count > 0) {
          if (snd_pcm_writei(global_sound_buffer.pcm_handle,
                             static_cast<void *>(sound_buffer.samples),
                             frame_count) == -EPIPE) {
            snd_pcm_prepare(global_sound_buffer.pcm_handle);
          }
        }
      }

      const LinuxWindowDimension dimension =
          linux_get_window_dimension(display, window);
      linux_display_buffer_in_window(display, window, gc, global_backbuffer,
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
