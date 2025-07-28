#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

static bool running;
static XImage bitmap_image;
static void *bitmap_memory;
static int bitmap_memory_size;
static int bytes_per_pixel = 4;
static int bitmap_width;
static int bitmap_height;

static void render_weird_gradient(int xoffset, int yoffset) {
  int width = bitmap_width;
  int height = bitmap_height;

  int pitch = width * bytes_per_pixel;
  uint8_t *row = (uint8_t *)bitmap_memory;

  for (int y = 0; y < height; ++y) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < width; ++x) {
      auto blue = (x + xoffset);
      auto green = (y + yoffset);

      *pixel++ = ((green << 8) | blue);
    }

    row += pitch;
  }
}

static void resize_bitmap(Display *display, int screen, int width, int height) {
  // TODO: maybe only destroy after successfully creating another one, and if
  // failed, destroy first
  if (bitmap_memory) {
    munmap(bitmap_memory, bitmap_memory_size);
  }

  bitmap_width = width;
  bitmap_height = height;

  bitmap_memory_size = width * height * bytes_per_pixel;
  bitmap_memory = mmap(NULL, bitmap_memory_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  bitmap_image.width = width;
  bitmap_image.height = height;
  bitmap_image.xoffset = 0;
  bitmap_image.format = ZPixmap;
  bitmap_image.data = (char *)bitmap_memory;
  bitmap_image.byte_order = LSBFirst;
  bitmap_image.bitmap_unit = 32;
  bitmap_image.bitmap_bit_order = LSBFirst;
  bitmap_image.bitmap_pad = 32;
  bitmap_image.depth = 24;
  bitmap_image.bytes_per_line = width * bytes_per_pixel;
  bitmap_image.bits_per_pixel = 32;
  bitmap_image.red_mask = 0x00FF0000;
  bitmap_image.green_mask = 0x0000FF00;
  bitmap_image.blue_mask = 0x000000FF;

  XInitImage(&bitmap_image);
}

static void update_window(Display *display, Window window, GC gc,
                          int window_width, int window_height) {
  XPutImage(display, window, gc, &bitmap_image, 0, 0, 0, 0, window_width,
            window_height);
}

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display) {
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 0, 0, 800, 600, 1,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);

    {
      Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
      XSetWMProtocols(display, window, &wm_delete, 1);
    }

    XMapWindow(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);

    resize_bitmap(display, screen, 800, 600);
    running = true;

    int xoffset = 0;
    int yoffset = 0;
    while (running) {
      while (XPending(display)) {
        XEvent event;
        XNextEvent(display, &event);
        switch (event.type) {
        case ConfigureNotify: {
          resize_bitmap(display, screen, event.xconfigure.width,
                        event.xconfigure.height);
        } break;
        case Expose: {
          XWindowAttributes window_attrs;
          XGetWindowAttributes(display, window, &window_attrs);
          update_window(display, window, gc, window_attrs.width,
                        window_attrs.height);
        } break;
        case ClientMessage: {
          running = false;
        } break;
        case DestroyNotify: {
          running = false;
        } break;
        default:
          break;
        }
      }

      render_weird_gradient(xoffset, yoffset);
      {
        XWindowAttributes window_attrs;
        XGetWindowAttributes(display, window, &window_attrs);
        update_window(display, window, gc, window_attrs.width,
                      window_attrs.height);
      }

      ++xoffset;
    }
  } else {
    // TODO: log
  }

  return 0;
}
