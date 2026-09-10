#pragma once
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace gs2::postprocess {
// File-based ImageIO/preloaded-image shortcuts can premultiply then
// unpremultiply alpha, losing hidden RGB and rounding translucent channels.
// Postprocessing uses those exact channels for bezel interpolation and
// reflections. Decode the original bytes through SDL_image's common stream
// backend on every platform.
inline SDL_Surface *load_image_rgba(const char *path) {
  size_t size = 0;
  void *bytes = SDL_LoadFile(path, &size);
  if (!bytes)
    return nullptr;
  auto *stream = SDL_IOFromConstMem(bytes, size);
  if (!stream) {
    SDL_free(bytes);
    return nullptr;
  }
  // Formats without a signature (for example TGA) require the same extension
  // hint that IMG_Load supplies, even though we bypass its file shortcuts.
  const char *extension = SDL_strrchr(path, '.');
  auto *surface =
      IMG_LoadTyped_IO(stream, true, extension ? extension + 1 : nullptr);
  SDL_free(bytes);
  return surface;
}
} // namespace gs2::postprocess
