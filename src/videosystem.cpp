#include "gs2.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_mouse.h"
#include "computer.hpp"
#include "videosystem.hpp"
#include "display/display.hpp"
#include "ui/Clipboard.hpp"
#include "ui/ScreenshotWriter.hpp"
#include "paths.hpp"
#include "util/Event.hpp"
#include <cmath>
#include "util/dialog.hpp"
#include "util/MenuInterface.h"
#include "display/shaders/GpuShaderLoader.hpp"

video_system_t::video_system_t(computer_t *computer) {

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error initializing SDL: %s\n", SDL_GetError());
    }

    clip = new ClipboardImage();
    screenshot_writer = new ScreenshotWriter();

    display_color_engine = DM_ENGINE_NTSC;
    display_mono_color = DM_MONO_GREEN;
    display_pixel_mode = DM_PIXEL_FUZZ;

    display_fullscreen_mode = DISPLAY_WINDOWED_MODE;
    event_queue = computer->event_queue;

    // TODO: calculate an initial window size that will get us an integral scale starting out.
    window_width = (BASE_WIDTH + border_width*2) * SCALE_X;
    window_height = (BASE_HEIGHT + border_height*2) * SCALE_Y;
    aspect_ratio = (float)window_width / (float)window_height;

#ifdef __EMSCRIPTEN__
    // Preserve ES3 when SDL creates its GLES renderer (required by WebGL2).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
    window = SDL_CreateWindow(
        "GSSquared - Apple ][ Emulator", 
        (BASE_WIDTH + border_width*2) * SCALE_X, 
        (BASE_HEIGHT + border_height*2) * SCALE_Y, 
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
#ifdef __EMSCRIPTEN__
        | SDL_WINDOW_OPENGL
#endif
    );

    if (!window) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
    }

    // Set minimum and maximum window sizes to maintain reasonable dimensions
    SDL_SetWindowMinimumSize(window, window_width / 2, window_height / 2);  // Half size
    //SDL_SetWindowMaximumSize(window, window_width * 2, window_height * 2);  // 4x size
    
    // Set the window's aspect ratio to match the Apple II display (560:384)
    SDL_SetWindowAspectRatio(window, aspect_ratio, aspect_ratio);

    /* for (int i = 0; i < SDL_GetNumRenderDrivers(); i++) {
        const char *name = SDL_GetRenderDriver(i);
        printf("Render driver %d: %s\n", i, name);
    } */

    postprocessor = std::make_unique<gs2::postprocess::PostProcessor>(window);
    renderer = postprocessor->renderer();
    gpu_device = postprocessor->device();
    if (!renderer) system_failure("Failed to create video renderer");
    int initial_w = 0, initial_h = 0;
    SDL_GetWindowSizeInPixels(window, &initial_w, &initial_h);
    postprocessor->begin_scene(initial_w, initial_h);
    printf("%s\n", postprocessor->status().c_str());

    screencap_texture = SDL_CreateTexture(renderer, PIXEL_FORMAT, SDL_TEXTUREACCESS_TARGET, 910, 263);
    if (!screencap_texture) {
        printf("Failed to create txt_shr\n");
        printf("SDL Error: %s\n", SDL_GetError());
        system_failure("Failed to create screencap texture");
    }

    // Set scaling quality to nearest neighbor for sharp pixels
    //SDL_SetRenderScale(renderer, SCALE_X, SCALE_Y);

    // Clear the texture to black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    clear();
    present();

    SDL_RaiseWindow(window);

    {
        int point_w = 0, point_h = 0;
        int pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSize(window, &point_w, &point_h);
        SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
        float display_scale = SDL_GetWindowDisplayScale(window);
        float pixel_density = SDL_GetWindowPixelDensity(window);
        printf("Display: %dx%d points, %dx%d pixels, display_scale=%.3f, pixel_density=%.3f\n",
            point_w, point_h, pixel_w, pixel_h, display_scale, pixel_density);
    }

    update_target_from_output();

    computer->dispatch->registerHandler(SDL_EVENT_WINDOW_RESIZED, [this](const SDL_Event &event) {
        window_resize(event);
        return true;
    });
    computer->dispatch->registerHandler(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, [this](const SDL_Event &event) {
        window_resize(event);
        return true;
    });
    computer->sys_event->registerHandler(SDL_EVENT_KEY_UP, [this, computer](const SDL_Event &event) {
        int key = event.key.key;
        if (key == SDLK_F3) {
            toggle_fullscreen();
            return true;
        }
        if (key == SDLK_F1) { // release or capture mouse
            display_capture_mouse_message(!mouse_captured);
            return true;
        }
        if (key == SDLK_F5) {
            flip_display_scale_mode();
            return true;
        }
        if (key == SDLK_F2) {
            toggle_display_engine();
            return true;
        }
        if (key == SDLK_F7) {
            return true;
        }
        if (key == SDLK_PRINTSCREEN) {
            if (event.key.mod & SDL_KMOD_SHIFT) {
                save_screenshot();
            } else {
                copy_screen();
            }
        }
        return false;
    });
    computer->sys_event->registerHandler(SDL_EVENT_KEY_DOWN, [this, computer](const SDL_Event &event) {
        int key = event.key.key;
        switch (key) {
            case SDLK_F7:
                // Capture modifiers on keydown: releasing Shift first must not
                // turn the settings shortcut into an effects toggle.
                if (!event.key.repeat) {
                    if (event.key.mod & SDL_KMOD_SHIFT) getMenuInterface()->openEffectsSettings();
                    else getMenuInterface()->toggleCrtShader();
                }
                return true;
            case SDLK_F3:
            case SDLK_F1:
            case SDLK_F5:
            case SDLK_F2:
            case SDLK_F6:
                return true; // eat the keydown
            case SDLK_PRINTSCREEN:
                if (event.key.mod & SDL_KMOD_SHIFT) {
                    save_screenshot();
                } else {
                    copy_screen();
                }
                return true;
            default:
                return false;
        }
    });
    computer->register_reset_handler([this](bool) {
        reset_postprocess_history();last_render_mode=UINT64_MAX;return true;
    });
    register_frame_processor(-10000,[this,computer](bool) {
        auto* ds=static_cast<display_state_t*>(computer->get_module_state(MODULE_DISPLAY));
        if(ds){
            // Page flips are intentionally excluded: pair merging needs their
            // history. Mode/decoder changes start a new phosphor history.
            uint64_t mode=uint64_t(ds->display_mode)|(uint64_t(ds->display_split_mode)<<4)|
                (uint64_t(ds->display_graphics_mode)<<8)|(uint64_t(ds->f_80col)<<12)|
                (uint64_t(ds->f_double_graphics)<<13)|(uint64_t(ds->new_video)<<16)|
                (uint64_t(ds->appletini_video_enabled)<<24);
            if(mode!=last_render_mode){reset_postprocess_history();last_render_mode=mode;}
        }
        return false;
    });
    computer->sys_event->registerHandler(SDL_EVENT_MOUSE_BUTTON_DOWN, [this](const SDL_Event &event) {
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            display_capture_mouse_message(!mouse_captured);
        }
        return false;
    });
}

video_system_t::~video_system_t() {
    if (screenshot_writer) {
        delete screenshot_writer;
        screenshot_writer = nullptr;
    }
    if (guest_overlay_texture) SDL_DestroyTexture(guest_overlay_texture);
    postprocessor.reset();
    renderer = nullptr; gpu_device = nullptr;
    if (window) SDL_DestroyWindow(window);
    if (clip) delete clip;
    SDL_Quit();
}

void video_system_t::present(bool draw_logical_borders) {
    // Drain screenshot worker status on the main thread (SPSC ring → EventQueue).
    if (screenshot_writer) {
        screenshot_writer->poll(event_queue);
    }
    int logical_w = 0, logical_h = 0;
    SDL_RendererLogicalPresentation logical_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!draw_logical_borders) {
        SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &logical_mode);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    }
    postprocessor->present();
    if (!draw_logical_borders) {
        SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, logical_mode);
    }
}

void video_system_t::set_window_title(const char *title) {
    SDL_SetWindowTitle(window, title);
}

void video_system_t::render_frame(SDL_Texture *texture, SDL_FRect *srcrect, SDL_FRect *dstadj, bool respect_mode,
        const SDL_FRect *content_inset_src) {

    if(texture!=last_texture || srcrect->x!=last_srcrect.x || srcrect->y!=last_srcrect.y ||
       srcrect->w!=last_srcrect.w || srcrect->h!=last_srcrect.h) reset_postprocess_history();
    SDL_FRect adj_target;
    if (dstadj) {
        float scale_x = target.w / srcrect->w; // recalc the scale
        float scale_y = target.h / srcrect->h; // recalc the scale
        
        float xadj = dstadj->w * scale_x;
        float yadj = dstadj->h * scale_y;
        adj_target.x = target.x + xadj;
        adj_target.y = target.y + yadj;
        adj_target.w = target.w - xadj*2;
        adj_target.h = target.h - yadj*2;
    } else {
        adj_target = target;
    }

    if (content_inset_src && srcrect->w > 0.0f && srcrect->h > 0.0f) {
        const float scale_x = adj_target.w / srcrect->w;
        const float scale_y = adj_target.h / srcrect->h;
        content.x = adj_target.x + content_inset_src->x * scale_x;
        content.y = adj_target.y + content_inset_src->y * scale_y;
        content.w = content_inset_src->w * scale_x;
        content.h = content_inset_src->h * scale_y;
    } else {
        content = adj_target;
    }

    if (respect_mode) {
        if (display_pixel_mode == DM_PIXEL_FUZZ) {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        } else {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART); // SDL_SCALEMODE_NEAREST
        }
    }

    SDL_RenderTexture(renderer, texture, srcrect, &adj_target);
    last_texture = texture;
    last_srcrect = *srcrect;
}

void video_system_t::clear() {
    SDL_RenderClear(renderer);
}

void video_system_t::raise() {
    SDL_RaiseWindow(window);
}
void video_system_t::raise(SDL_Window *windowp) {
    SDL_RaiseWindow(windowp);
}

void video_system_t::hide(SDL_Window *window) {
    SDL_HideWindow(window);
}

void video_system_t::show(SDL_Window *window) {
    SDL_ShowWindow(window);
}

/* Given new window width and height, calculate the target rectangle for the display. */
void video_system_t::calculate_target_rect(int new_w, int new_h) {
    float new_aspect = (float)new_w / new_h;
    constexpr float aspect_epsilon = 0.001f;
    if (std::fabs(new_aspect - aspect_ratio) <= aspect_epsilon) {
        printf("aspect match ");   
        // how accurate will a float be here?
        target.w = new_w;
        target.h = new_h;
        target.x = 0.0f;
        target.y = 0.0f;
    } else if (new_aspect > aspect_ratio) { 
        printf("aspect wide ");
        // wider than our aspect ratio.
        target.h = new_h;
        target.w = (float) new_h * aspect_ratio;        
        target.y = 0.0f;
        target.x = ((float)new_w - target.w) / 2.0f;
    } else {
        // narrower than our aspect ratio.
        printf("aspect tall ");
        target.w = new_w;
        target.h = (float) new_w / aspect_ratio;
        target.x = 0.0f;
        target.y = ((float)new_h - target.h) / 2.0f;
    }
    printf("calculate_target_rect: (%f, %f) [%f x %f] @ %f\n", target.x, target.y, target.w, target.h, (float)target.w/target.h);
}

void video_system_t::update_target_from_output() {
    int pixel_w = 0, pixel_h = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    calculate_target_rect(pixel_w, pixel_h);
    ensure_scene_target(pixel_w, pixel_h);
}

void video_system_t::ensure_scene_target(int w, int h) {
    if (postprocessor) {
        postprocessor->begin_scene(w, h);
        scene_target = postprocessor->scene_target();
        scene_target_w = w; scene_target_h = h;
    }
}

void video_system_t::window_resize(const SDL_Event &event) {
    if (event.window.windowID != SDL_GetWindowID(window)) {
        return;
    }
    // The emulator renders in real output pixels, so size the target from the
    // renderer's pixel output rather than the event's point dimensions.
    update_target_from_output();
}

display_fullscreen_mode_t video_system_t::get_window_fullscreen() {
    return display_fullscreen_mode;
}

void video_system_t::set_window_fullscreen(display_fullscreen_mode_t mode) {
    if (mode == DISPLAY_FULLSCREEN_MODE) {
        // Borderless "fullscreen desktop": a NULL fullscreen mode tells SDL not to
        // change the display's video mode, so the window simply covers the desktop
        // at its current resolution. This avoids the slow monitor re-sync / macOS
        // Space transition that an exclusive mode switch incurs.
        SDL_SetWindowAspectRatio(window, 0.0f, 0.0f);
        SDL_SetWindowFullscreenMode(window, NULL);
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowFullscreen(window, true);
    } else {
        // Reapply window size and aspect ratio constraints in reverse order from above.
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, true);
        sync_window();
        SDL_SetWindowAspectRatio(window, aspect_ratio, aspect_ratio);
    } 
}

void video_system_t::sync_window() {
    SDL_SyncWindow(window);
}

void video_system_t::toggle_fullscreen() {
    display_fullscreen_mode = (display_fullscreen_mode_t)((display_fullscreen_mode + 1) % NUM_FULLSCREEN_MODES);
    set_window_fullscreen(display_fullscreen_mode);
}

bool video_system_t::display_capture_mouse(bool capture) {
    printf("display_capture_mouse: %d\n", capture);
    mouse_captured = capture;
    if (!SDL_SetWindowRelativeMouseMode(window, capture)) {
        printf("SDL_SetWindowRelativeMouseMode failed: %s\n", SDL_GetError());
    }
    if (!SDL_SetWindowMouseGrab(window, capture)) {
        printf("SDL_SetWindowMouseGrab failed: %s\n", SDL_GetError());
    }
    if (!SDL_SetWindowKeyboardGrab(window, capture)) {
        printf("SDL_SetWindowKeyboardGrab failed: %s\n", SDL_GetError());
    }
    return capture;
}

bool video_system_t::display_capture_mouse_message(bool capture) {
    bool oldstate = mouse_captured;
    bool result = display_capture_mouse(capture);
    if (!oldstate) {
        event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, "Mouse Captured, release with F1"));
    }
    return true;
}

void video_system_t::push_mouse_capture(bool capture) {
    old_mouse_captured = mouse_captured;
    display_capture_mouse(capture);
}

void video_system_t::pop_mouse_capture() {
    display_capture_mouse(old_mouse_captured);
}

void video_system_t::send_engine_message() {
    static char buffer[256];
    const char *display_color_engine_names[] = {
        "NTSC",
        "RGB",
        "Monochrome"
    };

    snprintf(buffer, sizeof(buffer), "Display Engine Set to %s", display_color_engine_names[display_color_engine]);
    event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, buffer));
}

void video_system_t::toggle_display_engine() {
    reset_postprocess_history();
    display_color_engine = (display_color_engine_t)((display_color_engine + 1) % DM_NUM_COLOR_ENGINES);
    send_engine_message();
}

void video_system_t::set_display_engine(display_color_engine_t mode) {
    reset_postprocess_history();
    display_color_engine = mode;
    send_engine_message();
}

void video_system_t::set_display_mono_color(display_mono_color_t mode) {
    reset_postprocess_history();
    display_mono_color = mode;
}

void video_system_t::flip_display_scale_mode() {
    reset_postprocess_history();
    SDL_ScaleMode scale_mode;

    if (display_pixel_mode == DM_PIXEL_FUZZ) {
        display_pixel_mode = DM_PIXEL_SQUARE;
        scale_mode = SDL_SCALEMODE_PIXELART;
    } else {
        display_pixel_mode = DM_PIXEL_FUZZ;
        scale_mode = SDL_SCALEMODE_LINEAR;
    }
}

void video_system_t::set_crt_shader_enabled(bool enabled, bool show_message) {
    if (!crt_shader_available()) {
        if (show_message) {
            show_crt_shader_unavailable();
        }
        return;
    }
    crt_shader_enabled = enabled;
    postprocessor->settings().p_i_postprocessingLevel = enabled ? 2 : 0;
    postprocessor->settings_changed();
    if (show_message) {
        event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0,
            crt_shader_enabled ? "CRT Shader On" : "CRT Shader Off"));
    }
}

void video_system_t::show_crt_shader_unavailable() {
    event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, postprocessor->status().c_str()));
}

void video_system_t::toggle_crt_shader() {
    if (!crt_shader_available()) {
        show_crt_shader_unavailable();
        return;
    }
    set_crt_shader_enabled(!crt_shader_enabled, true);
}

SDL_Surface *video_system_t::capture_screen_surface(bool* double_vertical) {
    if(double_vertical) *double_vertical=!postprocessor->available() && last_srcrect.h<=263;
    if(postprocessor->available()) return postprocessor->capture_processed();
    if (!last_texture) return nullptr;
    const int needed_w=std::max(1,int(std::ceil(last_srcrect.x+last_srcrect.w)));
    const int needed_h=std::max(1,int(std::ceil(last_srcrect.y+last_srcrect.h)));
    float capture_w=0,capture_h=0;
    if(screencap_texture) SDL_GetTextureSize(screencap_texture,&capture_w,&capture_h);
    if(capture_w<needed_w||capture_h<needed_h){
        if(screencap_texture) SDL_DestroyTexture(screencap_texture);
        screencap_texture=SDL_CreateTexture(renderer,PIXEL_FORMAT,SDL_TEXTUREACCESS_TARGET,needed_w,needed_h);
        if(!screencap_texture)return nullptr;
    }
    SDL_Rect srect = { (int)last_srcrect.x, (int)last_srcrect.y, (int)last_srcrect.w, (int)last_srcrect.h };
    SDL_FRect trect = { last_srcrect.x, last_srcrect.y, last_srcrect.w, last_srcrect.h };
    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, screencap_texture);
    SDL_SetTextureBlendMode(last_texture, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(renderer, last_texture, &trect, &trect); // ensure no scaling.
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, &srect);
    SDL_SetRenderTarget(renderer, previous_target);
    return surface;
}

void video_system_t::copy_screen() {
    bool double_vertical = false;
    SDL_Surface *surface = capture_screen_surface(&double_vertical);
    if (!surface) {
        return;
    }
    clip->Clip(surface, double_vertical);
    SDL_DestroySurface(surface);
}

void video_system_t::save_screenshot() {
    if (!screenshot_writer) {
        return;
    }
    if (screenshot_writer->is_pending()) {
        event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, "Screenshot busy"));
        return;
    }
    bool double_vertical = false;
    SDL_Surface *surface = capture_screen_surface(&double_vertical);
    if (!surface) {
        event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, "Screenshot capture failed"));
        return;
    }
    const std::string path = Paths::make_screenshot_path();
    if (!screenshot_writer->try_submit(surface, path, double_vertical)) {
        event_queue->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, "Screenshot busy"));
    }
    SDL_DestroySurface(surface);
}

void video_system_t::register_frame_processor(int weight, FrameHandler handler) {
    frame_handlers.insert({weight, handler});
}

void video_system_t::update_display(bool force_full_frame) {
    int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h);
    if (w<=0 || h<=0) return;
    ensure_scene_target(w,h);
    logical_scanlines=0; fields_already_composed=false;
    postprocess_sample_width=postprocess_sample_height=0;
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    clear();
    for (const auto& pair : frame_handlers) if (pair.second(force_full_frame)) break;
    if (guest_overlay_provider) {
        const auto overlay=guest_overlay_provider();
        // The provider reports its firmware canvas even while hidden. Keep the
        // filter's sampling grid stable as linear text is shown or hidden, and
        // account for decoded borders surrounding the active guest picture.
        if(overlay.width>0&&overlay.height>0){
            float sx=content.w>0?target.w/content.w:1.0f;
            float sy=content.h>0?target.h/content.h:1.0f;
            postprocess_sample_width=std::max(postprocess_sample_width,int(std::lround(overlay.width*sx)));
            postprocess_sample_height=std::max(postprocess_sample_height,int(std::lround(overlay.height*sy)));
        }
        if (overlay.visible && overlay.pixels && overlay.width>0 && overlay.height>0) {
            if (!guest_overlay_texture || guest_overlay_width!=overlay.width || guest_overlay_height!=overlay.height) {
                if (guest_overlay_texture) SDL_DestroyTexture(guest_overlay_texture);
                guest_overlay_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,overlay.width,overlay.height);
                guest_overlay_width=overlay.width;guest_overlay_height=overlay.height;guest_overlay_generation=UINT64_MAX;
                if (guest_overlay_texture) {
                    SDL_SetTextureBlendMode(guest_overlay_texture,SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(guest_overlay_texture,SDL_SCALEMODE_NEAREST);
                }
            }
            if (guest_overlay_texture) {
                if (guest_overlay_generation!=overlay.generation) {
                    SDL_UpdateTexture(guest_overlay_texture,nullptr,overlay.pixels,overlay.width*4);
                    guest_overlay_generation=overlay.generation;
                }
                const SDL_FRect rect=content.w>0 && content.h>0 ? content : target;
                SDL_RenderTexture(renderer,guest_overlay_texture,nullptr,&rect);
            }
        }
    }
    ++rendered_frame_identity;
}

void video_system_t::present_scene() {
    gs2::postprocess::FrameView frame;
    frame.source_width=std::max(1,static_cast<int>(last_srcrect.w));
    frame.source_height=std::max(1,static_cast<int>(last_srcrect.h));
    frame.scanlines=logical_scanlines ? logical_scanlines : frame.source_height;
    frame.identity=rendered_frame_identity;
    frame.fields_already_composed=fields_already_composed;
    frame.sample_width=postprocess_sample_width;frame.sample_height=postprocess_sample_height;
    frame.seconds=SDL_GetTicksNS()/1000000000.0;
    if(scene_target_w>0 && scene_target_h>0 && target.w>0 && target.h>0)
        frame.source_region={target.x/scene_target_w,target.y/scene_target_h,target.w/scene_target_w,target.h/scene_target_h};
    postprocessor->begin_ui(frame);
}

void video_system_t::release_postprocessor() {
    if(screencap_texture) SDL_DestroyTexture(screencap_texture);
    screencap_texture=nullptr;
    if(guest_overlay_texture) SDL_DestroyTexture(guest_overlay_texture);
    guest_overlay_texture=nullptr;guest_overlay_generation=UINT64_MAX;
    last_texture=nullptr;scene_target=nullptr;
    postprocessor->release_renderer();renderer=nullptr;gpu_device=nullptr;
}

bool video_system_t::recreate_postprocessor() {
    release_postprocessor();
    bool okay=postprocessor->recreate();
    renderer=postprocessor->renderer();gpu_device=postprocessor->device();
    int w=0,h=0;SDL_GetWindowSizeInPixels(window,&w,&h);
    ensure_scene_target(w,h);reset_postprocess_history();
    return okay;
}

void video_system_t::begin_host_ui() {
    int w=0,h=0;SDL_GetWindowSizeInPixels(window,&w,&h);
    ensure_scene_target(w,h);
    reset_postprocess_history();
    SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    gs2::postprocess::FrameView frame;
    frame.source_width=w;frame.source_height=h;frame.scanlines=h;
    frame.identity=++rendered_frame_identity;
    postprocessor->begin_ui(frame);
}
