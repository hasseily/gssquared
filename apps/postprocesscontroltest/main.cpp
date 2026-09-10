#include "gs2.hpp"
#include "ui/NumericSlider.hpp"
#include "ui/UIContext.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

gs2_app_t gs2_app_values;
void system_failure(const char* message) { throw std::runtime_error(message); }
static void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
static const gs2::postprocess::Parameter& field(const char* key) {
    const auto& all = gs2::postprocess::parameters();
    const auto it = std::find_if(all.begin(), all.end(), [key](const auto& p) { return std::string(p.key) == key; });
    if (it == all.end()) throw std::runtime_error("Missing control field");
    return *it;
}
static void click(NumericSlider_t& row, float x, float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x; event.button.y = y; row.handle_mouse_event(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP; row.handle_mouse_event(event);
}
static void key(NumericSlider_t& row, SDL_Keycode code, SDL_Scancode scan = SDL_SCANCODE_UNKNOWN) {
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code; event.key.scancode = scan; row.handle_mouse_event(event);
}
static void clear_input(NumericSlider_t& row) {
    click(row, 504, 43);
    for (int i = 0; i < 20; ++i) key(row, SDLK_BACKSPACE);
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Asset path required");
        gs2_app_values.base_path = std::string(argv[1]) + "/";
        require(SDL_Init(SDL_INIT_VIDEO), "SDL video init");
        auto* surface = SDL_CreateSurface(520, 140, SDL_PIXELFORMAT_RGBA32);
        auto* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        require(renderer, "Software renderer");
        {
            TextRenderer text(renderer, "fonts/OpenSans-Regular.ttf", 15);
            require(text.font && text.engine, "Control font");
            UIContext ctx{renderer, nullptr, &text};
            gs2::postprocess::Settings settings;
            int changes = 0;
            NumericSlider_t row(&ctx, field("p_f_brightness"), settings, [&] { ++changes; });
            row.set_position(10, 10); row.size(500, 49); row.render();
            click(row, 202, 45);
            require(std::abs(settings.p_f_brightness - 50) < .01, "Slider updates the live value");
            row.render();
            clear_input(row);
            key(row, SDLK_1, SDL_SCANCODE_1); key(row, SDLK_PERIOD, SDL_SCANCODE_PERIOD);
            key(row, SDLK_2, SDL_SCANCODE_2); key(row, SDLK_5, SDL_SCANCODE_5);
            require(settings.p_f_brightness == 50, "Typing waits for commit");
            key(row, SDLK_RETURN);
            require(std::abs(settings.p_f_brightness - 1.25) < .0001, "Enter commits a precise typed value");
            row.render();
            clear_input(row);
            for (int i = 0; i < 3; ++i) key(row, SDLK_9, SDL_SCANCODE_9);
            const int before_invalid = changes;
            key(row, SDLK_RETURN);
            require(settings.p_f_brightness == 1.25 && changes == before_invalid,
                    "An out-of-range edit leaves live settings unchanged");
            row.finish_edit(); row.render();
            row.set_visible(false); click(row, 388, 45);
            require(settings.p_f_brightness == 1.25, "Hidden controls do not consume clicks");
            NumericSlider_t toggle(&ctx, field("p_b_smoothCorner"), settings, [&] { ++changes; });
            toggle.set_position(10, 75); toggle.size(500, 49); toggle.render();
            click(toggle, 40, 100); require(settings.p_b_smoothCorner, "Boolean control enables");
            click(toggle, 40, 100); require(!settings.p_b_smoothCorner, "Boolean control disables");
            SDL_Surface* captured = SDL_RenderReadPixels(renderer, nullptr);
            require(captured && captured->w == 520 && captured->h == 140, "Controls render to the host UI target");
            SDL_DestroySurface(captured);
        }
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit();
        std::puts("Postprocessing control interaction checks passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s: %s\n", error.what(), SDL_GetError());
        return 1;
    }
}
