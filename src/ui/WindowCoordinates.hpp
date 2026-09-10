#pragma once
#include <SDL3/SDL.h>
#include <algorithm>

// SDL's offscreen GPU renderer has no associated window. Convert its host UI
// input explicitly through the same drawable-pixel LETTERBOX as the UI target.
inline void window_event_to_design(SDL_Window* window, SDL_Event& event, int design_w, int design_h) {
    int points_w=0, points_h=0, pixels_w=0, pixels_h=0;
    if (!SDL_GetWindowSize(window, &points_w, &points_h) ||
        !SDL_GetWindowSizeInPixels(window, &pixels_w, &pixels_h) ||
        points_w <= 0 || points_h <= 0 || design_w <= 0 || design_h <= 0) return;
    const float scale = std::min(float(pixels_w)/design_w, float(pixels_h)/design_h);
    if (scale <= 0) return;
    const float sx = float(pixels_w)/points_w/scale, sy = float(pixels_h)/points_h/scale;
    const float ox = (pixels_w/scale - design_w)*.5f, oy = (pixels_h/scale - design_h)*.5f;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.x = event.motion.x*sx - ox; event.motion.y = event.motion.y*sy - oy;
        event.motion.xrel *= sx; event.motion.yrel *= sy;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        event.button.x = event.button.x*sx - ox; event.button.y = event.button.y*sy - oy;
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        event.wheel.mouse_x = event.wheel.mouse_x*sx - ox; event.wheel.mouse_y = event.wheel.mouse_y*sy - oy;
    }
}
