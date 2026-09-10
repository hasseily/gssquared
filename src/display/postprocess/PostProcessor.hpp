#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace gs2::postprocess {
struct FrameView {
    SDL_FRect source_region{0,0,1,1};
    int source_width=560,source_height=192;
};

// SDL renders guest content and host UI into separate targets. Native effects
// share SDL's GPU device; GL/WebGL use imported textures without CPU readback.
class PostProcessor {
public:
    explicit PostProcessor(SDL_Window* window);
    ~PostProcessor();
    PostProcessor(const PostProcessor&)=delete;
    PostProcessor& operator=(const PostProcessor&)=delete;
    SDL_Renderer* renderer() const;
    SDL_GPUDevice* device() const;
    bool available() const;
    const std::string& status() const;
    void set_enabled(bool enabled);
    void release_renderer();
    bool recreate();
    bool set_vsync(int enabled);
    bool begin_scene(int width,int height);
    void begin_ui(const FrameView& frame);
    bool present();
    SDL_Texture* scene_target() const;
    SDL_Surface* capture_processed(); // screenshot-only readback; excludes host UI
private:
    bool enabled_=false;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gs2::postprocess
