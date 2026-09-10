#include "PostProcessor.hpp"
#include "display/shaders/GpuShaderLoader.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten/html5_webgl.h>
#elif defined(__linux__)
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#endif

namespace gs2::postprocess {
namespace {
using Uniforms = std::array<float,4>;
constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr const char* kShaderRoot = "shaders/postprocess/";

}

struct PostProcessor::Impl {
    enum class Backend { Plain, Native, GL } backend=Backend::Plain;
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_GPUDevice* gpu=nullptr;
    SDL_GPUGraphicsPipeline* crt_pipeline=nullptr;
    SDL_GPUGraphicsPipeline* composite_pipeline=nullptr;
    SDL_GPUSampler* linear_sampler=nullptr;
    struct Texture { SDL_GPUTexture* gpu=nullptr; SDL_Texture* sdl=nullptr; unsigned gl=0; };
    Texture scene,ui,processed;
    int width=0,height=0;
    bool completed=false,ui_started=false;
    int vsync=1;
    bool& enabled;
    FrameView frame;
    std::string message="Postprocessing is unavailable";
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    unsigned gl_crt=0,gl_composite=0,gl_vao=0,gl_ubo=0,gl_fbo=0;
#endif

    explicit Impl(SDL_Window* w, bool& e):window(w),enabled(e) {
#ifndef __EMSCRIPTEN__
        if (init_native()) return;
        cleanup();
#endif
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        if (init_gl()) return;
        cleanup();
#endif
        backend=Backend::Plain;
        renderer=SDL_CreateRenderer(window,nullptr);
        message="Postprocessing unavailable: no supported shader renderer ("+std::string(SDL_GetError())+")";
    }
    ~Impl() { cleanup(); }
    void free_texture(Texture& t) {
        if (t.sdl) SDL_DestroyTexture(t.sdl);
        if (t.gpu && gpu) SDL_ReleaseGPUTexture(gpu,t.gpu);
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        if (t.gl) glDeleteTextures(1,&t.gl);
#endif
        t={};
    }
    void cleanup_targets() {
        free_texture(scene);free_texture(ui);free_texture(processed);
        width=height=0;completed=false;
    }
    void cleanup() {
        cleanup_targets();
        if (crt_pipeline && gpu) SDL_ReleaseGPUGraphicsPipeline(gpu,crt_pipeline);
        if (composite_pipeline && gpu) SDL_ReleaseGPUGraphicsPipeline(gpu,composite_pipeline);
        if (linear_sampler && gpu) SDL_ReleaseGPUSampler(gpu,linear_sampler);
        crt_pipeline=composite_pipeline=nullptr;linear_sampler=nullptr;
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        if (gl_crt) glDeleteProgram(gl_crt);if(gl_composite)glDeleteProgram(gl_composite);
        if(gl_vao)glDeleteVertexArrays(1,&gl_vao);if(gl_ubo)glDeleteBuffers(1,&gl_ubo);
        if(gl_fbo)glDeleteFramebuffers(1,&gl_fbo);
        gl_crt=gl_composite=gl_vao=gl_ubo=gl_fbo=0;
#endif
        if(renderer)SDL_DestroyRenderer(renderer);renderer=nullptr;
        if(gpu){SDL_ReleaseWindowFromGPUDevice(gpu,window);SDL_DestroyGPUDevice(gpu);}gpu=nullptr;
    }
    SDL_GPUShader* shader(const char* name,SDL_GPUShaderStage stage,unsigned samplers,unsigned uniforms) {
        auto formats=SDL_GetGPUShaderFormats(gpu);
        const char* suffix=(formats&SDL_GPU_SHADERFORMAT_MSL)?".metal":
            (formats&SDL_GPU_SHADERFORMAT_DXIL)?".dxil":".spv";
        auto path=std::string(kShaderRoot)+name+suffix;
        return create_gpu_shader_from_resource(gpu,path.c_str(),stage,samplers,uniforms,
            (formats&SDL_GPU_SHADERFORMAT_MSL)?"main0":"main");
    }
    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUShader* vertex,SDL_GPUShader* frag,SDL_GPUTextureFormat format) {
        SDL_GPUColorTargetDescription target{};target.format=format;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader=vertex;info.fragment_shader=frag;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&target;
        return SDL_CreateGPUGraphicsPipeline(gpu,&info);
    }
    bool init_native() {
        gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV,false,nullptr);
        if(!gpu || !SDL_ClaimWindowForGPUDevice(gpu,window))return false;
        renderer=SDL_CreateGPURenderer(gpu,nullptr);
        if(!renderer)return false;
        auto* vertex=shader("fullscreen.vert",SDL_GPU_SHADERSTAGE_VERTEX,0,0);
        auto* crt=shader("crt.frag",SDL_GPU_SHADERSTAGE_FRAGMENT,1,1);
        auto* compose=shader("composite.frag",SDL_GPU_SHADERSTAGE_FRAGMENT,2,0);
        if(vertex&&crt&&compose){
            crt_pipeline=pipeline(vertex,crt,kFormat);
            composite_pipeline=pipeline(vertex,compose,SDL_GetGPUSwapchainTextureFormat(gpu,window));
        }
        if(vertex)SDL_ReleaseGPUShader(gpu,vertex);
        if(crt)SDL_ReleaseGPUShader(gpu,crt);
        if(compose)SDL_ReleaseGPUShader(gpu,compose);
        if(!crt_pipeline||!composite_pipeline)return false;
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
        sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.max_lod=32.0f;
        linear_sampler=SDL_CreateGPUSampler(gpu,&sampler);
        if(!linear_sampler)return false;
        backend=Backend::Native;
        message="Postprocessing: SDL GPU / "+std::string(SDL_GetGPUDeviceDriver(gpu));
        return true;
    }
    bool make_texture(Texture& t,int w,int h) {
        if(backend==Backend::Native){
            SDL_GPUTextureCreateInfo info{};
            info.type=SDL_GPU_TEXTURETYPE_2D;info.format=kFormat;
            info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            info.width=w;info.height=h;info.layer_count_or_depth=1;
            info.num_levels=1;
            info.sample_count=SDL_GPU_SAMPLECOUNT_1;
            t.gpu=SDL_CreateGPUTexture(gpu,&info);
            if(!t.gpu)return false;
            SDL_PropertiesID props=SDL_CreateProperties();
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
            SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER,t.gpu);
            t.sdl=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        }
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        else if(backend==Backend::GL){
            glGenTextures(1,&t.gl);glBindTexture(GL_TEXTURE_2D,t.gl);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            SDL_PropertiesID props=SDL_CreateProperties();
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
#ifdef __EMSCRIPTEN__
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER,t.gl);
#else
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_OPENGL_TEXTURE_NUMBER,t.gl);
#endif
            t.sdl=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        }
#endif
        if(t.sdl){SDL_SetTextureBlendMode(t.sdl,SDL_BLENDMODE_NONE);return true;}
        free_texture(t);return false;
    }
    bool resize(int w,int h) {
        if(w<=0||h<=0)return false;
        if(width==w&&height==h&&scene.sdl)return true;
        if(renderer)SDL_FlushRenderer(renderer);
        cleanup_targets();
        if(!make_texture(scene,w,h)||!make_texture(ui,w,h)||!make_texture(processed,w,h)){
            message="Postprocessing: could not allocate render targets: "+std::string(SDL_GetError());
            cleanup_targets();return false;
        }
        width=w;height=h;
        SDL_SetRenderTarget(renderer,scene.sdl);
        return true;
    }
    Uniforms uniforms() const {
        // Preserve the existing F7 CRT's source-pixel scanline frequency.
        float rw=std::max(1.0f,width*frame.source_region.w);
        float rh=std::max(1.0f,height*frame.source_region.h);
        return {width*float(frame.source_width)/rw,2*height*float(frame.source_height)/rh,float(enabled),0};
    }
    void gpu_draw(SDL_GPUCommandBuffer* commands,SDL_GPUTexture* destination,SDL_GPUGraphicsPipeline* p,
        const SDL_GPUTextureSamplerBinding* bindings,unsigned n,const Uniforms& u) {
        SDL_GPUColorTargetInfo target{};target.texture=destination;target.load_op=SDL_GPU_LOADOP_CLEAR;
        target.store_op=SDL_GPU_STOREOP_STORE;target.clear_color={0,0,0,1};
        auto* pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);
        SDL_BindGPUGraphicsPipeline(pass,p);
        SDL_BindGPUFragmentSamplers(pass,0,bindings,n);
        if(p==crt_pipeline)SDL_PushGPUFragmentUniformData(commands,0,u.data(),sizeof(u));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    }
    bool native_present(const Uniforms& u) {
        if(!SDL_RenderPresent(renderer))return false; // submit SDL's offscreen scene and UI work
        auto* cmd=SDL_AcquireGPUCommandBuffer(gpu);if(!cmd)return false;
        SDL_GPUTextureSamplerBinding input{scene.gpu,linear_sampler};
        gpu_draw(cmd,processed.gpu,crt_pipeline,&input,1,u);
        SDL_GPUTexture* swap=nullptr;Uint32 sw=0,sh=0;
        if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,window,&swap,&sw,&sh)){SDL_CancelGPUCommandBuffer(cmd);return false;}
        if(swap){
            SDL_GPUTextureSamplerBinding inputs[2]={{processed.gpu,linear_sampler},{ui.gpu,linear_sampler}};
            gpu_draw(cmd,swap,composite_pipeline,inputs,2,u);
        }
        return SDL_SubmitGPUCommandBuffer(cmd);
    }
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    unsigned gl_shader(const char* name,unsigned stage) {
        std::string source;
#ifdef __EMSCRIPTEN__
        const char* ext=".gles";
#else
        const char* ext=".glsl";
#endif
        if(!load_resource_text((std::string(kShaderRoot)+name+ext).c_str(),source))return 0;
        const char* p=source.c_str();unsigned result=glCreateShader(stage);glShaderSource(result,1,&p,nullptr);glCompileShader(result);
        int good=0;glGetShaderiv(result,GL_COMPILE_STATUS,&good);
        if(!good){char log[4096]{};glGetShaderInfoLog(result,sizeof(log),nullptr,log);message=std::string("Postprocessing shader: ")+log;glDeleteShader(result);return 0;}
        return result;
    }
    unsigned gl_program(const char* vertex_name,const char* fragment_name) {
        unsigned v=gl_shader(vertex_name,GL_VERTEX_SHADER),f=gl_shader(fragment_name,GL_FRAGMENT_SHADER);
        if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
        unsigned p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
        int good=0;glGetProgramiv(p,GL_LINK_STATUS,&good);
        if(!good){char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);message=std::string("Postprocessing shader link: ")+log;glDeleteProgram(p);return 0;}
        unsigned block=glGetUniformBlockIndex(p,"Context");if(block!=GL_INVALID_INDEX)glUniformBlockBinding(p,block,0);
        return p;
    }
    bool init_gl() {
#ifdef __EMSCRIPTEN__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        renderer=SDL_CreateRenderer(window,"opengles2");
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        renderer=SDL_CreateRenderer(window,"opengl");
#endif
        if(!renderer)return false;
        gl_crt=gl_program("fullscreen_target.vert","crt.frag");
        gl_composite=gl_program("fullscreen.vert","composite.frag");
        if(!gl_crt||!gl_composite)return false;
        glGenVertexArrays(1,&gl_vao);glGenBuffers(1,&gl_ubo);glGenFramebuffers(1,&gl_fbo);
        glBindBuffer(GL_UNIFORM_BUFFER,gl_ubo);glBufferData(GL_UNIFORM_BUFFER,sizeof(Uniforms),nullptr,GL_DYNAMIC_DRAW);
        backend=Backend::GL;
#ifdef __EMSCRIPTEN__
        message="Postprocessing: WebGL2";
#else
        message="Postprocessing: OpenGL fallback";
#endif
        SDL_FlushRenderer(renderer);return true;
    }
    void gl_bind(unsigned program,const char* name,unsigned unit,unsigned texture) {
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);
        glUniform1i(glGetUniformLocation(program,name),static_cast<int>(unit));
    }
    bool gl_present(const Uniforms& u) {
        SDL_FlushRenderer(renderer);
        glBindVertexArray(gl_vao);glBindBuffer(GL_UNIFORM_BUFFER,gl_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(u),u.data());glBindBufferBase(GL_UNIFORM_BUFFER,0,gl_ubo);
        glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
        glViewport(0,0,width,height);
        glBindFramebuffer(GL_FRAMEBUFFER,gl_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,processed.gl,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(gl_crt);
        gl_bind(gl_crt,"Source",0,scene.gl);glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(gl_composite);
        gl_bind(gl_composite,"Processed",0,processed.gl);gl_bind(gl_composite,"UserInterface",1,ui.gl);
        glDrawArrays(GL_TRIANGLES,0,3);SDL_GL_SwapWindow(window);
        bool good=glGetError()==GL_NO_ERROR;
        // Invalidate SDL's cached state before its next pass.
        glActiveTexture(GL_TEXTURE0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);SDL_FlushRenderer(renderer);
        return good;
    }
#endif
};

PostProcessor::PostProcessor(SDL_Window* window):impl_(std::make_unique<Impl>(window,enabled_)){}
PostProcessor::~PostProcessor()=default;
SDL_Renderer* PostProcessor::renderer() const{return impl_->renderer;}
SDL_GPUDevice* PostProcessor::device() const{return impl_->gpu;}
bool PostProcessor::available() const{return impl_->backend!=Impl::Backend::Plain;}
const std::string& PostProcessor::status() const{return impl_->message;}
void PostProcessor::set_enabled(bool value){enabled_=value;}
void PostProcessor::release_renderer(){impl_->cleanup();}
bool PostProcessor::recreate(){
    auto* window=impl_->window;int vsync=impl_->vsync;
    impl_.reset();impl_=std::make_unique<Impl>(window,enabled_);set_vsync(vsync);
    // A plain renderer still lets the guest and host UI resume after a reset.
    return impl_->renderer != nullptr;
}
bool PostProcessor::set_vsync(int enabled){
    auto& p=*impl_;p.vsync=enabled;
    if(p.backend==Impl::Backend::Native){
        auto mode=SDL_GPU_PRESENTMODE_VSYNC;
        if(!enabled&&SDL_WindowSupportsGPUPresentMode(p.gpu,p.window,SDL_GPU_PRESENTMODE_IMMEDIATE))mode=SDL_GPU_PRESENTMODE_IMMEDIATE;
        return SDL_SetGPUSwapchainParameters(p.gpu,p.window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode);
    }
    return SDL_SetRenderVSync(p.renderer,enabled);
}
SDL_Texture* PostProcessor::scene_target() const{return impl_->scene.sdl;}
bool PostProcessor::begin_scene(int w,int h){
    auto& p=*impl_;p.ui_started=false;
    if(!available())return p.renderer!=nullptr;
    if(!p.resize(w,h))return false;
    if(!SDL_SetRenderTarget(p.renderer,p.scene.sdl))return false;
    return SDL_SetRenderScale(p.renderer,1,1);
}
void PostProcessor::begin_ui(const FrameView& frame){
    auto& p=*impl_;
    p.frame=frame;
    if(!available())return;
    if(SDL_SetRenderTarget(p.renderer,p.ui.sdl)){
        SDL_SetRenderScale(p.renderer,1,1);
        SDL_SetRenderDrawColor(p.renderer,0,0,0,0);SDL_RenderClear(p.renderer);
        SDL_SetRenderDrawColor(p.renderer,0,0,0,255);p.ui_started=true;
    }
}
bool PostProcessor::present(){
    auto& p=*impl_;
#ifdef __EMSCRIPTEN__
    // The loss event may be queued after the browser has invalidated GL.
    // This is an expected pause; resource rebuilding follows its callback.
    if(emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context()))return false;
#endif
    if(!available())return SDL_RenderPresent(p.renderer);
    if(!p.scene.sdl)return false;
    // During startup there may not be a display handler or an OSD frame yet.
    if(!p.ui_started)begin_ui(p.frame);
    auto u=p.uniforms();bool okay=false;
    if(p.backend==Impl::Backend::Native)okay=p.native_present(u);
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    else if(p.backend==Impl::Backend::GL)okay=p.gl_present(u);
#endif
    if(okay)p.completed=true;
    else {
        auto error="Postprocessing presentation failed: "+std::string(SDL_GetError());
        if(p.message!=error) std::fprintf(stderr,"%s\n",error.c_str());
        p.message=std::move(error);
    }
    return okay;
}
SDL_Surface* PostProcessor::capture_processed(){
    auto& p=*impl_;if(!available()||!p.completed)return nullptr;
    auto* previous=SDL_GetRenderTarget(p.renderer);
    SDL_SetRenderTarget(p.renderer,p.processed.sdl);
    auto* result=SDL_RenderReadPixels(p.renderer,nullptr);
    SDL_SetRenderTarget(p.renderer,previous);return result;
}
} // namespace gs2::postprocess
