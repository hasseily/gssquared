#include "PostProcessor.hpp"
#include "display/shaders/GpuShaderLoader.hpp"
#include "gs2.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
using Uniforms = std::array<std::array<float,4>,18>;
constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr const char* kShaderRoot = "shaders/postprocess/";
float finite(float value, float fallback=0.0f) { return std::isfinite(value) ? value : fallback; }
float clamp(float v,float a,float b) { return std::clamp(finite(v,a),a,b); }
std::string asset_path(const std::string& name) {
    if (name.empty() || name=="NONE") return {};
    const std::filesystem::path path(name);
    if (path.is_absolute()) return path.string();
    for (const auto& sub : {std::string("postprocess/bezels/"),std::string("bezels/"),std::string()}) {
        auto candidate=std::filesystem::path(gs2_app_values.base_path)/sub/path;
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
    return (std::filesystem::path(gs2_app_values.base_path)/"postprocess/bezels"/path).string();
}
}

struct PostProcessor::Impl {
    enum class Backend { Plain, Native, GL } backend=Backend::Plain;
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_GPUDevice* gpu=nullptr;
    SDL_GPUGraphicsPipeline* crt_pipeline=nullptr;
    SDL_GPUGraphicsPipeline* composite_pipeline=nullptr;
    SDL_GPUSampler* linear_sampler=nullptr;
    SDL_GPUSampler* source_sampler=nullptr;
    SDL_GPUSampler* nearest_sampler=nullptr;
    struct Texture { SDL_GPUTexture* gpu=nullptr; SDL_Texture* sdl=nullptr; unsigned gl=0; };
    Texture scene,ui,history[2],bezel,glass;
    int width=0,height=0,scene_width=0,scene_height=0,write_index=0,last_completed=0;
    uint64_t frame_count=0,merge_count=0,last_identity=0;
    bool history_valid=false,merge_was_active=false,ui_started=false;
    int vsync=1;
    Settings& settings;
    FrameView frame;
    std::string message="Postprocessing is unavailable";
    std::string requested_bezel,requested_glass,loaded_bezel,loaded_glass;
    std::string attempted_bezel,attempted_glass,bezel_error,glass_error,backend_message;
    bool assets_dirty=true;
    bool explicit_assets=false;
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    unsigned gl_crt=0,gl_composite=0,gl_vao=0,gl_ubo=0,gl_fbo=0;
#endif

    explicit Impl(SDL_Window* w, Settings& s):window(w),settings(s) {
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
        free_texture(scene);free_texture(ui);free_texture(history[0]);free_texture(history[1]);
        width=height=0;history_valid=false;
    }
    void cleanup() {
        cleanup_targets();free_texture(bezel);free_texture(glass);
        if (crt_pipeline && gpu) SDL_ReleaseGPUGraphicsPipeline(gpu,crt_pipeline);
        if (composite_pipeline && gpu) SDL_ReleaseGPUGraphicsPipeline(gpu,composite_pipeline);
        if (linear_sampler && gpu) SDL_ReleaseGPUSampler(gpu,linear_sampler);
        if (source_sampler && gpu) SDL_ReleaseGPUSampler(gpu,source_sampler);
        if (nearest_sampler && gpu) SDL_ReleaseGPUSampler(gpu,nearest_sampler);
        crt_pipeline=composite_pipeline=nullptr;linear_sampler=source_sampler=nearest_sampler=nullptr;
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
    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUShader* vertex,SDL_GPUShader* frag,SDL_GPUTextureFormat format,bool blend=false) {
        SDL_GPUColorTargetDescription target{};target.format=format;
        if(blend){
            // The source renderer blends the CRT over opaque black before
            // copying its history, including rounded-corner alpha.
            target.blend_state.enable_blend=true;
            target.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            target.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            target.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;
            target.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            target.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            target.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        }
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
        auto* crt=shader("crt.frag",SDL_GPU_SHADERSTAGE_FRAGMENT,2,1);
        auto* compose=shader("composite.frag",SDL_GPU_SHADERSTAGE_FRAGMENT,5,1);
        if(vertex&&crt&&compose){
            crt_pipeline=pipeline(vertex,crt,kFormat,true);
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
        sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
        source_sampler=SDL_CreateGPUSampler(gpu,&sampler);
        sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        nearest_sampler=SDL_CreateGPUSampler(gpu,&sampler);
        if(!linear_sampler||!source_sampler||!nearest_sampler)return false;
        backend=Backend::Native;
        message=backend_message="Postprocessing: SDL GPU / "+std::string(SDL_GetGPUDeviceDriver(gpu));
        return true;
    }
    bool make_texture(Texture& t,int w,int h,bool mipmaps) {
        if(backend==Backend::Native){
            SDL_GPUTextureCreateInfo info{};
            info.type=SDL_GPU_TEXTURETYPE_2D;info.format=kFormat;
            info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            info.width=w;info.height=h;info.layer_count_or_depth=1;
            info.num_levels=mipmaps?1+static_cast<unsigned>(std::floor(std::log2(std::max(w,h)))):1;
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
        // Keep firmware glyphs at their native canvas resolution until the final CRT pass.
        scene_width=std::max(w,1280);scene_height=std::max(h,800);
        if(!make_texture(scene,scene_width,scene_height,true)||!make_texture(ui,w,h,false)||
            !make_texture(history[0],w,h,false)||!make_texture(history[1],w,h,false)){
            message="Postprocessing: could not allocate render targets: "+std::string(SDL_GetError());
            cleanup_targets();return false;
        }
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        if(backend==Backend::GL){
            glActiveTexture(GL_TEXTURE0);
            for(auto& t:history){
                glBindTexture(GL_TEXTURE_2D,t.gl);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            }
        }
#endif
        width=w;height=h;write_index=last_completed=0;merge_count=0;
        // Defined initial history is also required by GPU validation even when
        // shader branches skip sampling history on the first frame.
        for(auto& t:history){SDL_SetRenderTarget(renderer,t.sdl);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);}
        SDL_SetRenderTarget(renderer,scene.sdl);
        return true;
    }
    bool upload_image(Texture& t,const std::string& path) {
        free_texture(t);
        SDL_Surface* raw=path.empty()?SDL_CreateSurface(1,1,SDL_PIXELFORMAT_RGBA32):IMG_Load(path.c_str());
        if(!raw)return false;
        SDL_Surface* surface=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);
        if(!surface)return false;
        if(path.empty())std::memset(surface->pixels,0,surface->pitch*surface->h);
        bool okay=make_texture(t,surface->w,surface->h,false);
        if(okay)okay=SDL_UpdateTexture(t.sdl,nullptr,surface->pixels,surface->pitch);
        SDL_DestroySurface(surface);return okay;
    }
    void assets() {
        std::string bp=explicit_assets?requested_bezel:asset_path(settings.bezelName);
        std::string gp=explicit_assets?requested_glass:asset_path(settings.glassName);
        bool attempted=false;
        if(assets_dirty||!bezel.sdl||attempted_bezel!=bp){
            attempted=true;attempted_bezel=bp;
            if(!upload_image(bezel,bp)){bezel_error="Postprocessing bezel could not be loaded: "+bp;upload_image(bezel,{});loaded_bezel.clear();}
            else {loaded_bezel=bp;bezel_error.clear();}
        }
        if(assets_dirty||!glass.sdl||attempted_glass!=gp){
            attempted=true;attempted_glass=gp;
            if(!upload_image(glass,gp)){glass_error="Postprocessing glass could not be loaded: "+gp;upload_image(glass,{});loaded_glass.clear();}
            else {loaded_glass=gp;glass_error.clear();}
        }
        assets_dirty=false;
        if(attempted)message=!bezel_error.empty()?bezel_error:!glass_error.empty()?glass_error:backend_message;
    }
    SDL_FRect output_rect(bool apply_zoom=true) const {
        const auto& s=settings;
        float ow=width*frame.source_region.w,oh=height*frame.source_region.h;
        if(s.bCRTFillWindow&&s.p_i_postprocessingLevel>1){ow=width;oh=height;}
        if(!s.bAutoScale){
            // Decoders can have non-square pixels (560x192 is displayed near
            // 4:3), so integer horizontal scale must retain the scene's aspect.
            float aspect=ow/std::max(1.0f,oh);
            ow=std::max(1,frame.source_width)*std::max(1,s.integer_scale);
            oh=ow/aspect;
        }
        if(apply_zoom){
            ow=std::max(1.0f,ow*clamp(s.p_v_zoom[0],.01f,20));
            oh=std::max(1.0f,oh*clamp(s.p_v_zoom[1],.01f,20));
        }
        return {width*.5f-ow*.5f+ow*finite(s.p_v_center[0])/200,
                height*.5f-oh*.5f-oh*finite(s.p_v_center[1])/200,ow,oh};
    }
    Uniforms uniforms() {
        auto& s=settings;Uniforms u{};
        float iw=std::max(1,frame.sample_width>0?frame.sample_width:frame.source_width);
        float ih=std::max(1,frame.sample_height>0?frame.sample_height:frame.source_height);
        // Geometry is expressed in output pixels; source and logical scanlines
        // remain distinct for double-height fields and sharp guest text overlays.
        const auto rect=output_rect(),unzoomed=output_rect(false);
        float ow=unzoomed.w,oh=unzoomed.h;
        u[0]={iw,ih,ow,oh};u[1]={float(std::max(1,frame.scanlines)),float(frame_count%10000000),float(merge_count%10000000),float(std::clamp(s.p_i_postprocessingLevel,0,2))};
        u[2]={clamp(s.p_f_ghostingPercent,0,99.99f),clamp(s.p_f_phosphorBlur,0,2),float(s.p_b_phosphorGlow),float(s.p_b_useOKlab)};
        u[3]={float(s.p_b_smoothCorner),float(s.p_b_slot),clamp(s.p_f_barrelDistortion,-.3f,5),clamp(s.p_f_bgr,0,1)};
        u[4]={clamp(s.p_f_black,-1,.999f),clamp(s.p_f_brDep,0,.5f),clamp(s.p_f_brightness,0,100),clamp(s.p_f_contrast,0,100)};
        u[5]={clamp(s.p_f_convB,-3,3),clamp(s.p_f_convG,-3,3),clamp(s.p_f_convR,-3,3),clamp(s.p_f_corner,0,100)/10000.0f};
        u[6]={clamp(s.p_f_cStr,0,.5f),finite(s.p_f_hue),finite(s.p_f_hueGB),finite(s.p_f_hueRB)};
        u[7]={finite(s.p_f_hueRG),clamp(s.p_f_maskHigh,0,1),clamp(s.p_f_maskLow,0,1),clamp(s.p_f_maskSize,.001f,20)};
        u[8]={clamp(s.p_f_saturation,0,100),clamp(s.p_f_scanlineWeight,0,2),clamp(s.p_f_scanSpeed,0,2),clamp(s.p_f_filmGrain,0,1)};
        u[9]={clamp(s.p_f_interlace,0,2),clamp(s.p_f_slotW,2,3),clamp(s.p_f_vignetteWeight,0,5),float(std::clamp(s.p_i_cSpace,0,3))};
        u[10]={float(std::clamp(s.p_i_maskType,0,2)),float(std::clamp(s.p_i_scanlineType,0,2)),clamp(s.p_v_warp[0],-.5f,.5f),clamp(s.p_v_warp[1],-.5f,.5f)};
        u[11]={frame.source_region.x,frame.source_region.y,frame.source_region.w,frame.source_region.h};
        u[12]={rect.x/width,rect.y/height,rect.w/width,rect.h/height};
        u[13]={clamp(s.p_f_bezelReflection,0,1),clamp(s.p_f_reflectionBlur,0,12),clamp(s.p_f_glassThickness,0,10),float(s.p_b_outlineQuad)};
        u[14]={finite(s.p_v_reflectionScale[0],1),finite(s.p_v_reflectionScale[1],1),finite(s.p_v_reflectionTranslation[0]),finite(s.p_v_reflectionTranslation[1])};
        u[15]={clamp(s.bezelWidth,.01f,20),clamp(s.bezelHeight,.01f,20),finite(s.bezelCenterX)/200,-finite(s.bezelCenterY)/200};
        bool merge=s.bHalveFramerate&&!frame.fields_already_composed; // Do not combine Appletini fields a second time.
        if(merge!=merge_was_active){merge_count=0;history_valid=false;merge_was_active=merge;u[1][2]=0;}
        u[16]={float(history_valid),float(merge),float(frame.seconds),0};
        float upsample=std::max(scene_width*frame.source_region.w/iw,scene_height*frame.source_region.h/ih);
        u[17]={std::max(0.0f,std::log2(std::max(upsample,1.0f))),float(!loaded_bezel.empty()),float(!loaded_glass.empty()),0};
        return u;
    }
    void gpu_draw(SDL_GPUCommandBuffer* commands,SDL_GPUTexture* destination,SDL_GPUGraphicsPipeline* p,
        const SDL_GPUTextureSamplerBinding* bindings,unsigned n,const Uniforms& u) {
        SDL_GPUColorTargetInfo target{};target.texture=destination;target.load_op=SDL_GPU_LOADOP_CLEAR;
        target.store_op=SDL_GPU_STOREOP_STORE;target.clear_color={0,0,0,1};
        auto* pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);
        SDL_BindGPUGraphicsPipeline(pass,p);
        SDL_BindGPUFragmentSamplers(pass,0,bindings,n);
        SDL_PushGPUFragmentUniformData(commands,0,u.data(),sizeof(u));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    }
    bool native_present(const Uniforms& u,bool skip) {
        if(!SDL_RenderPresent(renderer))return false; // offscreen renderer submits scene and UI work
        auto* cmd=SDL_AcquireGPUCommandBuffer(gpu);if(!cmd)return false;
        // Implicit minification and explicit phosphor/reflection LOD both use
        // this chain; every level must be defined even with blur disabled.
        SDL_GenerateMipmapsForGPUTexture(cmd,scene.gpu);
        SDL_GPUTextureSamplerBinding input[2]={{scene.gpu,source_sampler},{history[1-write_index].gpu,nearest_sampler}};
        gpu_draw(cmd,history[write_index].gpu,crt_pipeline,input,2,u);
        if(!skip){
            SDL_GPUTexture* swap=nullptr;Uint32 sw=0,sh=0;
            if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,window,&swap,&sw,&sh)){SDL_CancelGPUCommandBuffer(cmd);return false;}
            if(swap){
                SDL_GPUTextureSamplerBinding inputs[5]={{history[write_index].gpu,nearest_sampler},{scene.gpu,source_sampler},
                    {bezel.gpu,linear_sampler},{glass.gpu,linear_sampler},{ui.gpu,linear_sampler}};
                gpu_draw(cmd,swap,composite_pipeline,inputs,5,u);
            }
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
        message=backend_message="Postprocessing: WebGL2";
#else
        message=backend_message="Postprocessing: OpenGL fallback";
#endif
        SDL_FlushRenderer(renderer);return true;
    }
    void gl_bind(unsigned program,const char* name,unsigned unit,unsigned texture) {
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);
        glUniform1i(glGetUniformLocation(program,name),static_cast<int>(unit));
    }
    bool gl_present(const Uniforms& u,bool skip) {
        SDL_FlushRenderer(renderer);
        glBindVertexArray(gl_vao);glBindBuffer(GL_UNIFORM_BUFFER,gl_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(u),u.data());glBindBufferBase(GL_UNIFORM_BUFFER,0,gl_ubo);
        glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
        glViewport(0,0,width,height);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene.gl);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,gl_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,history[write_index].gl,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(gl_crt);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        gl_bind(gl_crt,"A2TextureCurrent",0,scene.gl);gl_bind(gl_crt,"PreviousFrame",1,history[1-write_index].gl);
        glDrawArrays(GL_TRIANGLES,0,3);
        glDisable(GL_BLEND);
        if(!skip){
            glBindFramebuffer(GL_FRAMEBUFFER,0);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(gl_composite);
            gl_bind(gl_composite,"Processed",0,history[write_index].gl);gl_bind(gl_composite,"Source",1,scene.gl);
            gl_bind(gl_composite,"Bezel",2,bezel.gl);gl_bind(gl_composite,"Glass",3,glass.gl);gl_bind(gl_composite,"UserInterface",4,ui.gl);
            glDrawArrays(GL_TRIANGLES,0,3);SDL_GL_SwapWindow(window);
        }
        bool good=glGetError()==GL_NO_ERROR;
        // Invalidate SDL's cached state before its next pass.
        glActiveTexture(GL_TEXTURE0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);SDL_FlushRenderer(renderer);
        return good;
    }
#endif
};

PostProcessor::PostProcessor(SDL_Window* window):impl_(std::make_unique<Impl>(window,settings_)){}
PostProcessor::~PostProcessor()=default;
SDL_Renderer* PostProcessor::renderer() const{return impl_->renderer;}
SDL_GPUDevice* PostProcessor::device() const{return impl_->gpu;}
bool PostProcessor::available() const{return impl_->backend!=Impl::Backend::Plain;}
const std::string& PostProcessor::status() const{return impl_->message;}
Settings& PostProcessor::settings(){return impl_->settings;}
const Settings& PostProcessor::settings() const{return impl_->settings;}
void PostProcessor::settings_changed(){impl_->history_valid=false;impl_->merge_count=0;}
void PostProcessor::reset_history(){impl_->history_valid=false;impl_->merge_count=0;}
void PostProcessor::set_assets(const std::string& b,const std::string& g){
    impl_->explicit_assets=true;impl_->requested_bezel=b;impl_->requested_glass=g;
    // An explicit reload can retry the same file after it has been repaired.
    // Failed loads otherwise retain their transparent placeholder across frames.
    impl_->assets_dirty=true;
}
void PostProcessor::release_renderer(){impl_->cleanup();}
bool PostProcessor::recreate(){
    auto bezel=impl_->requested_bezel,glass=impl_->requested_glass;
    bool explicit_assets=impl_->explicit_assets;auto* window=impl_->window;int vsync=impl_->vsync;
    impl_.reset();impl_=std::make_unique<Impl>(window,settings_);
    impl_->requested_bezel=std::move(bezel);
    impl_->requested_glass=std::move(glass);impl_->explicit_assets=explicit_assets;set_vsync(vsync);
    // A plain SDL renderer still lets the guest and host UI resume after a
    // context reset. Effects availability is reported separately by status().
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
bool PostProcessor::map_output_to_scene(float& x,float& y) const {
    const auto& p=*impl_;if(!available()||p.width<=0||p.height<=0)return true;
    auto rect=p.output_rect();
    float nx=(x-rect.x)/rect.w,ny=(y-rect.y)/rect.h;
    if(p.settings.p_i_postprocessingLevel>1){
        float ax=nx*2-1,ay=ny*2-1;
        nx=(ax*(1+ay*ay*clamp(p.settings.p_v_warp[0],-.5f,.5f)))*.5f+.5f;
        ny=(ay*(1+ax*ax*clamp(p.settings.p_v_warp[1],-.5f,.5f)))*.5f+.5f;
        float dx=nx-.5f,dy=ny-.5f,d2=dx*dx+dy*dy;
        float barrel=clamp(p.settings.p_f_barrelDistortion,-.3f,5);
        nx=(dx*(1+d2*d2*barrel))/(1+.04f*barrel)+.5f;
        ny=(dy*(1+d2*d2*barrel))/(1+.04f*barrel)+.5f;
    }
    x=(p.frame.source_region.x+nx*p.frame.source_region.w)*p.width;
    y=(p.frame.source_region.y+ny*p.frame.source_region.h)*p.height;
    return nx>=0&&nx<=1&&ny>=0&&ny<=1;
}
SDL_Texture* PostProcessor::scene_target() const{return impl_->scene.sdl;}
bool PostProcessor::begin_scene(int w,int h){
    auto& p=*impl_;p.ui_started=false;
    if(!available())return p.renderer!=nullptr;
    if(!p.resize(w,h))return false;
    p.assets();
    if(!SDL_SetRenderTarget(p.renderer,p.scene.sdl))return false;
    return SDL_SetRenderScale(p.renderer,float(p.scene_width)/w,float(p.scene_height)/h);
}
void PostProcessor::begin_ui(const FrameView& frame){
    auto& p=*impl_;
    if(frame.source_width!=p.frame.source_width||frame.source_height!=p.frame.source_height||
       frame.source_region.x!=p.frame.source_region.x||frame.source_region.y!=p.frame.source_region.y||
       frame.source_region.w!=p.frame.source_region.w||frame.source_region.h!=p.frame.source_region.h||
       frame.sample_width!=p.frame.sample_width||frame.sample_height!=p.frame.sample_height||
       frame.scanlines!=p.frame.scanlines||frame.fields_already_composed!=p.frame.fields_already_composed) p.history_valid=false;
    p.frame=frame;
    if(!available())return;
    if(frame.source_width<=0||frame.source_height<=0)return;
    if(frame.identity<p.last_identity)p.history_valid=false;
    p.last_identity=frame.identity;
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
    auto u=p.uniforms();bool merge=p.settings.bHalveFramerate&&!p.frame.fields_already_composed;
    bool skip=merge&&((p.merge_count&1)==0);
    bool okay=false;
    if(p.backend==Impl::Backend::Native)okay=p.native_present(u,skip);
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    else if(p.backend==Impl::Backend::GL)okay=p.gl_present(u,skip);
#endif
    if(okay){p.history_valid=true;p.last_completed=p.write_index;p.write_index=1-p.write_index;++p.frame_count;if(merge)++p.merge_count;}
    else {
        auto error="Postprocessing presentation failed: "+std::string(SDL_GetError());
        if(p.message!=error) std::fprintf(stderr,"%s\n",error.c_str());
        p.message=std::move(error);
    }
    return okay;
}
SDL_Surface* PostProcessor::capture_processed(){
    auto& p=*impl_;if(!available()||!p.history_valid)return nullptr;
    auto* previous=SDL_GetRenderTarget(p.renderer);
    SDL_FlushRenderer(p.renderer);
    Impl::Texture target,transparent;
    if(!p.make_texture(target,p.width,p.height,false)||!p.upload_image(transparent,{})){
        p.free_texture(target);p.free_texture(transparent);return nullptr;
    }
    // Image uploads belong to SDL's submission before the capture pass samples them.
    if(p.backend==Impl::Backend::Native)SDL_RenderPresent(p.renderer);
    else SDL_FlushRenderer(p.renderer);
    auto u=p.uniforms();bool okay=false;
    if(p.backend==Impl::Backend::Native){
        auto* vertex=p.shader("fullscreen.vert",SDL_GPU_SHADERSTAGE_VERTEX,0,0);
        auto* fragment=p.shader("composite.frag",SDL_GPU_SHADERSTAGE_FRAGMENT,5,1);
        auto* pipeline=(vertex&&fragment)?p.pipeline(vertex,fragment,kFormat):nullptr;
        if(vertex)SDL_ReleaseGPUShader(p.gpu,vertex);if(fragment)SDL_ReleaseGPUShader(p.gpu,fragment);
        if(pipeline){
            auto* cmd=SDL_AcquireGPUCommandBuffer(p.gpu);
            if(cmd){
                SDL_GPUTextureSamplerBinding inputs[5]={{p.history[p.last_completed].gpu,p.nearest_sampler},{p.scene.gpu,p.source_sampler},
                    {p.bezel.gpu,p.linear_sampler},{p.glass.gpu,p.linear_sampler},{transparent.gpu,p.linear_sampler}};
                p.gpu_draw(cmd,target.gpu,pipeline,inputs,5,u);okay=SDL_SubmitGPUCommandBuffer(cmd);
            }
            SDL_ReleaseGPUGraphicsPipeline(p.gpu,pipeline);
        }
    }
#if defined(__EMSCRIPTEN__) || defined(__linux__)
    else if(p.backend==Impl::Backend::GL){
        auto program=p.gl_program("fullscreen_target.vert","composite.frag");
        if(program){
            glBindVertexArray(p.gl_vao);glBindBuffer(GL_UNIFORM_BUFFER,p.gl_ubo);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(u),u.data());glBindBufferBase(GL_UNIFORM_BUFFER,0,p.gl_ubo);
            glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            glViewport(0,0,p.width,p.height);glBindFramebuffer(GL_FRAMEBUFFER,p.gl_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.gl,0);
            glUseProgram(program);
            p.gl_bind(program,"Processed",0,p.history[p.last_completed].gl);p.gl_bind(program,"Source",1,p.scene.gl);
            p.gl_bind(program,"Bezel",2,p.bezel.gl);p.gl_bind(program,"Glass",3,p.glass.gl);p.gl_bind(program,"UserInterface",4,transparent.gl);
            glDrawArrays(GL_TRIANGLES,0,3);okay=glGetError()==GL_NO_ERROR;
            glActiveTexture(GL_TEXTURE0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteProgram(program);SDL_FlushRenderer(p.renderer);
        }
    }
#endif
    SDL_Surface* result=nullptr;
    if(okay&&SDL_SetRenderTarget(p.renderer,target.sdl))result=SDL_RenderReadPixels(p.renderer,nullptr);
    SDL_SetRenderTarget(p.renderer,previous);p.free_texture(target);p.free_texture(transparent);
    return result;
}
SDL_Surface* PostProcessor::capture_crt(){
    auto& p=*impl_;if(!available()||!p.history_valid)return nullptr;
    auto* previous=SDL_GetRenderTarget(p.renderer);
    SDL_SetRenderTarget(p.renderer,p.history[p.last_completed].sdl);
    auto* result=SDL_RenderReadPixels(p.renderer,nullptr);
    SDL_SetRenderTarget(p.renderer,previous);return result;
}
} // namespace gs2::postprocess
