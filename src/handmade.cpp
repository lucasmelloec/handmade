#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <unistd.h>

struct OffscreenBuffer {
  XImage image;
  void *memory;
  int memory_size;
  int width;
  int height;
  int pitch;
  int bytes_per_pixel;
};

static bool running;
static OffscreenBuffer global_backbuffer;

struct WindowDimension {
  int width;
  int height;
};

WindowDimension get_window_dimension(Display *display, Window window) {
  XWindowAttributes window_attrs;
  XGetWindowAttributes(display, window, &window_attrs);
  return WindowDimension{window_attrs.width, window_attrs.height};
}

static void render_weird_gradient(OffscreenBuffer buffer, int blue_offset,
                                  int green_offset) {
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

static void resize_bitmap(Display *display, int screen, OffscreenBuffer &buffer,
                          int width, int height) {
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

  XInitImage(&buffer.image);
}

static void display_buffer_in_window(Display *display, Window window, GC gc,
                                     OffscreenBuffer buffer, int window_width,
                                     int window_height) {
  XPutImage(display, window, gc, &buffer.image, 0, 0, 0, 0, window_width,
            window_height);
}

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display) {
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 0, 0, 800, 600, 1,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window,
                 ExposureMask | StructureNotifyMask | KeyPressMask |
                     KeyReleaseMask);

    {
      Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      XSetWMProtocols(display, window, &wm_delete, 1);
    }

    XMapWindow(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);

    resize_bitmap(display, screen, global_backbuffer, 800, 600);

    libevdev *controller_evdev = NULL;
    int controller_found = -1;

    {
      const char *evdev_path = "/dev/input/by-id";
      const char *joystick_suffix = "event-joystick";
      char joystick_filename[128];
      DIR *dir = opendir(evdev_path);
      if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
          const char *filename = entry->d_name;
          size_t filename_len = strlen(filename);
          size_t suffix_len = strlen(joystick_suffix);

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

        int controller_fd = open(joystick_filename, O_RDONLY | O_NONBLOCK);
        controller_found =
            libevdev_new_from_fd(controller_fd, &controller_evdev);
      }
    }

    running = true;
    int xoffset = 0;
    int yoffset = 0;
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
          WindowDimension dimension = get_window_dimension(display, window);
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
          bool just_released = event.xkey.type == KeyRelease;
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
            bool square =
                (input_event.code == BTN_WEST && input_event.value == 1);
            bool circle =
                (input_event.code == BTN_EAST && input_event.value == 1);
            bool triangle =
                (input_event.code == BTN_NORTH && input_event.value == 1);
            bool cross =
                (input_event.code == BTN_SOUTH && input_event.value == 1);
            bool left_shoulder =
                (input_event.code == BTN_TL && input_event.value == 1);
            bool right_shoulder =
                (input_event.code == BTN_TR && input_event.value == 1);
            bool start =
                (input_event.code == BTN_START && input_event.value == 1);
            bool select =
                (input_event.code == BTN_SELECT && input_event.value == 1);
          } else if (input_event.type == EV_ABS) {
            switch (input_event.code) {
            case ABS_X: {
              int8_t stick_x = input_event.value - 128;
            } break;
            case ABS_Y: {
              int8_t stick_y = input_event.value - 128;
            } break;
            }
          }
        }
      }

      render_weird_gradient(global_backbuffer, xoffset, yoffset);

      WindowDimension dimension = get_window_dimension(display, window);
      display_buffer_in_window(display, window, gc, global_backbuffer,
                               dimension.width, dimension.height);

      ++xoffset;
      yoffset += 2;
    }
  } else {
    // TODO: log
  }
  return 0;
}
