#include "gs2.hpp"
#include "display/RendererResource.hpp"
#include "devices/displaypp/frame/frame.hpp"
#include "devices/displaypp/RGBA.hpp"
#include "ui/AssetAtlas.hpp"
#include "util/TextRenderer.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

gs2_app_t gs2_app_values;
void system_failure(const char* message) { throw std::runtime_error(message); }
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "assets directory argument");
        gs2_app_values.base_path = std::string(argv[1]) + "/";
        require(SDL_Init(0), "SDL init");
        auto* surface = SDL_CreateSurface(256, 64, SDL_PIXELFORMAT_RGBA32);
        require(surface, "software display surface");
        SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
        require(renderer, "software renderer");
        {
            Frame<RGBA_t, 8, 8, SDLTextureStorage> frame(8, 8, renderer, PIXEL_FORMAT);
            SDL_SetTextureScaleMode(frame.get_texture(), SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(frame.get_texture(), SDL_BLENDMODE_NONE);
            AssetAtlas_t atlas(renderer, "img/atlas.png", 32, 32);
            TextRenderer text(renderer, "fonts/OpenSans-Regular.ttf", 18);
            text.set_color(255, 240, 220, 255);
            require(text.font && text.engine, "real font and text engine");
            auto draw = [&]() {
                frame.open();
                for (int y = 0; y < 8; ++y) {
                    frame.set_line(y);
                    for (int x = 0; x < 8; ++x) frame.push(RGBA_t::make(x * 30, y * 30, 90));
                }
                frame.close();
                require(SDL_GetRendererFromTexture(frame.get_texture()) == renderer, "frame rebound to renderer");
                require(text.renderer == renderer, "text renderer rebound");
                SDL_ScaleMode scale; SDL_BlendMode blend;
                require(SDL_GetTextureScaleMode(frame.get_texture(), &scale) && scale == SDL_SCALEMODE_NEAREST,
                    "frame scale preserved");
                require(SDL_GetTextureBlendMode(frame.get_texture(), &blend) && blend == SDL_BLENDMODE_NONE,
                    "frame blend preserved");
                SDL_SetRenderDrawColor(renderer, 3, 5, 7, 255); SDL_RenderClear(renderer);
                SDL_FRect target{40, 4, 32, 32};
                require(SDL_RenderTexture(renderer, frame.get_texture(), nullptr, &target), "frame draw");
                atlas.draw(0, 0, 0);
                text.render("Context restored", 76, 16);
                SDL_RenderPresent(renderer);
                std::vector<uint8_t> pixels(256 * 64 * 4);
                for (int y = 0; y < 64; ++y)
                    memcpy(pixels.data() + y * 256 * 4,
                           static_cast<uint8_t*>(surface->pixels) + y * surface->pitch, 256 * 4);
                return pixels;
            };
            auto before = draw();
            const size_t frame_pixel = (14 * 256 + 50) * 4;
            require(before[frame_pixel] == 60 && before[frame_pixel + 1] == 60 && before[frame_pixel + 2] == 90,
                "decoded frame has expected colors before reset");
            bool visible_glyph = false;
            for (int y = 16; y < 48; ++y) for (int x = 76; x < 256; ++x) {
                const size_t offset = (y * 256 + x) * 4;
                visible_glyph |= before[offset] > 180 && before[offset + 1] > 180 && before[offset + 2] > 180;
            }
            require(visible_glyph, "real TTF glyphs present before reset");
            for (int iteration = 0; iteration < 3; ++iteration) {
                auto* previous = renderer;
                RendererResource::release_all(previous);
                require(frame.get_texture() == nullptr && text.engine == nullptr, "resources released before renderer");
                SDL_DestroyRenderer(previous);
                renderer = SDL_CreateSoftwareRenderer(surface);
                require(renderer, "replacement renderer");
                RendererResource::restore_all(previous, renderer);
                require(draw() == before, "frame, atlas and glyph output unchanged after renderer replacement");
            }
        }
        // Destruction unregisters the owners: these phases must be harmless.
        RendererResource::release_all(renderer);
        RendererResource::restore_all(renderer, renderer);
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit();
        std::puts("PASS renderer replacement: frame textures, atlas, font engine, pixels, repeated recovery and unregister");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
