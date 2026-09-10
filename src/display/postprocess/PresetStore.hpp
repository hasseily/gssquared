#pragma once
#include "PostProcessPreset.hpp"

namespace gs2::postprocess {
struct PresetFile { std::string name, path; bool bundled; };
std::string preset_directory();
std::string current_preset_path();
std::string asset_directory();
std::vector<PresetFile> available_presets();
std::vector<std::string> available_bezels();
bool load_current_settings(Settings& settings, std::string& error);
bool save_current_settings(const Settings& settings, std::string& error);
bool save_named_preset(const Settings& settings, std::string& error);
void resolve_assets(const Settings& settings, std::string& bezel, std::string& glass);
// Browser sync is asynchronous. Empty means no recorded persistence error.
std::string persistence_error();
void export_browser_preset(const Settings& settings);
} // namespace gs2::postprocess
