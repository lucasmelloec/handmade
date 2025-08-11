#pragma once

#include <X11/Xlib.h>
#include <alsa/asoundlib.h>

struct LinuxX11OffscreenBuffer {
  XImage image;
  void *memory;
  int32_t memory_size;
  int32_t width;
  int32_t height;
  int32_t pitch;
  int32_t bytes_per_pixel;
};

struct LinuxWindowDimension {
  int32_t width;
  int32_t height;
};

struct LinuxSoundOutput {
  int32_t samples_per_second;
  int32_t samples_per_write;
};
