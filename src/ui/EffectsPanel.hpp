#pragma once
#include "ModalContainer.hpp"
#include "Button.hpp"
#include "NumericSlider.hpp"
#include "ScrollBar.hpp"
#include "display/postprocess/PresetStore.hpp"
#include <memory>
#include <mutex>

struct video_system_t;
class EffectsPanel_t : public ModalContainer_t {
public:
    EffectsPanel_t(UIContext*, video_system_t*, modal_stack&);
    ~EffectsPanel_t() override;
    void layout() override;
    void render() override;
    void update() override;
    bool handle_mouse_event(const SDL_Event&) override;
private:
    struct Selection {
        std::mutex mutex;
        std::string path, error;
        bool ready = false, save = false;
    };
    video_system_t* video_;
    gs2::postprocess::Settings& settings_;
    std::vector<NumericSlider_t*> rows_;
    std::vector<Button_t*> actions_;
    TextInput_t* name_;
    ScrollBar_t* scroll_;
    Button_t *close_, *bezel_prev_, *bezel_next_;
    std::vector<gs2::postprocess::PresetFile> presets_;
    std::vector<std::string> bezels_;
    std::shared_ptr<Selection> selection_;
    std::string message_, loaded_bezel_, loaded_glass_;
    int preset_index_ = -1, first_row_ = 0, page_rows_ = 1;
    int width_ = 0, height_ = 0, controls_y_ = 0;
    bool dirty_ = false;
    uint64_t changed_at_ = 0;
    void changed(bool reload_assets = false);
    void persist();
    void select_preset(int direction);
    void select_bezel(int direction);
    void choose_file(bool save);
    void finish_edits();
    static void SDLCALL file_selected(void*, const char* const*, int);
};
