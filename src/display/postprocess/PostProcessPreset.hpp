#pragma once

#include "PostProcessSettings.hpp"
#include <functional>
#include <string>
#include <vector>

namespace gs2::postprocess {

enum class ParameterKind { Number, Integer, Boolean };
struct Parameter {
    const char* key;
    const char* label;
    const char* group;
    ParameterKind kind;
    double minimum, maximum, step;
    std::function<double(const Settings&)> get;
    std::function<void(Settings&, double)> set;
};

// One schema drives the portable controls, validation and SuperDuperDisplay JSON keys.
const std::vector<Parameter>& parameters();
std::string serialize_preset(const Settings& settings);
// Transactional: an invalid document leaves settings unchanged. Unknown keys
// are ignored so presets from later SuperDuperDisplay versions remain usable.
bool deserialize_preset(const std::string& json, Settings& settings, std::string& error);
bool load_preset(const std::string& path, Settings& settings, std::string& error);
bool save_preset(const std::string& path, const Settings& settings, std::string& error);

} // namespace gs2::postprocess
