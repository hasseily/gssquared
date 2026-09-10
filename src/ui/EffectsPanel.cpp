#include "EffectsPanel.hpp"
#include "Button.hpp"
#include "videosystem.hpp"
#include "platform-specific/emscripten/web_file_dialog.hpp"
#include <algorithm>
#include <filesystem>

using namespace gs2::postprocess;

EffectsPanel_t::EffectsPanel_t(UIContext* ctx, video_system_t* video, modal_stack& stack)
    : ModalContainer_t(ctx, "", stack), video_(video), settings_(video->postprocess_settings()) {
    style.background_color = 0xF7FAFCFF; style.border_color = 0x1A789EFF;
    style.border_width = 2; style.text_color = 0x102030FF;
    Style_t buttons;
    buttons.background_color = 0xD9EAF4FF; buttons.hover_color = 0xA9D2E8FF;
    buttons.text_color = 0x102030FF; buttons.border_color = 0x7395AAFF;
    buttons.border_width = 1; buttons.padding = 2;
    const auto button = [&](const char* label, std::function<void()> fn) {
        auto* b = new Button_t(ctx, label, buttons);
        b->on_click([fn](const SDL_Event&) { fn(); return true; }); add(b); return b;
    };
    actions_.push_back(button("Previous", [this] { select_preset(-1); }));
    actions_.push_back(button("Next preset", [this] { select_preset(1); }));
    actions_.push_back(button("Save new", [this] {
        finish_edits();
        if (save_named_preset(settings_, message_)) { message_ = "Preset saved"; presets_ = available_presets(); }
    }));
    actions_.push_back(button("Import", [this] { choose_file(false); }));
    actions_.push_back(button("Export", [this] { choose_file(true); }));
    actions_.push_back(button("Reset", [this] {
        finish_edits(); settings_.reset_to_defaults(); name_->set_text(settings_.preset_name); changed();
    }));
    close_ = button("Done", [this] { finish_edits(); persist(); completed = true; });
    bezel_prev_ = button("<", [this] { select_bezel(-1); });
    bezel_next_ = button(">", [this] { select_bezel(1); });
    name_ = new TextInput_t(ctx, settings_.preset_name, buttons);
    name_->set_text_renderer(ctx->text_render); name_->set_max_length(100);
    name_->set_enter_handler([this](const SDL_Event&) {
        settings_.preset_name = name_->get_text(); name_->set_edit_active(false); changed(); return true;
    });
    add(name_);
    scroll_ = new ScrollBar_t(ctx, buttons); scroll_->set_origin(ScrollBarOrigin::Top);
    scroll_->on_change([this](int n) {
        for (auto* row : rows_) row->finish_edit(); first_row_ = n; layout();
    }); add(scroll_);
    for (const auto& parameter : parameters()) {
        auto* row = new NumericSlider_t(ctx, parameter, settings_, [this] { changed(); });
        rows_.push_back(row); add(row);
    }
    presets_ = available_presets(); bezels_ = available_bezels();
    loaded_bezel_ = settings_.bezelName; loaded_glass_ = settings_.glassName;
    video_->push_mouse_capture(false);
    layout();
}

EffectsPanel_t::~EffectsPanel_t() { if (dirty_) persist(); video_->pop_mouse_capture(); }

void EffectsPanel_t::layout() {
    SDL_GetWindowSize(ctx->window, &width_, &height_);
    const int w = std::max(300, std::min(600, width_ - 24));
    const int h = std::max(300, std::min(760, height_ - 24));
    // Leave the guest picture visible beside the live controls.
    set_position(width_ - w - 12.f, (height_ - h) / 2.f); size(w, h);
    name_->set_position(tp.x + 84, tp.y + 48); name_->size(w - 112, 28);
    const int columns = w >= 780 ? 6 : 3;
    const float button_width = (w - 36.f) / columns - 6;
    for (size_t i = 0; i < actions_.size(); ++i) {
        actions_[i]->set_position(tp.x + 16 + (i % columns) * (button_width + 6), tp.y + 86 + (i / columns) * 34);
        actions_[i]->size(button_width, 28);
    }
    const int bezel_y = 86 + ((6 + columns - 1) / columns) * 34;
    bezel_prev_->set_position(tp.x + w - 92, tp.y + bezel_y); bezel_prev_->size(32, 26);
    bezel_next_->set_position(tp.x + w - 54, tp.y + bezel_y); bezel_next_->size(32, 26);
    controls_y_ = bezel_y + 36;
    page_rows_ = std::max(1, (h - controls_y_ - 82) / 54);
    first_row_ = std::clamp(first_row_, 0, std::max(0, static_cast<int>(rows_.size()) - page_rows_));
    for (size_t i = 0; i < rows_.size(); ++i) {
        const int offset = static_cast<int>(i) - first_row_;
        const bool show = offset >= 0 && offset < page_rows_;
        rows_[i]->set_visible(show);
        if (show) {
            rows_[i]->set_position(tp.x + 16, tp.y + controls_y_ + offset * 54);
            rows_[i]->size(w - 58, 49);
        }
    }
    scroll_->set_position(tp.x + w - 30, tp.y + controls_y_);
    scroll_->size(14, page_rows_ * 54 - 5); scroll_->set_range(rows_.size(), page_rows_);
    scroll_->set_position(first_row_);
    close_->set_position(tp.x + w - 118, tp.y + h - 42); close_->size(98, 28);
}

void EffectsPanel_t::changed() {
    video_->postprocess_settings_changed();
    if (loaded_bezel_ != settings_.bezelName || loaded_glass_ != settings_.glassName) {
        std::string bezel, glass; resolve_assets(settings_, bezel, glass);
        video_->set_postprocess_assets(bezel, glass);
        loaded_bezel_ = settings_.bezelName; loaded_glass_ = settings_.glassName;
    }
    dirty_ = true; changed_at_ = SDL_GetTicks(); message_ = "Preview updated";
}
void EffectsPanel_t::finish_edits() {
    for (auto* row : rows_) row->finish_edit();
    if (settings_.preset_name != name_->get_text()) {
        settings_.preset_name = name_->get_text(); changed();
    }
    name_->set_edit_active(false);
}
void EffectsPanel_t::persist() {
    if (save_current_settings(settings_, message_)) { dirty_ = false; message_ = "Settings saved"; }
}
void EffectsPanel_t::select_preset(int direction) {
    if (presets_.empty()) { message_ = "No presets found"; return; }
    finish_edits();
    if (preset_index_ < 0 && direction < 0) preset_index_ = 0;
    preset_index_ = (preset_index_ + direction + static_cast<int>(presets_.size())) % presets_.size();
    if (load_preset(presets_[preset_index_].path, settings_, message_)) {
        name_->set_text(settings_.preset_name); changed();
    }
}
void EffectsPanel_t::select_bezel(int direction) {
    const auto it = std::find(bezels_.begin(), bezels_.end(), settings_.bezelName);
    int index = it == bezels_.end() ? 0 : static_cast<int>(it - bezels_.begin());
    index = (index + direction + static_cast<int>(bezels_.size())) % bezels_.size();
    settings_.bezelName = bezels_[index]; settings_.glassName.clear(); changed();
}

void SDLCALL EffectsPanel_t::file_selected(void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Selection>> owner(static_cast<std::shared_ptr<Selection>*>(data));
    std::lock_guard<std::mutex> lock((*owner)->mutex);
    if (!files) (*owner)->error = SDL_GetError();
    else if (files[0]) (*owner)->path = files[0];
    (*owner)->ready = true;
}
void EffectsPanel_t::choose_file(bool save) {
    finish_edits();
    if (selection_) { message_ = "Finish the open file dialog first"; return; }
#ifdef __EMSCRIPTEN__
    if (save) { export_browser_preset(settings_); message_ = "Preset exported"; return; }
#endif
    selection_ = std::make_shared<Selection>(); selection_->save = save;
    auto* context = new std::shared_ptr<Selection>(selection_);
    static const SDL_DialogFileFilter filter{"Postprocessing preset", "json"};
#ifdef __EMSCRIPTEN__
    web_open_file_dialog(file_selected, context, ".json");
#else
    if (save) SDL_ShowSaveFileDialog(file_selected, context, ctx->window, &filter, 1, "GSSquared-effects.json");
    else SDL_ShowOpenFileDialog(file_selected, context, ctx->window, &filter, 1, nullptr, false);
#endif
    message_ = save ? "Choose where to export the preset" : "Choose a postprocessing JSON preset";
}

void EffectsPanel_t::update() {
    int w, h; SDL_GetWindowSize(ctx->window, &w, &h);
    if (w != width_ || h != height_) layout();
    if (selection_) {
        bool ready, save; std::string path, error;
        { std::lock_guard<std::mutex> lock(selection_->mutex);
          ready = selection_->ready; save = selection_->save; path = selection_->path; error = selection_->error; }
        if (ready) {
            selection_.reset();
            if (!error.empty()) message_ = error;
            else if (path.empty()) message_ = "Canceled";
            else if (save) {
                if (std::filesystem::path(path).extension().empty()) path += ".json";
                if (save_preset(path, settings_, message_)) message_ = "Preset exported";
            } else if (load_preset(path, settings_, message_)) { name_->set_text(settings_.preset_name); changed(); }
        }
    }
    if (dirty_ && SDL_GetTicks() - changed_at_ >= 400) persist();
    Container_t::update();
}

void EffectsPanel_t::render() {
    Container_t::render();
    ctx->text_render->set_color(0x10, 0x20, 0x30, 0xFF);
    ctx->text_render->render("Postprocessing", tp.x + 16, tp.y + 12);
    ctx->text_render->render("Name", tp.x + 16, tp.y + 52);
    const auto fit = [this](std::string text, float width) {
        if (ctx->text_render->string_width(text) <= width) return text;
        while (!text.empty() && ctx->text_render->string_width(text + "...") > width) text.pop_back();
        return text + "...";
    };
    ctx->text_render->render(fit("Bezel: " + settings_.bezelName, tp.w - 124), tp.x + 16, tp.y + controls_y_ - 32);
    std::string status = persistence_error();
    if (status.empty()) status = !video_->postprocess_available() ? video_->postprocess_status() : message_;
    ctx->text_render->render(fit(status, tp.w - 32), tp.x + 16, tp.y + tp.h - 72);
    ctx->text_render->render(fit("Scroll for controls. Enter applies. Esc closes.", tp.w - 152), tp.x + 16, tp.y + tp.h - 35);
}

bool EffectsPanel_t::handle_mouse_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        finish_edits(); persist(); completed = true; return true;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        scroll_->scroll_by(event.wheel.y > 0 ? -3 : 3); return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !name_->is_edit_active()) {
        if (event.key.key == SDLK_PAGEDOWN) { scroll_->scroll_by(page_rows_); return true; }
        if (event.key.key == SDLK_PAGEUP) { scroll_->scroll_by(-page_rows_); return true; }
    }
    // Let every numeric editor see clicks outside its bounds to commit/blur it.
    bool handled = false;
    for (auto* row : rows_) handled = row->handle_mouse_event(event) || handled;
    handled = name_->handle_mouse_event(event) || handled;
    if (handled) return true;
    if (scroll_->handle_mouse_event(event)) return true;
    for (auto* b : actions_) if (b->handle_mouse_event(event)) return true;
    if (bezel_prev_->handle_mouse_event(event) || bezel_next_->handle_mouse_event(event)) return true;
    return close_->handle_mouse_event(event);
}
