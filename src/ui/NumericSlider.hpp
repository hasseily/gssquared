#pragma once
#include "Tile.hpp"
#include "TextInput.hpp"
#include "display/postprocess/PostProcessPreset.hpp"
#include <memory>

// A labeled, keyboard-editable numeric control shared by every host platform.
class NumericSlider_t : public Tile_t {
public:
    NumericSlider_t(UIContext*, const gs2::postprocess::Parameter&,
                    gs2::postprocess::Settings&, std::function<void()> changed);
    void render() override;
    bool handle_mouse_event(const SDL_Event&) override;
    void finish_edit();
private:
    const gs2::postprocess::Parameter& parameter_;
    gs2::postprocess::Settings& settings_;
    std::function<void()> changed_;
    std::unique_ptr<TextInput_t> input_;
    bool dragging_ = false, invalid_ = false;
    SDL_FRect track() const;
    void assign(double);
    bool commit_text();
    void update_geometry();
};
