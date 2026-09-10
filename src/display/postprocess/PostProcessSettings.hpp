#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace gs2::postprocess {

// SuperDuperDisplay 0.9.1 parameter names are retained so its JSON presets remain portable.
// Backend objects and UI state deliberately do not belong to this value type.
struct Settings {
    std::string preset_name;
    std::string bezelName = "NONE";
    std::string glassName;
    bool bAutoScale = true;
    bool bCRTFillWindow = false;
    bool bHalveFramerate = false;
    int integer_scale = 1;
    float bezelWidth = 1.0f, bezelHeight = 1.0f;
    float bezelCenterX = 0.0f, bezelCenterY = 0.0f;
    bool p_b_useOKlab = false;
    bool p_b_smoothCorner = false;
    bool p_b_slot = false;
    bool p_b_phosphorGlow = false;
    bool p_b_outlineQuad = false;
    float p_f_barrelDistortion = 0.0f;
    float p_f_bgr = 0.0f;
    float p_f_black = 0.0f;
    float p_f_brDep = 0.2f;
    float p_f_brightness = 1.0f;
    float p_f_contrast = 1.0f;
    float p_f_convB = 0.0f, p_f_convG = 0.0f, p_f_convR = 0.0f;
    float p_f_corner = 0.0f, p_f_cStr = 0.0f;
    float p_f_hue = 0.0f, p_f_hueGB = 0.0f, p_f_hueRB = 0.0f, p_f_hueRG = 0.0f;
    float p_f_maskHigh = 0.75f, p_f_maskLow = 0.3f, p_f_maskSize = 1.0f;
    float p_f_saturation = 1.0f;
    float p_f_scanlineWeight = 1.0f, p_f_scanSpeed = 0.0f;
    float p_f_filmGrain = 0.0f, p_f_interlace = 0.0f;
    float p_f_slotW = 3.0f, p_f_vignetteWeight = 0.0f;
    int p_i_cSpace = 0, p_i_maskType = 0;
    int p_i_postprocessingLevel = 0, p_i_scanlineType = 2;
    float p_f_ghostingPercent = 0.0f, p_f_phosphorBlur = 0.0f;
    std::array<float, 2> p_v_warp{0.0f, 0.0f};
    std::array<float, 2> p_v_center{0.0f, 0.0f};
    std::array<float, 2> p_v_zoom{1.0f, 1.0f};
    float p_f_bezelReflection = 0.0f, p_f_reflectionBlur = 0.0f;
    float p_f_glassThickness = 1.0f;
    std::array<float, 2> p_v_reflectionScale{1.0f, 1.0f};
    std::array<float, 2> p_v_reflectionTranslation{0.0f, 0.0f};

    void reset_to_defaults() { *this = Settings{}; }
};

using PostProcessSettings = Settings;

} // namespace gs2::postprocess
