#include "PostProcessPreset.hpp"
#include "../../../third_party/nlohmann/json.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace gs2::postprocess {

const std::vector<Parameter>& parameters() {
#define FIELD(member, label, group, kind, lo, hi, step) \
    {#member, label, group, ParameterKind::kind, lo, hi, step, \
     [](const Settings& s) { return static_cast<double>(s.member); }, \
     [](Settings& s, double v) { s.member = static_cast<decltype(s.member)>(v); }}
#define AXIS(member, axis, key, label, group, lo, hi, step) \
    {key, label, group, ParameterKind::Number, lo, hi, step, \
     [](const Settings& s) { return static_cast<double>(s.member[axis]); }, \
     [](Settings& s, double v) { s.member[axis] = static_cast<float>(v); }}
    static const std::vector<Parameter> fields = {
        FIELD(p_i_postprocessingLevel, "Effects: Off / Scanlines / Full", "Display", Integer, 0, 2, 1),
        FIELD(bAutoScale, "Automatic scale", "Display", Boolean, 0, 1, 1),
        FIELD(integer_scale, "Integer scale", "Display", Integer, 1, 32, 1),
        FIELD(bCRTFillWindow, "Fill window", "Display", Boolean, 0, 1, 1),
        AXIS(p_v_zoom, 0, "p_v_zoomX", "Image zoom X", "Display", .001, 5, .001),
        AXIS(p_v_zoom, 1, "p_v_zoomY", "Image zoom Y", "Display", .001, 5, .001),
        AXIS(p_v_center, 0, "p_v_centerX", "Image center X", "Display", -100, 100, .1),
        AXIS(p_v_center, 1, "p_v_centerY", "Image center Y", "Display", -100, 100, .1),
        FIELD(bHalveFramerate, "Merge frame pairs", "Temporal", Boolean, 0, 1, 1),
        FIELD(p_f_ghostingPercent, "Phosphor persistence (%)", "Temporal", Number, 0, 100, .1),
        FIELD(p_f_phosphorBlur, "Phosphor blur", "Temporal", Number, 0, 2, .01),
        FIELD(p_b_phosphorGlow, "Phosphor glow", "Temporal", Boolean, 0, 1, 1),
        FIELD(p_i_scanlineType, "Scanlines: None / Simple / Complex", "Scanlines", Integer, 0, 2, 1),
        FIELD(p_f_scanlineWeight, "Scanline weight", "Scanlines", Number, 0, 2, .01),
        FIELD(p_f_scanSpeed, "Scan speed", "Scanlines", Number, 0, 2, .01),
        FIELD(p_f_filmGrain, "Film grain", "Scanlines", Number, 0, 1, .01),
        FIELD(p_f_vignetteWeight, "Vignette", "Scanlines", Number, 0, 5, .01),
        FIELD(p_f_interlace, "Interlacing", "Scanlines", Number, 0, 2, .01),
        FIELD(p_i_maskType, "Mask: None / CGWG / RGB", "Mask", Integer, 0, 2, 1),
        FIELD(p_f_maskSize, "Mask size", "Mask", Number, .001, 20, .001),
        FIELD(p_b_slot, "Slot mask", "Mask", Boolean, 0, 1, 1),
        FIELD(p_f_slotW, "Slot mask width", "Mask", Number, 2, 3, .1),
        FIELD(p_f_bgr, "Subpixels RGB to BGR", "Mask", Number, 0, 1, .1),
        FIELD(p_f_maskLow, "Dark mask brightness", "Mask", Number, 0, 1, .01),
        FIELD(p_f_maskHigh, "Bright mask brightness", "Mask", Number, 0, 1, .01),
        AXIS(p_v_warp, 0, "p_v_warpX", "Curvature X", "Shape", -.5, .5, .001),
        AXIS(p_v_warp, 1, "p_v_warpY", "Curvature Y", "Shape", -.5, .5, .001),
        FIELD(p_f_barrelDistortion, "Barrel distortion", "Shape", Number, -.3, 5, .01),
        FIELD(p_f_corner, "Corner cut", "Shape", Number, 0, 100, .01),
        FIELD(p_b_smoothCorner, "Smooth corners", "Shape", Boolean, 0, 1, 1),
        FIELD(p_b_useOKlab, "Use OKLab color", "Color", Boolean, 0, 1, 1),
        FIELD(p_f_brightness, "Brightness", "Color", Number, 0, 100, .01),
        FIELD(p_f_contrast, "Contrast", "Color", Number, 0, 100, .01),
        FIELD(p_f_black, "Black level", "Color", Number, -1, 1, .01),
        FIELD(p_f_saturation, "Saturation", "Color", Number, 0, 100, .01),
        FIELD(p_f_hue, "Hue", "Color", Number, -3.141593, 3.141593, .001),
        FIELD(p_f_hueRG, "Green / red hue", "Color", Number, -2.5, 2.5, .01),
        FIELD(p_f_hueRB, "Blue / red hue", "Color", Number, -2.5, 2.5, .01),
        FIELD(p_f_hueGB, "Blue / green hue", "Color", Number, -2.5, 2.5, .01),
        FIELD(p_f_brDep, "Mask brightness dependence", "Color", Number, 0, .5, .001),
        FIELD(p_i_cSpace, "Color: sRGB / PAL / NTSC-U / NTSC-J", "Color", Integer, 0, 3, 1),
        FIELD(p_f_cStr, "Convergence strength", "Convergence", Number, 0, .5, .01),
        FIELD(p_f_convR, "Red convergence", "Convergence", Number, -3, 3, .01),
        FIELD(p_f_convG, "Green convergence", "Convergence", Number, -3, 3, .01),
        FIELD(p_f_convB, "Blue convergence", "Convergence", Number, -3, 3, .01),
        FIELD(bezelWidth, "Bezel width", "Bezel", Number, .0001, 300, .0001),
        FIELD(bezelHeight, "Bezel height", "Bezel", Number, .0001, 300, .0001),
        FIELD(bezelCenterX, "Bezel center X", "Bezel", Number, -300, 300, .1),
        FIELD(bezelCenterY, "Bezel center Y", "Bezel", Number, -300, 300, .1),
        FIELD(p_f_glassThickness, "Glass thickness", "Bezel", Number, 0, 2, .01),
        FIELD(p_f_bezelReflection, "Bezel reflection", "Bezel", Number, 0, .5, .001),
        FIELD(p_f_reflectionBlur, "Reflection blur", "Bezel", Number, 0, 10, .001),
        AXIS(p_v_reflectionScale, 0, "p_v_reflectionScaleX", "Reflection scale X", "Bezel", .001, 5, .001),
        AXIS(p_v_reflectionScale, 1, "p_v_reflectionScaleY", "Reflection scale Y", "Bezel", .001, 5, .001),
        AXIS(p_v_reflectionTranslation, 0, "p_v_reflectionTranslationX", "Reflection center X", "Bezel", -4, 4, .001),
        AXIS(p_v_reflectionTranslation, 1, "p_v_reflectionTranslationY", "Reflection center Y", "Bezel", -4, 4, .001),
        FIELD(p_b_outlineQuad, "Show reflection bounds", "Bezel", Boolean, 0, 1, 1),
    };
#undef FIELD
#undef AXIS
    return fields;
}

std::string serialize_preset(const Settings& s) {
    nlohmann::json j = {{"preset_name", s.preset_name}, {"bezelName", s.bezelName}, {"glassName", s.glassName}};
    for (const auto& p : parameters()) {
        const double v = p.get(s);
        if (p.kind == ParameterKind::Boolean) j[p.key] = v != 0;
        else if (p.kind == ParameterKind::Integer) j[p.key] = static_cast<int>(v);
        else j[p.key] = v;
    }
    // SuperDuperDisplay 0.9.1's writer misspells this key, while its reader uses the correct one.
    j["p_f_flimGrain"] = s.p_f_filmGrain;
    return j.dump(4) + "\n";
}

bool deserialize_preset(const std::string& text, Settings& settings, std::string& error) {
    error.clear();
    if (text.size() > 1024 * 1024) { error = "Preset exceeds 1 MB"; return false; }
    try {
        auto j = nlohmann::json::parse(text);
        if (!j.is_object()) { error = "Preset must be a JSON object"; return false; }
        Settings candidate{};
        if (!j.contains("p_f_filmGrain") && j.contains("p_f_flimGrain"))
            j["p_f_filmGrain"] = j["p_f_flimGrain"];
        for (const auto& p : parameters()) {
            const auto it = j.find(p.key);
            if (it == j.end()) continue;
            double value;
            if (p.kind == ParameterKind::Boolean) {
                if (!it->is_boolean()) { error = std::string(p.key) + " must be a boolean"; return false; }
                value = it->get<bool>() ? 1 : 0;
            } else {
                if (!it->is_number()) { error = std::string(p.key) + " must be a number"; return false; }
                value = it->get<double>();
                if (!std::isfinite(value) || value < p.minimum - 1e-6 || value > p.maximum + 1e-6 ||
                    (p.kind == ParameterKind::Integer && std::trunc(value) != value)) {
                    error = std::string(p.key) + " is outside its supported range"; return false;
                }
            }
            p.set(candidate, std::max(p.minimum, std::min(p.maximum, value)));
        }
        for (auto entry : {std::pair{"preset_name", &candidate.preset_name},
                           std::pair{"bezelName", &candidate.bezelName}, std::pair{"glassName", &candidate.glassName}}) {
            if (!j.contains(entry.first)) continue;
            if (!j[entry.first].is_string()) { error = std::string(entry.first) + " must be text"; return false; }
            *entry.second = j[entry.first].get<std::string>();
            if (entry.second->size() > 4096 || entry.second->find('\0') != std::string::npos) {
                error = std::string(entry.first) + " is invalid"; return false;
            }
        }
        settings = std::move(candidate);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool load_preset(const std::string& path, Settings& settings, std::string& error) {
    std::error_code ec;
    const auto length = std::filesystem::file_size(path, ec);
    if (ec || length > 1024 * 1024) { error = ec ? "Cannot read preset: " + ec.message() : "Preset exceeds 1 MB"; return false; }
    std::ifstream in(path, std::ios::binary);
    std::string text(static_cast<size_t>(length), '\0');
    if (!in.read(text.data(), static_cast<std::streamsize>(length))) { error = "Cannot read preset"; return false; }
    return deserialize_preset(text, settings, error);
}

bool save_preset(const std::string& path, const Settings& settings, std::string& error) {
    error.clear();
    const auto temp = std::filesystem::path(path).concat(".tmp");
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Cannot open preset for writing"; return false; }
        out << serialize_preset(settings);
        out.close();
        if (!out) { error = "Cannot write preset"; std::filesystem::remove(temp); return false; }
        // rename replaces atomically on POSIX; Windows needs its replacement API.
#ifdef _WIN32
        if (!MoveFileExW(temp.wstring().c_str(), std::filesystem::u8path(path).wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { error = "Cannot replace preset"; std::filesystem::remove(temp); return false; }
#else
        std::filesystem::rename(temp, path);
#endif
        return true;
    } catch (const std::exception& e) {
        error = e.what(); std::error_code ignored; std::filesystem::remove(temp, ignored); return false;
    }
}

} // namespace gs2::postprocess
