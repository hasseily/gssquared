#include "display/postprocess/PostProcessPreset.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

using namespace gs2::postprocess;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    try {
        Settings s;
        std::string error;
        require(deserialize_preset(R"({"p_f_flimGrain":0.37,"p_i_postprocessingLevel":2,"preset_name":"Legacy"})", s, error), "legacy SuperDuperDisplay preset rejected");
        require(std::abs(s.p_f_filmGrain - .37f) < 1e-6 && s.p_i_postprocessingLevel == 2, "legacy film grain lost");
        require(deserialize_preset(R"({"p_f_flimGrain":0.2,"p_f_filmGrain":0.7})", s, error), "dual spelling rejected");
        require(std::abs(s.p_f_filmGrain - .7f) < 1e-6, "canonical film grain should win");
        require(deserialize_preset(R"({"p_v_zoomX":1.3,"p_v_warpY":-0.1,"p_v_reflectionTranslationX":0.17,"p_b_useOKlab":true,"p_f_ghostingPercent":97.2,"preset_name":"CRT \\" laboratory"})", s, error) == false, "malformed escape accepted");
        require(deserialize_preset(R"({"p_v_zoomX":1.3,"p_v_warpY":-0.1,"p_v_reflectionTranslationX":0.17,"p_b_useOKlab":true,"p_f_ghostingPercent":97.2,"preset_name":"CRT \" laboratory"})", s, error), "SuperDuperDisplay vector/scalar schema rejected");
        const auto saved = serialize_preset(s);
        Settings restored;
        require(deserialize_preset(saved, restored, error), "roundtrip failed");
        require(restored.preset_name == "CRT \" laboratory" && restored.p_b_useOKlab &&
            std::abs(restored.p_v_zoom[0] - 1.3f) < 1e-6 &&
            std::abs(restored.p_v_warp[1] + .1f) < 1e-6 &&
            std::abs(restored.p_v_reflectionTranslation[0] - .17f) < 1e-6 &&
            std::abs(restored.p_f_ghostingPercent - 97.2f) < 1e-5, "roundtrip changed visual settings");
        for (const char* invalid : {"[]", "null", "{", R"({"p_f_brightness":"1"})",
            R"({"p_b_slot":1})", R"({"p_i_maskType":0.5})", R"({"p_i_cSpace":4})",
            R"({"p_f_maskSize":0})", R"({"p_f_brightness":1e400})",
            R"({"preset_name":null})", R"({"p_f_brightness":2,"p_v_zoomY":0})"}) {
            require(!deserialize_preset(invalid, restored, error), "invalid preset accepted");
            require(!error.empty() && serialize_preset(restored) == saved, "failed import mutated settings");
        }
        require(deserialize_preset(R"({"future_setting":{"anything":[1,2,3]},"p_f_contrast":1.5})", s, error), "forward-compatible preset rejected");
        require(s.p_f_contrast == 1.5f, "known field was ignored beside unknown field");
        require(!deserialize_preset(std::string(1024*1024+1, ' '), s, error), "oversize input accepted");
        if (argc > 1) {
            int count = 0;
            for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
                if (file.path().extension() != ".json") continue;
                require(load_preset(file.path().string(), s, error), (file.path().string() + ": " + error).c_str());
                ++count;
            }
            require(count >= 6, "bundled SuperDuperDisplay presets missing");
            std::cout << count << " bundled SuperDuperDisplay presets loaded\n";
        }
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto dir = std::filesystem::temp_directory_path() / ("gs2-preset-test-" + std::to_string(unique));
        std::filesystem::create_directory(dir);
        const auto file = dir / "preset.json";
        s.preset_name = "first";
        require(save_preset(file.string(), s, error), "initial save failed");
        s.preset_name = "replacement";
        require(save_preset(file.string(), s, error), "replacement save failed");
        require(load_preset(file.string(), restored, error) && restored.preset_name == "replacement", "replacement was not persisted");
        require(!std::filesystem::exists(file.string()+".tmp"), "temporary save leaked");
        std::filesystem::remove_all(dir);
        std::cout << "Postprocessing preset contract tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
