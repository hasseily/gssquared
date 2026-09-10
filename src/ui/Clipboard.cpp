#include "Clipboard.hpp"
#include "CapturePixels.hpp"
#include <memory>

namespace {
const void* SDLCALL clipboard_data(void* userdata, const char*, size_t* size) {
    const auto* bytes = static_cast<const std::vector<uint8_t>*>(userdata);
    *size = bytes->size();
    return bytes->data();
}
void SDLCALL clipboard_cleanup(void* userdata) {
    delete static_cast<std::vector<uint8_t>*>(userdata);
}
}

void ClipboardImage::Clip(SDL_Surface* surface, bool double_vertical) {
    auto bytes = std::make_unique<std::vector<uint8_t>>();
    if (!capture_bmp(surface, double_vertical, *bytes)) return;
    const char* mime_types[] = {"image/bmp"};
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
    // With valid parameters and a video device SDL takes ownership before
    // invoking the platform backend; even a backend failure may retain it.
    if (!SDL_SetClipboardData(clipboard_data, clipboard_cleanup, bytes.release(), mime_types, 1)) {
        SDL_Log("Failed to set clipboard data: %s", SDL_GetError());
        SDL_ClearClipboardData();
    }
}
