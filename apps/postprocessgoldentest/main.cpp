// Replays unchanged original-shader golden outputs on each production backend.
#include "../../third_party/nlohmann/json.hpp"
#include "../postprocessfixtures/Fixture.hpp"
#include "display/postprocess/ImageLoader.hpp"
#include "display/postprocess/PostProcessPreset.hpp"
#include "display/postprocess/PostProcessor.hpp"
#include "gs2.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

gs2_app_t gs2_app_values;
namespace pp = gs2::postprocess;
namespace fx = pp::testfixtures;
using Json = nlohmann::json;
static void require(bool okay, const std::string &message) {
  if (!okay)
    throw std::runtime_error(message + ": " + SDL_GetError());
}
static fx::Pixels pixels(SDL_Surface *source) {
  require(source, "frame surface");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> owner(
      source, SDL_DestroySurface);
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
      SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  require(bool(rgba), "RGBA32 conversion");
  require(rgba->w == fx::W && rgba->h == fx::H, "golden image dimensions");
  fx::Pixels out(fx::W * fx::H * 4);
  for (int y = 0; y < fx::H; ++y)
    std::copy_n(static_cast<unsigned char *>(rgba->pixels) + y * rgba->pitch,
                fx::W * 4, out.data() + y * fx::W * 4);
  return out;
}
struct GoldenTest {
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
      nullptr, SDL_DestroyWindow};
  std::unique_ptr<pp::PostProcessor> processor;
  std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{
      nullptr, SDL_DestroyTexture};
  Json manifest;
  std::filesystem::path goldens, artifacts;
  std::ofstream csv;
  pp::Settings settings;
  pp::FrameView frame;
  size_t case_index = 0, frame_index = 0;
  bool warmup = false;
  int comparisons = 0, failures = 0;
  std::string backend;
  GoldenTest(const std::filesystem::path &golden_path,
             const std::filesystem::path &artifact_path, bool require_gl,
             bool require_native)
      : goldens(golden_path), artifacts(artifact_path) {
    std::ifstream input(goldens / "manifest.json");
    require(bool(input), "open golden manifest");
    input >> manifest;
    require(manifest.at("version") == 1 &&
                manifest.at("fixture_version") == fx::fixture_version,
            "golden fixture version");
    require(manifest.at("width") == fx::W && manifest.at("height") == fx::H,
            "golden dimensions");
    require(manifest.at("cases").size() == 16, "complete portable case set");
    // Keep identical asset bytes across platform image decoders. In
    // particular alpha zero must not erase RGB used at reflection boundaries.
    auto check_asset = [&](const char *name, int x, int y,
                           std::array<Uint8, 4> expected) {
      std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
          pp::load_image_rgba((goldens / name).string().c_str()),
          SDL_DestroySurface);
      require(bool(source), "decode fixture asset");
      std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
          SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32),
          SDL_DestroySurface);
      require(bool(rgba), "fixture asset RGBA");
      require(x < rgba->w && y < rgba->h, "fixture asset sample dimensions");
      const auto *pixel =
          static_cast<const Uint8 *>(rgba->pixels) + y * rgba->pitch + x * 4;
      require(std::equal(expected.begin(), expected.end(), pixel),
              "preserve exact straight-alpha asset bytes");
    };
    check_asset("synthetic-bezel.png", 50, 40, {80, 75, 55, 0});
    check_asset("synthetic-glass.glass.png", 10, 10, {70, 120, 180, 64});
    window.reset(SDL_CreateWindow("Postprocessing backend parity", fx::W, fx::H,
                                  SDL_WINDOW_HIDDEN));
    require(bool(window), "test window");
    processor = std::make_unique<pp::PostProcessor>(window.get());
    if (!processor->available())
      throw std::runtime_error("NO_GPU: " + processor->status());
    if (require_gl)
      require(processor->device() == nullptr, "OpenGL backend selection");
    if (require_native)
      require(processor->device() != nullptr, "native GPU backend selection");
    backend = processor->status();
    std::puts(backend.c_str());
#ifndef __EMSCRIPTEN__
    if (auto *device = processor->device()) {
      const char *device_name =
          SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),
                                SDL_PROP_GPU_DEVICE_NAME_STRING, "unavailable");
      std::printf("Native GPU device: %s\n", device_name);
    }
#endif
    auto *renderer = processor->renderer();
    texture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, fx::W, fx::H));
    require(bool(texture), "fixture texture");
    require(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST),
            "fixture nearest scale");
    require(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE),
            "fixture opaque blend");
    frame.source_width = fx::W;
    frame.source_height = fx::H;
    frame.scanlines = 192;
    if (!artifacts.empty()) {
      std::filesystem::create_directories(artifacts);
      csv.open(artifacts / "golden-metrics.csv");
      require(bool(csv), "open golden metrics");
      csv << "fixture,frame,mean_rgb,rms_rgb,p99_rgb,max_rgb,percent_above_3,"
             "ssim_luma_8x8,signed_mean_rgb\n";
    }
    begin_case();
  }
  std::string asset(const std::string &location) {
    if (location.empty())
      return {};
    if (location.rfind("resource:", 0) == 0)
      return (std::filesystem::path(gs2_app_values.base_path) /
              location.substr(9))
          .string();
    if (location.rfind("golden:", 0) == 0)
      return (goldens / location.substr(7)).string();
    throw std::runtime_error("Unknown golden asset location");
  }
  void begin_case() {
    const auto &entry = manifest["cases"][case_index];
    std::string error;
    require(
        pp::deserialize_preset(entry.at("settings").dump(), settings, error),
        "golden settings: " + error);
    require(entry.at("frames").size() == 4, "four deterministic frames");
    processor->set_assets(asset(entry.at("bezel")), asset(entry.at("glass")));
    processor->settings() = settings;
    processor->settings_changed();
    frame_index = 0;
    warmup = entry.at("warmup").get<bool>();
  }
  bool step() {
    const auto &entry = manifest["cases"][case_index];
    const auto &expected_frame = entry["frames"][frame_index];
    auto active = settings;
    if (warmup)
      active.p_f_ghostingPercent = 0;
    processor->settings() = active;
    const int phase = warmup ? 0 : expected_frame.at("phase").get<int>();
    auto input = fx::fixture(phase);
    require(processor->begin_scene(fx::W, fx::H), "begin fixture scene");
    auto *renderer = processor->renderer();
    require(SDL_UpdateTexture(texture.get(), nullptr, input.data(), fx::W * 4),
            "fixture upload");
    require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
                SDL_RenderClear(renderer),
            "fixture clear");
    require(SDL_RenderTexture(renderer, texture.get(), nullptr, nullptr),
            "fixture render");
    ++frame.identity;
    frame.seconds =
        expected_frame.at("seconds").get<double>() - (warmup ? 1.0 / 60 : 0);
    processor->begin_ui(frame);
    require(processor->present(), "fixture present");
    if (warmup) {
      warmup = false;
      return false;
    }
    if (expected_frame.at("image").is_null())
      return advance_frame();
    auto actual = pixels(processor->capture_processed());
    const auto filename = expected_frame.at("image").get<std::string>();
    auto reference =
        pixels(pp::load_image_rgba((goldens / filename).string().c_str()));
    auto metric = fx::compare(actual, reference);
    const auto name = entry.at("name").get<std::string>();
    const bool okay = fx::within_tolerance(name, metric);
    ++comparisons;
    if (!okay)
      ++failures;
    std::printf("%s %s frame%zu: mean %.5f RMS %.5f SSIM %.6f >3 %.4f%%\n",
                okay ? "PASS" : "FAIL", name.c_str(), frame_index, metric.mean,
                metric.rms, metric.ssim, metric.changed);
    std::fflush(stdout);
    if (csv)
      csv << name << ',' << frame_index << ',' << metric.mean << ','
          << metric.rms << ',' << metric.p99 << ',' << metric.max << ','
          << metric.changed << ',' << metric.ssim << ',' << metric.signed_mean
          << '\n';
    if (!okay && !artifacts.empty()) {
      auto *surface = SDL_CreateSurfaceFrom(
          fx::W, fx::H, SDL_PIXELFORMAT_RGBA32, actual.data(), fx::W * 4);
      require(surface, "failure image surface");
      require(
          IMG_SavePNG(surface,
                      (artifacts / ("actual-" + filename)).string().c_str()),
          "failure PNG");
      SDL_DestroySurface(surface);
    }
    return advance_frame();
  }
  bool advance_frame() {
    if (++frame_index == 4) {
      if (++case_index == manifest["cases"].size())
        return true;
      begin_case();
    }
    return false;
  }
  void finish() {
    std::printf("%s: %d original-shader golden comparisons; %d failures\n",
                failures ? "FAIL" : "PASS", comparisons, failures);
    std::fflush(stdout);
    if (csv)
      csv.flush();
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM(
        {
          window.gs2GoldenTest = ({
            status : $0 ? "failed" : "passed",
            comparisons : $1,
            failures : $0,
            backend : UTF8ToString($2)
          });
        },
        failures, comparisons, backend.c_str());
#endif
  }
};
#ifdef __EMSCRIPTEN__
static std::unique_ptr<GoldenTest> web_test;
static void web_step() {
  try {
    if (web_test->step()) {
      web_test->finish();
      emscripten_cancel_main_loop();
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    MAIN_THREAD_EM_ASM(
        {
          window.gs2GoldenTest =
              ({status : "failed", error : UTF8ToString($0)});
        },
        error.what());
    emscripten_cancel_main_loop();
  }
}
#endif
int main(int argc, char **argv) {
  bool strict = false, require_gl = false;
  std::filesystem::path goldens, artifacts;
  auto base = std::filesystem::absolute(argv[0]).parent_path();
#ifdef __EMSCRIPTEN__
  gs2_app_values.base_path = "/resources/";
  goldens = "/goldens";
  artifacts = "/golden-artifacts";
  MAIN_THREAD_EM_ASM({ window.gs2GoldenTest = ({status : "running"}); });
#else
  gs2_app_values.base_path = (base / "resources").string() + "/";
  goldens = base / "postprocess-goldens";
#endif
  if (const char *path = SDL_getenv("GS2_TEST_RESOURCE_PATH"))
    gs2_app_values.base_path = std::string(path) + "/";
  if (const char *path = SDL_getenv("GS2_TEST_GOLDEN_PATH"))
    goldens = path;
  if (const char *path = SDL_getenv("GS2_TEST_ARTIFACT_DIR"))
    artifacts = path;
  for (int i = 1; i < argc; ++i) {
    strict |= std::string(argv[i]) == "--require-gpu";
    require_gl |= std::string(argv[i]) == "--require-opengl";
  }
  strict |= require_gl;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "%s: %s\n", strict ? "FAIL" : "SKIP", SDL_GetError());
    return strict ? 1 : 77;
  }
  try {
#ifdef __EMSCRIPTEN__
    web_test = std::make_unique<GoldenTest>(goldens, artifacts, true, false);
    emscripten_set_main_loop(web_step, 0, 1);
#else
    GoldenTest test(goldens, artifacts, require_gl, strict && !require_gl);
    while (!test.step()) {
    }
    test.finish();
    return test.failures ? 1 : 0;
#endif
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM(
        {
          window.gs2GoldenTest =
              ({status : "failed", error : UTF8ToString($0)});
        },
        error.what());
#else
    if (!strict && std::string(error.what()).rfind("NO_GPU:", 0) == 0)
      return 77;
#endif
    return 1;
  }
  return 0;
}
