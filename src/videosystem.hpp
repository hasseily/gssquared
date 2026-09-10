#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <map>
#include <memory>
#include "display/postprocess/PostProcessor.hpp"
#include "computer.hpp"
#include "util/EventQueue.hpp"
#include "display/types.hpp"
#include "ui/Clipboard.hpp"
#include "ui/ScreenshotWriter.hpp"
#include "devices/displaypp/RGBA.hpp"

// somewhere calculate the window size properly (42+49, and 19+21)
#define BORDER_WIDTH 42
#define BORDER_HEIGHT 20

typedef enum {
    DISPLAY_WINDOWED_MODE = 0,
    DISPLAY_FULLSCREEN_MODE = 1,
    NUM_FULLSCREEN_MODES
} display_fullscreen_mode_t;

/** Display Modes */

typedef enum {
    DM_ENGINE_NTSC = 0,
    DM_ENGINE_RGB,
    DM_ENGINE_MONO,
    DM_NUM_COLOR_ENGINES
} display_color_engine_t;

typedef enum {
    DM_MONO_WHITE = 0,
    DM_MONO_GREEN,
    DM_MONO_AMBER,
    DM_NUM_MONO_MODES
} display_mono_color_t;

typedef enum {
    DM_PIXEL_FUZZ = 0,
    DM_PIXEL_SQUARE,
    DM_NUM_PIXEL_MODES
} display_pixel_mode_t;


struct video_system_t {
    using FrameHandler = std::function<bool(bool)>;

    std::multimap<int, FrameHandler, std::greater<int>> frame_handlers;

    SDL_Window *window; // primary emulated display window
    SDL_Renderer* renderer;
    // Non-null when the renderer is the SDL GPU-backed renderer (required for
    // custom fragment-shader post-processing). Null means we fell back to the
    // classic renderer and shader effects are unavailable.
    SDL_GPUDevice *gpu_device = nullptr;
    std::unique_ptr<gs2::postprocess::PostProcessor> postprocessor;
    SDL_Texture *scene_target = nullptr; // non-owning alias of postprocessor target
    int scene_target_w = 0, scene_target_h = 0;
    bool crt_shader_enabled = false;
    std::function<gs2::postprocess::GuestOverlayFrame()> guest_overlay_provider;
    SDL_Texture* guest_overlay_texture = nullptr;
    uint64_t guest_overlay_generation = UINT64_MAX;
    int guest_overlay_width = 0, guest_overlay_height = 0;
    unsigned logical_scanlines = 0;
    int postprocess_sample_width=0,postprocess_sample_height=0;
    bool fields_already_composed = false;
    uint64_t rendered_frame_identity = 0;
    uint64_t last_render_mode = UINT64_MAX;
    SDL_Texture *screencap_texture = nullptr;
    
    display_fullscreen_mode_t display_fullscreen_mode = DISPLAY_WINDOWED_MODE;
    display_color_engine_t display_color_engine = DM_ENGINE_NTSC;
    display_mono_color_t display_mono_color = DM_MONO_GREEN;
    display_pixel_mode_t display_pixel_mode = DM_PIXEL_FUZZ;

    SDL_FRect target = { 0.0f, 0.0f, 0.0f, 0.0f };
    // Guest-visible screen area within the render target (excludes bezel borders).
    SDL_FRect content = { 0.0f, 0.0f, 0.0f, 0.0f };

    int border_width = BORDER_WIDTH;
    int border_height = BORDER_HEIGHT;
    float aspect_ratio = 0.0;
    /* float scale_x = 2.0f;
    float scale_y = 4.0f; */
    int window_width = 0;
    int window_height = 0;

    EventQueue *event_queue = nullptr;

    ClipboardImage *clip = nullptr;
    ScreenshotWriter *screenshot_writer = nullptr;

    bool mouse_captured = false;
    bool old_mouse_captured = false;
    // True while the OSD control panel or a modal dialog needs the host cursor visible.
    bool osd_control_panel_open = false;
    
    SDL_Texture *last_texture = nullptr;
    SDL_FRect last_srcrect = { 0.0f, 0.0f, 0.0f, 0.0f };

    RGBA_t mono_color_table[DM_NUM_MONO_MODES] = {
        RGBA_t::make(0xFF, 0xFF, 0xFF), // white
        RGBA_t::make(0x00, 0xFF, 0x4A), // green (was 55) chosen from measuring @ 549nm
        RGBA_t::make(0xFF, 0xBF, 0x00)  // amber
    };

protected:
    void calculate_target_rect(int new_w, int new_h);
    // Recompute the target rect from the renderer's real pixel output size
    // (not window points), so the emulator image is sized for the high-DPI backbuffer.
    void update_target_from_output();
    // Create/recreate the offscreen scene_target to match (w x h) pixels. No-op
    // when the CRT shader is unavailable. Called at init and on resize.
    void ensure_scene_target(int w, int h);
    void show_crt_shader_unavailable();
    // GPU readback of the last presented guest frame (+ borders). Caller owns the surface.
    SDL_Surface *capture_screen_surface(bool* double_vertical=nullptr);

public:
    video_system_t(computer_t *computer);
    ~video_system_t();
    void set_window_title(const char *title);
    void window_resize(const SDL_Event &event);
    void toggle_fullscreen();
    void set_window_fullscreen(display_fullscreen_mode_t mode);
    display_fullscreen_mode_t get_window_fullscreen();
    void sync_window();
    void render_frame(SDL_Texture *texture, SDL_FRect *srcrect, SDL_FRect *dstadj, bool respect_mode = true,
        const SDL_FRect *content_inset_src = nullptr);
    void clear();
    void present();
    bool display_capture_mouse(bool capture);
    bool display_capture_mouse_message(bool capture);
    bool is_mouse_captured();
    void raise();
    void raise(SDL_Window *window);
    void hide(SDL_Window *window);
    void show(SDL_Window *window);
    void send_engine_message();
    void toggle_display_engine();
    void set_display_engine(display_color_engine_t mode);
    void set_display_mono_color(display_mono_color_t mode);
    void copy_screen();
    void save_screenshot();
    void flip_display_scale_mode();
    // True when the CRT post-process shader is available to be used.
    bool crt_shader_available() const { return postprocessor && postprocessor->available(); }
    bool get_crt_shader_enabled() const { return crt_shader_enabled; }
    void set_crt_shader_enabled(bool enabled, bool show_message = false);
    void toggle_crt_shader();
    void register_frame_processor(int weight, FrameHandler handler);
    void update_display(bool force_full_frame = false);
    // Finish guest composition and start the independent host UI layer. Called
    // once per frame after update_display() and before the OSD is drawn.
    void present_scene();
    gs2::postprocess::Settings& postprocess_settings() { return postprocessor->settings(); }
    const gs2::postprocess::Settings& postprocess_settings() const { return postprocessor->settings(); }
    bool postprocess_available() const { return crt_shader_available(); }
    const std::string& postprocess_status() const { return postprocessor->status(); }
    void postprocess_settings_changed() { postprocessor->settings_changed(); crt_shader_enabled = postprocessor->settings().p_i_postprocessingLevel != 0; }
    void set_postprocess_assets(const std::string& bezel, const std::string& glass) { postprocessor->set_assets(bezel, glass); }
    void release_postprocessor();
    bool recreate_postprocessor();
    bool set_vsync(int enabled) { return postprocessor->set_vsync(enabled); }
    void begin_host_ui();
    bool map_output_to_scene(float& x,float& y) const { return postprocessor->map_output_to_scene(x,y); }
    void reset_postprocess_history() { if (postprocessor) postprocessor->reset_history(); }
    void set_guest_overlay_provider(std::function<gs2::postprocess::GuestOverlayFrame()> provider) { guest_overlay_provider = std::move(provider); }
    void set_fields_already_composed(bool value) { fields_already_composed = value; }
    void set_postprocess_sample_dimensions(int w,int h) { postprocess_sample_width=w;postprocess_sample_height=h; }
    void set_logical_scanlines(unsigned count) { logical_scanlines = count; }

    void push_mouse_capture(bool capture);
    void pop_mouse_capture();
    RGBA_t get_mono_color() { return mono_color_table[display_mono_color]; };
};
