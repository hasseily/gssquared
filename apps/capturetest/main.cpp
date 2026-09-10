#include "ui/CapturePixels.hpp"
#include "ui/ScreenshotWriter.hpp"
#include "ui/Clipboard.hpp"
#include <SDL3_image/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void expect(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); std::exit(1); }
}
void check(SDL_Surface* surface, bool doubled) {
    std::vector<uint8_t> actual;
    int width = 0, height = 0;
    expect(capture_rgba(surface, false, actual, width, height), "decode result");
    expect(width == 1025 && height == (doubled ? 1400 : 700), "dynamic dimensions and aspect");
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto* pixel = actual.data() + (size_t(y) * width + x) * 4;
        expect(pixel[0] == uint8_t(x) && pixel[1] == uint8_t(y / (doubled ? 2 : 1)) &&
            pixel[2] == uint8_t(x ^ (y / (doubled ? 2 : 1))) && pixel[3] == 255,
            "surface conversion, padding and row orientation");
    }
}
}
int main() {
    // The dummy backend exercises SDL clipboard ownership without touching the
    // user's system clipboard or requiring a desktop display on CI.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    expect(SDL_Init(SDL_INIT_VIDEO), "SDL init");
    // Odd width exercises BMP row padding; padded BGRA rows exercise pitch and
    // explicit format conversion. Dimensions exceed both old hardcoded limits.
    const int w = 1025, h = 700, pitch = w * 4 + 12;
    std::vector<uint8_t> source(size_t(pitch) * h, 0xBD);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        auto* pixel = source.data() + size_t(y) * pitch + x * 4;
        pixel[0] = x ^ y; pixel[1] = y; pixel[2] = x; pixel[3] = 255;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_BGRA32, source.data(), pitch);
    expect(surface != nullptr, "source surface");
    for (bool doubled : {false, true}) {
        std::vector<uint8_t> bmp;
        expect(capture_bmp(surface, doubled, bmp), "clipboard BMP encoding");
        { ClipboardImage clipboard; clipboard.Clip(surface, doubled); }
        size_t clipboard_size = 0;
        void* clipboard = SDL_GetClipboardData("image/bmp", &clipboard_size);
        expect(clipboard && clipboard_size == bmp.size() &&
            std::memcmp(clipboard, bmp.data(), bmp.size()) == 0,
            "clipboard owns snapshot after producer destruction");
        SDL_free(clipboard);
        SDL_Surface* decoded = SDL_LoadBMP_IO(SDL_IOFromConstMem(bmp.data(), bmp.size()), true);
        expect(decoded != nullptr, "clipboard BMP decoding"); check(decoded, doubled); SDL_DestroySurface(decoded);
        const auto path = std::filesystem::temp_directory_path() /
            ("gs2-capture-" + std::to_string(SDL_GetTicksNS()) + ".png");
        {
            ScreenshotWriter writer;
            expect(writer.try_submit(surface, path.string(), doubled), "submit PNG worker");
            const uint64_t deadline = SDL_GetTicks() + 10000;
            while (writer.is_pending() && SDL_GetTicks() < deadline) SDL_Delay(1);
            expect(!writer.is_pending(), "PNG worker completed");
            decoded = IMG_Load(path.string().c_str());
            expect(decoded != nullptr, "PNG decoding"); check(decoded, doubled); SDL_DestroySurface(decoded);
        }
        std::filesystem::remove(path);
    }
    const auto final_path = std::filesystem::temp_directory_path() /
        ("gs2-capture-exit-" + std::to_string(SDL_GetTicksNS()) + ".png");
    {
        ScreenshotWriter writer;
        expect(writer.try_submit(surface, final_path.string()), "submit before shutdown");
    }
    SDL_Surface* final_image = IMG_Load(final_path.string().c_str());
    expect(final_image != nullptr, "shutdown drains accepted screenshot");
    check(final_image, false); SDL_DestroySurface(final_image);
    std::filesystem::remove(final_path);
    SDL_DestroySurface(surface);
    SDL_Quit();
    std::puts("Capture PNG/clipboard BMP: large padded BGRA, exact processed aspect, optional raw doubling passed");
}
