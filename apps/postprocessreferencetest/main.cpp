// Independent reference shader validation. The original GLSL is compiled
// unchanged.
#define GL_SILENCE_DEPRECATION
#include "../../third_party/nlohmann/json.hpp"
#include "../postprocessfixtures/Fixture.hpp"
#include "display/postprocess/ImageLoader.hpp"
#include "display/postprocess/PostProcessPreset.hpp"
#include "display/postprocess/PostProcessor.hpp"
#include "gs2.hpp"
#include <OpenGL/gl3.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

gs2_app_t gs2_app_values;
namespace pp = gs2::postprocess;
using namespace gs2::postprocess::testfixtures;
static bool require_gpu = false;
[[noreturn]] void unavailable(const char *message) {
  std::fprintf(stderr, "%s: %s: %s\n", require_gpu ? "FAIL" : "SKIP", message,
               SDL_GetError());
  std::exit(require_gpu ? 1 : 77);
}
void check(bool x, const char *msg) {
  if (!x) {
    std::fprintf(stderr, "FAIL %s: %s\n", msg, SDL_GetError());
    std::exit(1);
  }
}
std::string read(const std::string &path) {
  std::ifstream f(path);
  check(bool(f), path.c_str());
  return {std::istreambuf_iterator<char>(f), {}};
}
GLuint shader(GLenum stage, const std::string &src) {
  GLuint s = glCreateShader(stage);
  const char *p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char b[8192];
    glGetShaderInfoLog(s, sizeof(b), nullptr, b);
    std::fprintf(stderr, "%s\n", b);
  }
  check(ok, "reference shader compilation");
  return s;
}

void save(const std::string &name, const Pixels &p) {
  auto *s = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_RGBA32,
                                  const_cast<unsigned char *>(p.data()), W * 4);
  check(s, "save surface");
  check(IMG_SavePNG(s, name.c_str()), "save PNG");
  SDL_DestroySurface(s);
}
struct Reference {
  SDL_Window *window;
  SDL_GLContext context;
  GLuint program, vao, source, history[2], fbo;
  int index = 0;
  Reference(const std::string &path, bool composite = false) {
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow("Reference shader", W, H,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window)
      unavailable("reference window");
    context = SDL_GL_CreateContext(window);
    if (!context)
      unavailable("OpenGL reference context");
    std::printf("Original OpenGL %s: vendor=%s; renderer=%s; version=%s\n",
                composite ? "composition" : "CRT",
                reinterpret_cast<const char *>(glGetString(GL_VENDOR)),
                reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    std::fflush(stdout);
    auto src = read(path);
    GLuint vs = shader(GL_VERTEX_SHADER,
                       "#version 410 core\n#define VERTEX\n" + src),
           fs = shader(GL_FRAGMENT_SHADER,
                       "#version 410 core\n#define FRAGMENT\n" + src);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    check(ok, "reference shader link");
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // Row zero is the visual top in both readbacks. This quad converts only
    // framebuffer orientation; the reference GLSL itself stays unchanged.
    float vertices[] = {-1, -1, 0, 0, 1, -1, 1, 0, 1,  1, 1, 1,
                        -1, -1, 0, 0, 1, 1,  1, 1, -1, 1, 0, 1};
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    for (int n = 0; n < 2; ++n) {
      auto loc = glGetAttribLocation(program, n == 0      ? "aPos"
                                              : composite ? "aTexCoord"
                                                          : "TexCoord");
      check(loc >= 0, "reference attribute");
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            reinterpret_cast<void *>(n * 2 * sizeof(float)));
    }
    glGenTextures(1, &source);
    glGenTextures(2, history);
    glGenFramebuffers(1, &fbo);
    for (auto t : {source, history[0], history[1]}) {
      glBindTexture(GL_TEXTURE_2D, t);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      t == source ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      t == source ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      t == source ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
    }
    reset();
  }
  void activate() {
    check(SDL_GL_MakeCurrent(window, context), "reference current");
    glUseProgram(program);
    glBindVertexArray(vao);
  }
  void reset() {
    activate();
    index = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (auto t : history) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, t, 0);
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
    }
  }
  void f(const char *k, float v) {
    glUniform1f(glGetUniformLocation(program, k), v);
  }
  void i(const char *k, int v) {
    glUniform1i(glGetUniformLocation(program, k), v);
  }
  void v(const char *k, float a, float b) {
    glUniform2f(glGetUniformLocation(program, k), a, b);
  }
  Pixels render(const pp::Settings &s, const Pixels &p, int count, int sequence,
                int source_height = H) {
    activate();
#define F(a, b) f(a, s.b)
#define I(a, b) i(a, s.b)
    F("GhostingPercent", p_f_ghostingPercent);
    F("BlurSize", p_f_phosphorBlur);
    I("bBlurGlow", p_b_phosphorGlow);
    I("bCORNER_SMOOTH", p_b_smoothCorner);
    I("bUseOKlab", p_b_useOKlab);
    I("bSLOT", p_b_slot);
    F("BARRELDISTORTION", p_f_barrelDistortion);
    F("BGR", p_f_bgr);
    F("BLACK", p_f_black);
    F("BR_DEP", p_f_brDep);
    F("BRIGHTNESS", p_f_brightness);
    F("CONTRAST", p_f_contrast);
    F("C_STR", p_f_cStr);
    F("CONV_B", p_f_convB);
    F("CONV_G", p_f_convG);
    F("CONV_R", p_f_convR);
    f("CORNER", s.p_f_corner / 10000);
    F("MASKH", p_f_maskHigh);
    F("MASKL", p_f_maskLow);
    F("MSIZE", p_f_maskSize);
    F("HUE", p_f_hue);
    F("GB", p_f_hueGB);
    F("RB", p_f_hueRB);
    F("RG", p_f_hueRG);
    F("SATURATION", p_f_saturation);
    F("SCANLINE_WEIGHT", p_f_scanlineWeight);
    F("SCAN_SPEED", p_f_scanSpeed);
    F("FILM_GRAIN", p_f_filmGrain);
    F("SLOTW", p_f_slotW);
    F("VIGNETTE_WEIGHT", p_f_vignetteWeight);
    F("INTERLACE_WEIGHT", p_f_interlace);
    I("iCOLOR_SPACE", p_i_cSpace);
    I("iM_TYPE", p_i_maskType);
    I("iSCANLINE_TYPE", p_i_scanlineType);
    I("POSTPROCESSING_LEVEL", p_i_postprocessingLevel);
    I("bHalveFrameRate", bHalveFramerate);
#undef F
#undef I
    i("iFrameCount", count);
    i("iFrameMergeCount", sequence);
    i("A2TextureCurrent", 0);
    i("PreviousFrame", 1);
    v("TextureSize", W, source_height);
    v("InputSize", W, source_height);
    // The original host sends the quad size BEFORE applying zoom.
    v("OutputSize", W, H);
    glUniform1ui(glGetUniformLocation(program, "ScanlineCount"), 192);
    v("vWARP", s.p_v_warp[0], s.p_v_warp[1]);
    float transform[] = {s.p_v_zoom[0],
                         0,
                         0,
                         0,
                         0,
                         s.p_v_zoom[1],
                         0,
                         0,
                         0,
                         0,
                         1,
                         0,
                         s.p_v_center[0] * s.p_v_zoom[0] / 100,
                         -s.p_v_center[1] * s.p_v_zoom[1] / 100,
                         0,
                         1};
    glUniformMatrix4fv(glGetUniformLocation(program, "uTransform"), 1, GL_FALSE,
                       transform);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    Pixels packed(W * source_height * 4);
    for (int y = 0; y < source_height; ++y)
      std::copy_n(p.data() + (y * H / source_height) * W * 4, W * 4,
                  packed.data() + y * W * 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, source_height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, packed.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, history[1 - index]);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           history[index], 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "reference framebuffer");
    glViewport(0, 0, W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    Pixels out(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    check(glGetError() == GL_NO_ERROR, "reference draw");
    index = 1 - index;
    return out;
  }

  Pixels composite(const pp::Settings &s, const Pixels &source_pixels,
                   const Pixels &base, const std::string &bezel_path,
                   const std::string &glass_path) {
    activate();
    GLuint image[2];
    glGenTextures(2, image);
    auto upload = [&](GLuint texture, const std::string &path) {
      SDL_Surface *loaded =
          path.empty() ? SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32)
                       : pp::load_image_rgba(path.c_str());
      check(loaded, "reference composition asset");
      if (path.empty())
        SDL_ClearSurface(loaded, 0, 0, 0, 0);
      auto *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
      SDL_DestroySurface(loaded);
      check(rgba, "reference composition asset format");
      glBindTexture(GL_TEXTURE_2D, texture);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, rgba->pixels);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      SDL_DestroySurface(rgba);
    };
    glActiveTexture(GL_TEXTURE0);
    upload(image[0], bezel_path);
    glActiveTexture(GL_TEXTURE2);
    upload(image[1], glass_path);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, source);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE,
                    source_pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, history[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE,
                    base.data());
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           history[0], 0);
    i("uMainTex", 0);
    i("uA2Tex", 1);
    i("uGlassTex", 2);
    f("uReflectionAmount", s.p_f_bezelReflection);
    f("uReflectionBlur", s.p_f_reflectionBlur);
    f("uGlassThickness", glass_path.empty() ? 0 : s.p_f_glassThickness);
    v("uReflectionScale", s.p_v_reflectionScale[0], s.p_v_reflectionScale[1]);
    v("uReflectionTranslation", s.p_v_reflectionTranslation[0],
      s.p_v_reflectionTranslation[1]);
    i("uOutlineQuad", s.p_b_outlineQuad);
    float transform[] = {s.bezelWidth,
                         0,
                         0,
                         0,
                         0,
                         s.bezelHeight,
                         0,
                         0,
                         0,
                         0,
                         1,
                         0,
                         s.bezelCenterX * s.bezelWidth / 100,
                         -s.bezelCenterY * s.bezelHeight / 100,
                         0,
                         1};
    glUniformMatrix4fv(glGetUniformLocation(program, "uTransform"), 1, GL_FALSE,
                       transform);
    glViewport(0, 0, W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    Pixels out(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    check(glGetError() == GL_NO_ERROR, "reference composition draw");
    glDeleteTextures(2, image);
    return out;
  }
  ~Reference() {
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
  }
};

int main(int argc, char **argv) {
  check(argc >= 3,
        "usage: postprocessreferencetest RESOURCE_PATH ARTIFACT_PATH");
  bool benchmark = false, boundary_probes = false, all_images = false;
  std::string export_goldens;
  for (int i = 3; i < argc; ++i) {
    if (std::string(argv[i]) == "--export-goldens") {
      check(i + 1 < argc, "golden export path");
      export_goldens = argv[++i];
      continue;
    }
    require_gpu |= std::string(argv[i]) == "--require-gpu";
    benchmark |= std::string(argv[i]) == "--benchmark";
    boundary_probes |= std::string(argv[i]) == "--boundary-probes";
    all_images |= std::string(argv[i]) == "--all-images";
  }
  if (!SDL_Init(SDL_INIT_VIDEO))
    unavailable("SDL video initialization");
  gs2_app_values.base_path = std::string(argv[1]) + "/";
  std::filesystem::create_directories(argv[2]);
  Reference ref(gs2_app_values.base_path +
                "shaders/postprocess/source_crt_reference.glsl");
  Reference compositor(gs2_app_values.base_path +
                           "shaders/postprocess/source_bezel_reference.glsl",
                       true);
  auto *w = SDL_CreateWindow("Postprocessing numerical parity", W, H,
                             SDL_WINDOW_HIDDEN);
  if (!w)
    unavailable("port window");
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_owner(
      w, SDL_DestroyWindow);
  pp::PostProcessor port(w);
  if (!port.available())
    unavailable("Metal backend");
  std::puts(port.status().c_str());
  auto *r = port.renderer();
  auto *texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, W, H);
  check(texture, "fixture texture");
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  pp::FrameView frame;
  frame.source_width = W;
  frame.source_height = H;
  frame.scanlines = 192;
  int count = 0;
  std::ofstream csv(std::string(argv[2]) + "/metrics.csv");
  csv << "fixture,frame,mean_rgb,rms_rgb,p99_rgb,max_rgb,percent_above_3,ssim_"
         "luma_8x8,signed_mean_rgb\n";
  std::vector<std::pair<std::string, pp::Settings>> cases;
  auto add = [&](std::string name, std::function<void(pp::Settings &)> fn) {
    pp::Settings s;
    s.p_i_postprocessingLevel = 2;
    s.p_f_scanlineWeight = 0;
    fn(s);
    cases.emplace_back(name, s);
  };
  add("neutral", [](auto &) {});
  add("scanlines", [](auto &s) {
    s.p_f_scanlineWeight = .8f;
    s.p_f_scanSpeed = .4f;
  });
  add("mask", [](auto &s) {
    s.p_i_maskType = 2;
    s.p_b_slot = true;
  });
  add("curvature", [](auto &s) {
    s.p_v_warp = {.02f, .025f};
    s.p_f_barrelDistortion = .08f;
  });
  add("zoomed_mask", [](auto &s) {
    s.p_i_maskType = 2;
    s.p_b_slot = true;
    // Even raster bounds keep derivative pairs inside the original quad;
    // this scale also avoids repeated exact mask-phase ties.
    s.p_v_zoom = {.70931f, .70931f};
  });
  if (boundary_probes) {
    add("boundary_derivative_zoom", [](auto &s) {
      s.p_i_maskType = 2;
      s.p_b_slot = true;
      s.p_v_zoom = {.713f, .713f};
    });
    add("boundary_zoomed_mask", [](auto &s) {
      s.p_i_maskType = 2;
      s.p_b_slot = true;
      s.p_v_zoom = {.75f, .75f};
    });
  }
  add("black_level", [](auto &s) { s.p_f_black = .04f; });
  add("mask_alternate", [](auto &s) {
    s.p_i_maskType = 1;
    s.p_b_slot = true;
    s.p_f_bgr = 1;
  });
  add("scanline_alternate_2x", [](auto &s) { s.p_i_scanlineType = 1; });
  add("corners_hard", [](auto &s) { s.p_f_corner = 30; });
  for (int space = 1; space <= 3; ++space)
    add("color_space_" + std::to_string(space),
        [space](auto &s) { s.p_i_cSpace = space; });
  add("corners", [](auto &s) {
    s.p_f_corner = 30;
    s.p_b_smoothCorner = true;
  });
  add("vignette", [](auto &s) { s.p_f_vignetteWeight = 1; });
  add("convergence", [](auto &s) {
    s.p_f_convR = .5f;
    s.p_f_convB = -.5f;
    s.p_f_cStr = .3f;
  });
  add("linear_color", [](auto &s) {
    s.p_f_brightness = 1.1f;
    s.p_f_contrast = 1.1f;
    s.p_f_saturation = .8f;
    s.p_f_hueGB = .2f;
  });
  add("perceptual_color", [](auto &s) {
    s.p_b_useOKlab = true;
    s.p_f_hue = .25f;
    s.p_f_saturation = 1.2f;
  });
  add("phosphor_blur", [](auto &s) { s.p_f_phosphorBlur = .5f; });
  add("phosphor_glow", [](auto &s) {
    s.p_f_phosphorBlur = 1;
    s.p_b_phosphorGlow = true;
  });
  add("grain", [](auto &s) { s.p_f_filmGrain = .1f; });
  add("interlace", [](auto &s) { s.p_f_interlace = .1f; });
  add("history", [](auto &s) { s.p_f_ghostingPercent = 80; });
  add("history_perceptual", [](auto &s) {
    s.p_f_ghostingPercent = 80;
    s.p_b_useOKlab = true;
  });
  add("field_merge", [](auto &s) { s.bHalveFramerate = true; });
  auto make_asset = [&](const std::string &name, bool glass,
                        unsigned char alpha = 64) {
    auto *image = SDL_CreateSurface(128, 80, SDL_PIXELFORMAT_RGBA32);
    check(image, "synthetic asset");
    for (int y = 0; y < 80; ++y)
      for (int x = 0; x < 128; ++x) {
        auto *p = static_cast<unsigned char *>(image->pixels) +
                  y * image->pitch + x * 4;
        if (glass) {
          p[0] = 70;
          p[1] = 120;
          p[2] = 180;
          p[3] = alpha;
        } else {
          p[0] = 30 + x;
          p[1] = 35 + y;
          p[2] = 55;
          p[3] = (x < 5 || x >= 123 || y < 5 || y >= 75)     ? 255
                 : (x < 18 || x >= 110 || y < 15 || y >= 65) ? 128
                                                             : 0;
        }
      }
    auto path = std::filesystem::path(argv[2]) / name;
    check(IMG_SavePNG(image, path.string().c_str()), "synthetic asset save");
    SDL_DestroySurface(image);
    return path.string();
  };
  auto bezel_only = make_asset("synthetic-bezel.png", false),
       with_glass = make_asset("synthetic-glass.png", false);
  make_asset("synthetic-glass.glass.png", true);
  auto with_thick_glass = make_asset("synthetic-thick.png", false);
  make_asset("synthetic-thick.glass.png", true, 192);
  add("bezel_only", [&](auto &s) { s.bezelName = bezel_only; });
  add("reflection", [&](auto &s) {
    s.bezelName = bezel_only;
    s.p_f_bezelReflection = .4f;
    s.p_f_reflectionBlur = 3;
    s.p_v_reflectionScale = {1.7f, 1.8f};
    s.p_v_reflectionTranslation = {.1f, -.1f};
  });
  add("reflection_outline", [&](auto &s) {
    s.bezelName = with_glass;
    s.p_f_bezelReflection = .4f;
    s.p_b_outlineQuad = true;
    s.p_v_reflectionScale = {1.3f, 1.1f};
  });
  add("glass", [&](auto &s) {
    s.bezelName = with_glass;
    s.p_f_glassThickness = 2;
  });
  add("thick_glass", [&](auto &s) {
    s.bezelName = with_glass;
    s.bezelName = with_thick_glass;
    s.p_f_glassThickness = 2;
  });
  for (auto const &e : std::filesystem::directory_iterator(
           gs2_app_values.base_path + "postprocess/presets")) {
    if (e.path().extension() != ".json")
      continue;
    pp::Settings s;
    std::string error;
    check(pp::load_preset(e.path().string(), s, error), error.c_str());
    s.p_i_postprocessingLevel = 2;
    // Source and output have identical dimensions here, so either original
    // automatic scaling or fill-window produces the same pre-zoom quad.
    s.bAutoScale = true;
    s.bCRTFillWindow = true;
    cases.emplace_back(e.path().stem().string(), s);
  }

  nlohmann::json golden_manifest = {
      {"version", 1},
      {"fixture_version", fixture_version},
      {"width", W},
      {"height", H},
      {"reference_project", "SuperDuperDisplay"},
      {"reference_commit", "b49a491671cb78d0a1fba1d1ba2c950f1a6c2455"},
      {"reference_crt_sha256",
       "491c6e55967d88d33034eb0d5d4a3db0393963416de8000abe42a6ed8059afef"},
      {"reference_bezel_sha256",
       "7532e291345c339cce644cedb0f0fed90d83512f37d522dae0f1933226aa326c"},
      {"cases", nlohmann::json::array()}};
  auto portable_case = [](const std::string &name) {
    static const std::vector<std::string> names = {
        "neutral",       "corners",    "zoomed_mask",
        "phosphor_blur", "history",    "history_perceptual",
        "field_merge",   "reflection", "glass",
        "thick_glass"};
    return name.rfind("00_default", 0) == 0 ||
           std::find(names.begin(), names.end(), name) != names.end();
  };
  if (!export_goldens.empty())
    std::filesystem::create_directories(export_goldens);
  auto export_asset = [&](const std::string &path) {
    if (path.empty())
      return std::string{};
    if (path.rfind(gs2_app_values.base_path, 0) == 0)
      return "resource:" + path.substr(gs2_app_values.base_path.size());
    auto file = std::filesystem::path(path).filename();
    std::filesystem::copy_file(
        path, std::filesystem::path(export_goldens) / file,
        std::filesystem::copy_options::overwrite_existing);
    return "golden:" + file.string();
  };
  auto export_frame = [&](const std::string &name, int seq, int frame_count,
                          const Pixels &pixels) {
    const bool keep =
        seq == 3 ||
        ((name == "history" || name == "history_perceptual") && seq > 0) ||
        (name == "field_merge" && seq == 1);
    nlohmann::json output = nullptr;
    if (keep) {
      auto file = name + "-" + std::to_string(seq) + ".png";
      auto *surface = SDL_CreateSurfaceFrom(
          W, H, SDL_PIXELFORMAT_RGBA32,
          const_cast<unsigned char *>(pixels.data()), W * 4);
      check(surface, "golden image surface");
      check(
          IMG_SavePNG(
              surface,
              (std::filesystem::path(export_goldens) / file).string().c_str()),
          "golden PNG export");
      SDL_DestroySurface(surface);
      output = file;
    }
    golden_manifest["cases"].back()["frames"].push_back(
        {{"phase", seq % 3},
         {"seconds", frame_count / 60.0},
         {"image", output}});
  };
  int failures = 0, comparisons = 0;
  auto validate = [&](const std::string &name, const Metric &m) {
    if (name.rfind("boundary_", 0) == 0)
      return;
    ++comparisons;
    bool okay = within_tolerance(name, m);
    if (!okay) {
      ++failures;
      std::fprintf(stderr, "FAIL reference tolerance: %s\n", name.c_str());
    }
  };
  for (auto const &[name, s] : cases) {
    auto bp = std::filesystem::path(gs2_app_values.base_path) /
              "postprocess/bezels" / s.bezelName;
    auto gp = bp.parent_path() /
              (bp.stem().string() + ".glass" + bp.extension().string());
    std::string bezel_path = s.bezelName == "NONE" ? "" : bp.string(),
                glass_path = std::filesystem::exists(gp) ? gp.string() : "";
    const bool exporting = !export_goldens.empty() && portable_case(name);
    if (exporting) {
      auto portable = s;
      portable.bezelName =
          std::filesystem::path(s.bezelName).filename().string();
      golden_manifest["cases"].push_back(
          {{"name", name},
           {"settings", nlohmann::json::parse(pp::serialize_preset(portable))},
           {"warmup", s.p_f_ghostingPercent > 0},
           {"bezel", export_asset(bezel_path)},
           {"glass", export_asset(glass_path)},
           {"frames", nlohmann::json::array()}});
    }
    port.set_assets(bezel_path, glass_path);
    port.settings() = s;
    port.settings_changed();
    ref.reset();
    for (int seq = s.p_f_ghostingPercent > 0 ? -1 : 0; seq < 4; ++seq) {
      auto active = s;
      // Warm both histories identically. The original host leaves initial GPU
      // history undefined; fresh-history behavior is covered by
      // postprocesstest.
      if (seq == -1)
        active.p_f_ghostingPercent = 0;
      port.settings() = active;
      auto pixels = fixture(seq <= 0 ? 0 : seq % 3);
      // At 1:1, the original shader discards one lane of every vertical
      // derivative pair before sampling. Use a real 2x scanline scale to avoid
      // asking two drivers to agree about those undefined texture derivatives.
      const int source_height = name == "scanline_alternate_2x" ? H / 2 : H;
      if (source_height != H)
        for (int y = 1; y < H; y += 2)
          std::copy_n(pixels.data() + (y - 1) * W * 4, W * 4,
                      pixels.data() + y * W * 4);
      frame.source_height = source_height;
      check(port.begin_scene(W, H), "port begin");
      check(SDL_UpdateTexture(texture, nullptr, pixels.data(), W * 4),
            "fixture texture upload");
      SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
      SDL_RenderClear(r);
      check(SDL_RenderTexture(r, texture, nullptr, nullptr), "fixture draw");
      frame.identity++;
      frame.seconds = count / 60.;
      port.begin_ui(frame);
      check(port.present(), "port present");
      auto *surface = port.capture_crt();
      check(surface, "port capture");
      auto *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
      check(rgba, "fixture conversion");
      Pixels actual(W * H * 4);
      for (int y = 0; y < H; ++y)
        std::copy_n(static_cast<unsigned char *>(rgba->pixels) +
                        y * rgba->pitch,
                    W * 4, actual.data() + y * W * 4);
      SDL_DestroySurface(surface);
      SDL_DestroySurface(rgba);
      auto expected =
          ref.render(active, pixels, count, std::max(seq, 0), source_height);
      if (seq < 0) {
        ++count;
        continue;
      }
      auto m = compare(actual, expected);
      validate(name, m);
      if (exporting && bezel_path.empty())
        export_frame(name, seq, count, expected);
      csv << name << ',' << seq << ',' << m.mean << ',' << m.rms << ',' << m.p99
          << ',' << m.max << ',' << m.changed << ',' << m.ssim << ','
          << m.signed_mean << '\n';
      std::printf(
          "%-24s %d mean %.4f rms %.4f p99 %.0f max %d >3 %.3f%% SSIM %.6f\n",
          name.c_str(), seq, m.mean, m.rms, m.p99, m.max, m.changed, m.ssim);
      if (all_images || !within_tolerance(name, m)) {
        auto prefix = std::string(argv[2]) + "/" + name + "-" + std::to_string(seq);
        save(prefix + "-reference.png", expected);
        save(prefix + "-metal.png", actual);
      }
      if (!bezel_path.empty()) {
        auto *composed_surface = port.capture_processed();
        check(composed_surface, "composed capture");
        auto *composed_rgba =
            SDL_ConvertSurface(composed_surface, SDL_PIXELFORMAT_RGBA32);
        check(composed_rgba, "composition conversion");
        Pixels composed(W * H * 4);
        for (int y = 0; y < H; ++y)
          std::copy_n(static_cast<unsigned char *>(composed_rgba->pixels) +
                          y * composed_rgba->pitch,
                      W * 4, composed.data() + y * W * 4);
        SDL_DestroySurface(composed_surface);
        SDL_DestroySurface(composed_rgba);
        auto composition_reference =
            compositor.composite(s, pixels, expected, bezel_path, glass_path);
        auto c = compare(composed, composition_reference);
        validate("compose_" + name, c);
        if (exporting)
          export_frame(name, seq, count, composition_reference);
        csv << "compose_" << name << ',' << seq << ',' << c.mean << ',' << c.rms
            << ',' << c.p99 << ',' << c.max << ',' << c.changed << ',' << c.ssim
            << ',' << c.signed_mean << '\n';
        std::printf("compose_%-16s %d mean %.4f rms %.4f p99 %.0f max %d >3 "
                    "%.3f%% SSIM %.6f\n",
                    name.c_str(), seq, c.mean, c.rms, c.p99, c.max, c.changed,
                    c.ssim);
        if (all_images || !within_tolerance("compose_" + name, c)) {
          auto prefix = std::string(argv[2]) + "/compose_" + name + "-" + std::to_string(seq);
          save(prefix + "-reference.png",
               composition_reference);
          save(prefix + "-metal.png",
               composed);
        }
      }
      ++count;
    }
  }
  if (!export_goldens.empty()) {
    std::ofstream manifest(std::filesystem::path(export_goldens) /
                           "manifest.json");
    manifest << golden_manifest.dump(2) << '\n';
    check(bool(manifest), "golden manifest export");
  }
  if (benchmark) {
    pp::Settings settings;
    std::string error;
    check(pp::load_preset(gs2_app_values.base_path +
                              "postprocess/presets/00_default_apple_crt.json",
                          settings, error),
          error.c_str());
    settings.p_f_phosphorBlur = .5f;
    settings.p_f_ghostingPercent = 50;
    settings.p_b_useOKlab = true;
    port.settings() = settings;
    port.settings_changed();
    auto bezel = std::filesystem::path(gs2_app_values.base_path) /
                 "postprocess/bezels" / settings.bezelName;
    auto glass = bezel.parent_path() / (bezel.stem().string() + ".glass" +
                                        bezel.extension().string());
    port.set_assets(bezel.string(),
                    std::filesystem::exists(glass) ? glass.string() : "");
    port.set_vsync(0);
    auto pixels = fixture(0);
    check(SDL_UpdateTexture(texture, nullptr, pixels.data(), W * 4),
          "fixture texture upload");
    for (auto dimensions : {std::pair<int, int>{1920, 1080}, {3840, 2160}}) {
      check(SDL_SetWindowSize(w, dimensions.first, dimensions.second),
            "benchmark window size");
      int drawable_w = 0, drawable_h = 0;
      SDL_GetWindowSizeInPixels(w, &drawable_w, &drawable_h);
      std::printf("BENCHMARK drawable: %dx%d\n", drawable_w, drawable_h);
      auto draw = [&]() {
        check(port.begin_scene(dimensions.first, dimensions.second),
              "benchmark begin");
        check(SDL_RenderTexture(r, texture, nullptr, nullptr), "fixture draw");
        frame.identity++;
        port.begin_ui(frame);
        check(port.present(), "benchmark present");
      };
      for (int i = 0; i < 10; ++i)
        draw();
      auto *target = port.capture_crt();
      check(target, "benchmark target readback");
      const int target_w = target->w, target_h = target->h;
      SDL_DestroySurface(target);
      auto *probe = SDL_AcquireGPUCommandBuffer(port.device());
      check(probe, "benchmark swapchain probe");
      SDL_GPUTexture *swap = nullptr;
      Uint32 swap_w = 0, swap_h = 0;
      check(SDL_WaitAndAcquireGPUSwapchainTexture(probe, w, &swap, &swap_w,
                                                  &swap_h),
            "benchmark swapchain acquire");
      check(SDL_SubmitGPUCommandBuffer(probe),
            "benchmark swapchain probe submit");
      std::printf("BENCHMARK actual CRT target: %dx%d; swapchain: %ux%u (%s)\n",
                  target_w, target_h, swap_w, swap_h,
                  swap ? "acquired" : "unavailable");
      check(target_w == dimensions.first && target_h == dimensions.second,
            "benchmark CRT dimensions");
      check(swap && swap_w == Uint32(dimensions.first) &&
                swap_h == Uint32(dimensions.second),
            "benchmark swapchain dimensions");
      SDL_WaitForGPUIdle(port.device());
      auto start = SDL_GetTicksNS();
      for (int i = 0; i < 120; ++i)
        draw();
      SDL_WaitForGPUIdle(port.device());
      double elapsed = double(SDL_GetTicksNS() - start) / 120 / 1.e6;
      std::printf("BENCHMARK %dx%d Apple CRT preset + blur + 50%% history + "
                  "perceptual color: %.3f ms/frame (120 frames, submission and "
                  "presentation included)\n",
                  dimensions.first, dimensions.second, elapsed);
    }
  }
  SDL_DestroyTexture(texture);
  std::printf("%s: %d reference image comparisons; %d failures\n",
              failures ? "FAIL" : "PASS", comparisons, failures);
  return failures ? 1 : 0;
}
