#pragma once

#include "PostProcessSettings.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace gs2::postprocess {

struct FrameView {
    SDL_FRect source_region{0, 0, 1, 1}; // normalized region in the scene
    int source_width = 560, source_height = 192;
    int scanlines = 192;
    int sample_width = 0, sample_height = 0; // native composed pixels, independent of field scanlines
    uint64_t identity = 0;
    double seconds = 0;
    bool fields_already_composed = false;
};

// Owns GPU resources; SDL's renderer remains the scene and host-UI producer.
// Native scene submission and effect passes share a device without readback.
class PostProcessor {
public:
    explicit PostProcessor(SDL_Window* window);
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    SDL_Renderer* renderer() const;
    SDL_GPUDevice* device() const;
    bool available() const;
    const std::string& status() const;
    Settings& settings();
    const Settings& settings() const;
    void settings_changed();
    void set_assets(const std::string& bezel_path, const std::string& glass_path);
    void reset_history();
    // Release GPU objects while a lost context still accepts no-op deletes.
    // Settings and requested assets remain available until recreation.
    void release_renderer();
    bool recreate();
    bool set_vsync(int enabled);
    // Maps drawable pixels through CRT geometry back into the original scene.
    bool map_output_to_scene(float& x,float& y) const;
    // begin_scene sets the SDL target. begin_ui saves the frame descriptor and
    // starts a transparent host overlay; present submits the completed image.
    bool begin_scene(int width, int height);
    void begin_ui(const FrameView& frame);
    bool present();
    SDL_Texture* scene_target() const;
    // Screenshot / test readback only. Normal rendering does not use this.
    SDL_Surface* capture_processed(); // CRT, bezel and glass; excludes host UI
    SDL_Surface* capture_crt();

private:
    // Controls retain references to settings while graphics resources are rebuilt.
    Settings settings_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gs2::postprocess
