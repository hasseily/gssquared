#include "PresetStore.hpp"
#include "paths.hpp"
#include <algorithm>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace gs2::postprocess {
namespace fs = std::filesystem;

std::string preset_directory() {
#ifdef __EMSCRIPTEN__
    return "/postprocess/presets";
#else
    std::string path;
    Paths::calc_pref(path, "postprocess/presets");
    return path;
#endif
}
std::string current_preset_path() { return (fs::path(preset_directory()).parent_path() / "current.json").string(); }
std::string asset_directory() { std::string path; Paths::calc_base(path, "postprocess"); return path; }

std::vector<PresetFile> available_presets() {
    std::vector<PresetFile> result;
    for (bool bundled : {true, false}) {
        const fs::path dir = bundled ? fs::path(asset_directory()) / "presets" : fs::path(preset_directory());
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".json") {
                Settings s; std::string error;
                if (load_preset(it->path().string(), s, error))
                    result.push_back({s.preset_name.empty() ? it->path().stem().string() : s.preset_name,
                                      it->path().string(), bundled});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.bundled != b.bundled ? a.bundled > b.bundled : a.path < b.path;
    });
    return result;
}

std::vector<std::string> available_bezels() {
    std::vector<std::string> names{"NONE"};
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(asset_directory()) / "bezels", ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (it->is_regular_file(ec) && name.find(".glass.") == std::string::npos &&
            (it->path().extension() == ".png" || it->path().extension() == ".jpg")) names.push_back(name);
    }
    std::sort(names.begin() + 1, names.end());
    return names;
}

bool load_current_settings(Settings& s, std::string& error) {
    error.clear();
    std::error_code ec;
    if (!fs::exists(current_preset_path(), ec)) return !ec;
    return load_preset(current_preset_path(), s, error);
}

static void sync_browser_settings() {
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM({
        if (!Module.gs2PostprocessPersistent) return;
        Module.gs2PostprocessSyncDirty = true;
        if (Module.gs2PostprocessSyncBusy) return;
        var sync = function () {
            Module.gs2PostprocessSyncBusy = true;
            Module.gs2PostprocessSyncDirty = false;
            FS.syncfs(false, function (error) {
                Module.gs2PostprocessSyncBusy = false;
                Module.gs2PostprocessStorageError = error ? 'Browser preset save failed: ' + error : '';
                if (Module.gs2PostprocessSyncDirty) sync();
            });
        };
        sync();
    });
#endif
}

bool save_current_settings(const Settings& s, std::string& error) {
    std::error_code ec;
    fs::create_directories(preset_directory(), ec);
    if (ec) { error = ec.message(); return false; }
    if (!save_preset(current_preset_path(), s, error)) return false;
    sync_browser_settings();
    return true;
}

bool save_named_preset(const Settings& s, std::string& error) {
    std::string filename;
    // The preset display name is arbitrary UTF-8; the filename stays portable.
    for (unsigned char c : s.preset_name) {
        if (filename.size() >= 80) break;
        filename += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' ? static_cast<char>(c) : '_';
    }
    if (filename.empty()) filename = "Untitled";
    std::error_code ec; fs::create_directories(preset_directory(), ec);
    if (ec) { error = ec.message(); return false; }
    // Save creates a new preset. Repeated names never replace an unrelated file.
    auto path = fs::path(preset_directory()) / (filename + ".json");
    for (unsigned n = 2; fs::exists(path, ec); ++n) path = fs::path(preset_directory()) / (filename + "-" + std::to_string(n) + ".json");
    if (ec) { error = ec.message(); return false; }
    if (!save_preset(path.string(), s, error)) return false;
    sync_browser_settings();
    return true;
}

void resolve_assets(const Settings& s, std::string& bezel, std::string& glass) {
    bezel.clear(); glass.clear();
    if (s.bezelName.empty() || s.bezelName == "NONE") return;
    const auto resolve = [](const std::string& name) {
        // Presets refer to packaged images by name; explicit imported image
        // paths are retained when the user selected a local asset.
        const fs::path p = fs::u8path(name);
        return p.is_absolute() ? p : fs::path(asset_directory()) / "bezels" / p.filename();
    };
    const auto image = resolve(s.bezelName);
    bezel = image.string();
    const auto companion = image.parent_path() / (image.stem().string() + ".glass" + image.extension().string());
    std::error_code ec;
    if (!s.glassName.empty()) glass = resolve(s.glassName).string();
    else if (fs::is_regular_file(companion, ec)) glass = companion.string();
}

std::string persistence_error() {
#ifdef __EMSCRIPTEN__
    char* error = reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        return stringToNewUTF8(Module.gs2PostprocessStorageError || '');
    }));
    std::string result = error ? error : "";
    free(error);
    return result;
#else
    return {};
#endif
}

void export_browser_preset(const Settings& s) {
#ifdef __EMSCRIPTEN__
    const auto text = serialize_preset(s);
    MAIN_THREAD_EM_ASM({
        var blob = new Blob([UTF8ToString($0)], {type: 'application/json'});
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url; a.download = 'GSSquared-effects.json';
        document.body.appendChild(a); a.click(); a.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    }, text.c_str());
#endif
}
} // namespace gs2::postprocess
