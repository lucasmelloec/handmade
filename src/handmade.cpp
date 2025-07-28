#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>

static bool running;
static XImage *bitmap_image;
static void *bitmap_memory;
static int bitmap_width = 800;
static int bitmap_height = 600;

static void resize_bitmap(Display *display, int screen, int width, int height) {
  // TODO: maybe only destroy after successfully creating another one, and if
  // failed, destroy first
  if (bitmap_image) {
    XDestroyImage(bitmap_image);
  }

  bitmap_width = width;
  bitmap_height = height;
  bitmap_memory = malloc(width * height * 4);

  bitmap_image =
      XCreateImage(display, DefaultVisual(display, screen), 24, ZPixmap, 0,
                   (char *)bitmap_memory, width, height, 32, 0);
}

static void update_window(Display *display, Window window, GC gc) {
  XPutImage(display, window, gc, bitmap_image, 0, 0, 0, 0, bitmap_width,
            bitmap_height);
}

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display) {
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 0, 0, bitmap_width, bitmap_height,
        1, BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);
    XMapWindow(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);

    resize_bitmap(display, screen, bitmap_width, bitmap_height);
    running = true;
    XEvent event;

    while (running) {
      XNextEvent(display, &event);
      switch (event.type) {
      case ConfigureNotify: {
        resize_bitmap(display, screen, event.xconfigure.width,
                      event.xconfigure.height);
      } break;
      case Expose: {
        update_window(display, window, gc);
      } break;
      case ClientMessage:
      case DestroyNotify: {
        running = false;
      } break;
      default:
        break;
      }
    }
  } else {
    // TODO: log
  }

  return 0;
}
