#pragma once

#include <cstdint>

// The provider owns the pixels, valid until its next call. Bytes are R,G,B,A
// (SDL_PIXELFORMAT_RGBA32) with straight alpha; dimensions describe its canvas
// even while hidden. Generation changes whenever the visible pixels change.
struct GuestOverlayFrame {
    const uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    uint64_t generation = 0;
    bool visible = false;
};
