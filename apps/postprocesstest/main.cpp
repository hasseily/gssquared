#include "display/postprocess/PostProcessor.hpp"
#include "gs2.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

gs2_app_t gs2_app_values;
using gs2::postprocess::PostProcessor;
using gs2::postprocess::FrameView;

static bool check(PostProcessor& processor,int width,int height,bool enabled) {
    auto* renderer=processor.renderer();
    std::vector<unsigned char> pixels(width*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        auto i=(y*width+x)*4;
        pixels[i]=32+x*128/width;pixels[i+1]=24+y*160/height;
        pixels[i+2]=40+(x+y)%96;pixels[i+3]=255;
    }
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,width,height);
    if(!texture)return false;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    SDL_UpdateTexture(texture,nullptr,pixels.data(),width*4);
    processor.set_enabled(enabled);
    bool okay=processor.begin_scene(width,height);
    SDL_RenderTexture(renderer,texture,nullptr,nullptr);
    FrameView frame;frame.source_width=width;frame.source_height=height;
    processor.begin_ui(frame);
    // The host UI must never appear in a captured guest image.
    SDL_SetRenderDrawColor(renderer,255,0,255,255);SDL_RenderClear(renderer);
    okay=processor.present()&&okay;
    auto* raw=processor.capture_processed();
    auto* result=raw?SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32):nullptr;
    if(raw)SDL_DestroySurface(raw);
    if(!result||result->w!=width||result->h!=height)okay=false;
    int worst=0;
    if(result)for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto* actual=static_cast<unsigned char*>(result->pixels)+y*result->pitch+x*4;
        float multiplier=1;
        if(enabled){
            float u=(x+.5f)/width,v=(y+.5f)/height;
            float dx=u-.5f,dy=v-.5f,warp=(dx*dx+dy*dy)*.2f;
            u+=dx*warp;v+=dy*warp;
            float scan=.75f+.25f*(std::sin(v*2*height*3.14159265359f)*.5f+.5f);
            float grille=std::fmod(u*width,3.f)<1.5f?.995f:1.005f;
            float vignette=.93f+.07f*u*(1-u)*v*(1-v)*15;
            multiplier=scan*grille*vignette*1.2f;
        }
        for(int c=0;c<3;++c){
            int expected=std::clamp(int(std::lround(pixels[(y*width+x)*4+c]*multiplier)),0,255);
            worst=std::max(worst,std::abs(int(actual[c])-expected));
        }
    }
    if(result)SDL_DestroySurface(result);
    SDL_DestroyTexture(texture);
    // The CRT uses trigonometric floats; allow at most two 8-bit levels.
    okay=okay&&worst<=2;
    std::printf("%dx%d CRT %s: %s (maximum channel error %d)\n",width,height,enabled?"on":"off",okay?"PASS":"FAIL",worst);
    return okay;
}

int main(int argc,char** argv){
    gs2_app_values.base_path=argc>1?argv[1]:"resources";
    if(!gs2_app_values.base_path.empty()&&gs2_app_values.base_path.back()!='/')gs2_app_values.base_path+='/';
    bool strict=false,require_gl=false;
    for(int i=2;i<argc;++i){strict|=std::strcmp(argv[i],"--require-gpu")==0;require_gl|=std::strcmp(argv[i],"--require-opengl")==0;}
    if(!SDL_Init(SDL_INIT_VIDEO))return strict||require_gl?1:77;
    auto* window=SDL_CreateWindow("Renderer regression",320,240,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
    if(!window){SDL_Quit();return strict||require_gl?1:77;}
    bool okay=true,skipped=false;
    {
        PostProcessor processor(window);
        std::printf("%s\n",processor.status().c_str());
        if(!processor.available()){
            skipped=true;
        }else{
        if(strict&&!processor.device())okay=false;
        if(require_gl&&processor.device())okay=false;
        processor.set_vsync(0);
        okay=check(processor,320,240,false)&&okay;
        okay=check(processor,320,240,true)&&okay;
        okay=check(processor,257,193,false)&&okay;
        okay=processor.recreate()&&okay;
        okay=check(processor,320,240,true)&&okay;
        }
    }
    SDL_DestroyWindow(window);SDL_Quit();
    return skipped?(strict||require_gl?1:77):okay?0:1;
}
