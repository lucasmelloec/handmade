#pragma once

#include <X11/Xlib.h>
#include <alsa/asoundlib.h>

struct LinuxX11OffscreenBuffer {
  XImage image;
  void *memory;
  uint32_t memory_size;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bytes_per_pixel;
};

struct LinuxWindowDimension {
  uint32_t width;
  uint32_t height;
};

struct LinuxSoundOutput {
  uint32_t samples_per_second;
  uint32_t samples_per_write;
};
