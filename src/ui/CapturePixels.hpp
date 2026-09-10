#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

// Capture surfaces can be float/packed BGRA from a GPU backend and can have
// padded rows. Convert once to explicit RGBA bytes before any encoder sees them.
inline bool capture_rgba(SDL_Surface* surface, bool double_vertical,
                         std::vector<uint8_t>& pixels, int& width, int& height) {
    if (!surface || surface->w <= 0 || surface->h <= 0) return false;
    const int repeat = double_vertical ? 2 : 1;
    if (surface->w > std::numeric_limits<int>::max() / 4 ||
        surface->h > std::numeric_limits<int>::max() / repeat) return false;
    const size_t row = size_t{static_cast<unsigned>(surface->w)} * 4;
    const int out_height = surface->h * repeat;
    if (row > std::numeric_limits<size_t>::max() / out_height) return false;
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (!converted) return false;
    try { pixels.resize(row * out_height); }
    catch (const std::bad_alloc&) { SDL_DestroySurface(converted); return false; }
    for (int y = 0; y < out_height; ++y)
        std::memcpy(pixels.data() + size_t{static_cast<unsigned>(y)} * row,
            static_cast<const uint8_t*>(converted->pixels) +
                size_t{static_cast<unsigned>(y / repeat)} * converted->pitch, row);
    width = surface->w;
    height = out_height;
    SDL_DestroySurface(converted);
    return true;
}

// A portable 24-bit BMP payload for clipboard MIME image/bmp. Header fields
// are little endian; scanlines are bottom-up with byte (not pixel) padding.
inline bool capture_bmp(SDL_Surface* surface, bool double_vertical,
                        std::vector<uint8_t>& bmp) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!capture_rgba(surface, double_vertical, rgba, w, h)) return false;
    const size_t stride = (size_t{static_cast<unsigned>(w)} * 3 + 3) & ~size_t{3};
    if (stride > (std::numeric_limits<uint32_t>::max() - 54) / h) return false;
    try { bmp.assign(54 + stride * h, 0); }
    catch (const std::bad_alloc&) { return false; }
    auto word = [&](size_t at, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) bmp[at + i] = value >> (i * 8);
    };
    bmp[0] = 'B'; bmp[1] = 'M';
    word(2, static_cast<uint32_t>(bmp.size()), 4); word(10, 54, 4); word(14, 40, 4);
    word(18, w, 4); word(22, h, 4); word(26, 1, 2); word(28, 24, 2);
    word(34, static_cast<uint32_t>(stride * h), 4); word(38, 2835, 4); word(42, 2835, 4);
    for (int y = 0; y < h; ++y) {
        auto* dst = bmp.data() + 54 + size_t{static_cast<unsigned>(h - y - 1)} * stride;
        const auto* src = rgba.data() + size_t{static_cast<unsigned>(y)} * w * 4;
        for (int x = 0; x < w; ++x) {
            dst[x * 3] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4];
        }
    }
    return true;
}
