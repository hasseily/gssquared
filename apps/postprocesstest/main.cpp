#include "display/postprocess/PostProcessor.hpp"
#include "gs2.hpp"
#include "display/postprocess/PostProcessPreset.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <vector>

gs2_app_t gs2_app_values;
namespace pp=gs2::postprocess;
static void require(bool v,const char* message) {
    if(!v){std::fprintf(stderr,"FAIL: %s: %s\n",message,SDL_GetError());std::exit(1);}
}
static std::vector<unsigned char> bytes(SDL_Surface* s) {
    require(s,"processed frame readback");
    auto* rgba=SDL_ConvertSurface(s,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(s);
    require(rgba,"RGBA32 conversion");
    std::vector<unsigned char> b(rgba->w*rgba->h*4);
    for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,b.data()+y*rgba->w*4);
    SDL_DestroySurface(rgba);return b;
}
int main(int argc,char** argv) {
    bool strict=false,benchmark=false,require_gl=false;
    for(int i=1;i<argc;++i){strict|=std::string(argv[i])=="--require-gpu";benchmark|=std::string(argv[i])=="--benchmark";require_gl|=std::string(argv[i])=="--require-opengl";}
    strict|=require_gl;
    if(!SDL_Init(SDL_INIT_VIDEO)){std::fprintf(stderr,"SKIP: %s\n",SDL_GetError());return strict?1:77;}
    std::filesystem::path executable=std::filesystem::absolute(argv[0]).parent_path();
    gs2_app_values.base_path=(executable/"resources").string();
    if(const char* path=SDL_getenv("GS2_TEST_RESOURCE_PATH"))gs2_app_values.base_path=path;
    if(gs2_app_values.base_path.back()!='/')gs2_app_values.base_path+='/';
    auto* window=SDL_CreateWindow("GSSquared postprocessor regression",512,384,SDL_WINDOW_HIDDEN);
    if(!window){std::fprintf(stderr,"SKIP: %s\n",SDL_GetError());return strict?1:77;}
    {
        pp::PostProcessor processor(window);std::puts(processor.status().c_str());
        if(!processor.available())return strict?1:77;
        if(require_gl)require(processor.device()==nullptr,"OpenGL fallback must be selected");
        auto* r=processor.renderer();
        pp::FrameView frame;frame.source_width=560;frame.source_height=192;frame.scanlines=192;frame.fields_already_composed=false;
        auto draw=[&](bool lit=true){
            require(processor.begin_scene(512,384),"begin scene");
            SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
            if(lit){
                SDL_FRect q[4]={{0,0,256,192},{256,0,256,192},{0,192,256,192},{256,192,256,192}};
                const unsigned char c[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
                for(int i=0;i<4;++i){SDL_SetRenderDrawColor(r,c[i][0],c[i][1],c[i][2],255);SDL_RenderFillRect(r,&q[i]);}
            }
            ++frame.identity;frame.seconds=0;processor.begin_ui(frame);
            require(processor.present(),"present");
            return bytes(processor.capture_crt());
        };
        auto plain=draw();
        auto channel=[&](int x,int y,int c){return plain[(y*512+x)*4+c];};
        require(channel(32,32,0)>250&&channel(32,32,1)<3,"red top left, no vertical inversion");
        require(channel(400,32,1)>250&&channel(400,32,0)<3,"green top right");
        require(channel(32,300,2)>250&&channel(32,300,0)<3,"blue bottom left");
        auto& settings=processor.settings();settings.p_i_postprocessingLevel=2;settings.p_f_scanlineWeight=0;
        settings.p_f_ghostingPercent=80;processor.settings_changed();draw();auto ghost=draw(false);
        require(ghost[(32*512+32)*4]>80,"phosphor history survives a dark frame");
        processor.reset_history();auto reset=draw(false);
        require(reset[(32*512+32)*4]<3,"history reset removes stale image");
        // A settings change and a resize must never sample uninitialized history.
        settings.p_b_useOKlab=true;settings.p_f_hue=0.35f;settings.p_f_hueRB=-0.4f;settings.p_f_hueGB=0.2f;
        settings.p_f_saturation=1.4f;settings.p_f_phosphorBlur=1;settings.p_b_phosphorGlow=true;
        settings.p_b_slot=true;settings.p_i_maskType=2;settings.p_f_vignetteWeight=1;
        settings.p_f_scanlineWeight=.8f;settings.p_v_warp={.03f,.04f};settings.p_f_barrelDistortion=.05f;
        settings.p_f_filmGrain=.1f;settings.p_f_convR=.5f;settings.p_f_convB=-.5f;
        processor.settings_changed();auto complex=draw();
        require(complex!=plain,"full effects modify the decoded frame");
        size_t light=0;for(size_t i=0;i<complex.size();i+=4)light+=complex[i]+complex[i+1]+complex[i+2];
        require(light>100000,"full effects preserve visible picture");
        if(const char* out=SDL_getenv("GS2_TEST_ARTIFACT_DIR")){
            std::filesystem::create_directories(out);
            auto* surface=processor.capture_crt();require(surface,"fixture readback");
            require(SDL_SaveBMP(surface,(std::filesystem::path(out)/"postprocessing.bmp").string().c_str()),"save fixture");SDL_DestroySurface(surface);
        }
        // Firmware-composed fields must bypass the explicit second merge too.
        settings=pp::Settings{};settings.bHalveFramerate=true;frame.fields_already_composed=true;
        processor.settings_changed();draw();auto dark=draw(false);
        require(dark[(32*512+32)*4]<3,"already-composed Appletini fields are never averaged twice");
        // Imported SuperDuperDisplay presets exercise complete CRT + bezel/reflection/glass
        // composition. The readback uses a capture-only render pass and omits UI.
        for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::path(gs2_app_values.base_path)/"postprocess/presets")){
            if(entry.path().extension()!=".json")continue;
            std::string error;require(pp::load_preset(entry.path().string(),settings,error),error.c_str());
            settings.p_i_postprocessingLevel=2;
            auto bp=std::filesystem::path(gs2_app_values.base_path)/"postprocess/bezels"/settings.bezelName;
            auto gp=bp.parent_path()/(bp.stem().string()+".glass"+bp.extension().string());
            processor.set_assets(settings.bezelName=="NONE"?"":bp.string(),std::filesystem::exists(gp)?gp.string():"");
            processor.settings_changed();auto preset_crt=draw();
            unsigned brightest=0;for(size_t i=0;i<preset_crt.size();i+=4) brightest=std::max(brightest,unsigned(preset_crt[i])+preset_crt[i+1]+preset_crt[i+2]);
            require(brightest>30,"each SuperDuperDisplay preset retains a visible CRT picture");
            auto* composed=processor.capture_processed();require(composed,"final bezel/glass composition readback");
            require(composed->w==512&&composed->h==384,"final capture preserves output dimensions");
            if(const char* out=SDL_getenv("GS2_TEST_ARTIFACT_DIR")){
                require(SDL_SaveBMP(composed,(std::filesystem::path(out)/(entry.path().stem().string()+".bmp")).string().c_str()),"preset fixture save");
            }
            auto final=bytes(composed);
            require(final!=plain,"preset final composition changes image");
        }
        // The mapping used by absolute mouse input matches displayed geometry.
        settings=pp::Settings{};settings.p_v_zoom={.5f,.5f};settings.p_v_center={20,0};
        processor.settings_changed();draw();float x=281.6f,y=192;
        require(processor.map_output_to_scene(x,y)&&std::abs(x-256)<.01f&&std::abs(y-192)<.01f,"mouse maps image center through zoom and translation");
        processor.set_assets("","");settings=pp::Settings{};processor.settings_changed();draw();
        auto final=bytes(processor.capture_processed());require(final==plain,"capture compositor preserves plain decoded colors");
        require(processor.recreate(),"graphics device recreation");r=processor.renderer();
        processor.settings()=pp::Settings{};auto recreated=draw();require(recreated==plain,"renderer recreation restores valid clear history and frame");
        // Missing imported assets must not be decoded again on every frame.
        // Explicitly reloading the same preset retries repaired files.
        auto temp=std::filesystem::temp_directory_path()/("gs2-postprocess-assets-"+std::to_string(SDL_GetPerformanceCounter()));
        require(std::filesystem::create_directory(temp),"temporary asset directory");
        auto bezel_path=(temp/"bezel.bmp").string(),glass_path=(temp/"glass.bmp").string();
        auto save_color=[&](const std::string& path,Uint8 red,Uint8 green,Uint8 blue){
            auto* surface=SDL_CreateSurface(8,8,SDL_PIXELFORMAT_RGBA32);require(surface,"asset fixture surface");
            SDL_FillSurfaceRect(surface,nullptr,SDL_MapSurfaceRGBA(surface,red,green,blue,255));
            require(SDL_SaveBMP(surface,path.c_str()),"asset fixture save");SDL_DestroySurface(surface);
        };
        processor.set_assets(bezel_path,"");draw();
        require(bytes(processor.capture_processed())==plain,"missing bezel uses transparent fallback");
        save_color(bezel_path,0,255,255);draw();
        require(bytes(processor.capture_processed())==plain,"missing bezel is not retried during ordinary frames");
        processor.set_assets(bezel_path,"");draw();auto bezel_frame=bytes(processor.capture_processed());
        require(bezel_frame!=plain&&bezel_frame[0]==0&&bezel_frame[1]==255&&bezel_frame[2]==255,"explicit reload recovers repaired bezel");
        require(processor.status().find("could not be loaded")==std::string::npos,"repaired bezel clears its error status");
        processor.set_assets(bezel_path,glass_path);draw();
        require(bytes(processor.capture_processed())==bezel_frame,"missing glass retains bezel");
        save_color(glass_path,255,0,255);draw();
        require(bytes(processor.capture_processed())==bezel_frame,"missing glass is not retried during ordinary frames");
        processor.set_assets(bezel_path,glass_path);draw();auto glass_frame=bytes(processor.capture_processed());
        require(glass_frame[0]==255&&glass_frame[1]==0&&glass_frame[2]==255,"explicit reload recovers repaired glass");
        require(processor.status().find("could not be loaded")==std::string::npos,"repaired glass clears its error status");
        processor.set_assets("","");draw();std::filesystem::remove_all(temp);
        // If shader recreation fails but SDL can still draw, recovery must
        // resume through the plain renderer instead of leaving the app paused.
        const auto resource_path=gs2_app_values.base_path;
        gs2_app_values.base_path=(temp/"missing-shaders").string()+"/";
        processor.settings().preset_name="Recovery fallback";
        require(processor.recreate(),"plain renderer fallback counts as successful recreation");
        require(!processor.available(),"missing shaders exercise the plain renderer fallback");
        require(processor.settings().preset_name=="Recovery fallback","fallback recreation retains effect settings");
        r=processor.renderer();require(processor.begin_scene(512,384),"plain fallback begins a frame");
        require(SDL_SetRenderDrawColor(r,23,71,149,255)&&SDL_RenderClear(r),"plain fallback draws");
        processor.begin_ui(frame);require(processor.present(),"plain fallback presents");
        gs2_app_values.base_path=resource_path;
        require(processor.recreate()&&processor.available(),"restored shaders recover the effects renderer");
        r=processor.renderer();processor.settings()=pp::Settings{};
        require(draw()==plain,"effects recover after a temporary plain fallback");
        if(benchmark){
            processor.set_vsync(0);processor.settings().p_i_postprocessingLevel=2;
            processor.settings().p_f_phosphorBlur=.5f;processor.settings().p_f_ghostingPercent=50;
            processor.settings_changed();
            if(processor.device())SDL_WaitForGPUIdle(processor.device());
            Uint64 start=SDL_GetTicksNS();
            for(int i=0;i<180;++i){
                require(processor.begin_scene(1920,1080),"benchmark scene");
                SDL_SetRenderDrawColor(r,100,150,200,255);SDL_RenderClear(r);
                frame.source_region={0,0,1,1};frame.identity++;processor.begin_ui(frame);require(processor.present(),"benchmark present");
            }
            if(processor.device())SDL_WaitForGPUIdle(processor.device());
            std::printf("1080p CRT + mipmapped blur + history: %.3f ms/frame (180 frames, includes submission/presentation)\n",double(SDL_GetTicksNS()-start)/180.0/1000000.0);
        }
        std::puts("PASS: orientation, colors, all SuperDuperDisplay presets, composition, mipmaps, history, reset, mouse geometry, recreation and asset recovery");
    }
    SDL_DestroyWindow(window);SDL_Quit();return 0;
}
