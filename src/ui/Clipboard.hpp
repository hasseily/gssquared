#pragma once
#include <SDL3/SDL.h>

struct ClipboardImage {
    // SDL owns each encoded snapshot until its clipboard cleanup callback.
    void Clip(SDL_Surface* surface, bool double_vertical = true);
};
