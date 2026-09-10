#include "NumericSlider.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace gs2::postprocess;

NumericSlider_t::NumericSlider_t(UIContext* ctx, const Parameter& parameter,
                               Settings& settings, std::function<void()> changed)
    : Tile_t(ctx), parameter_(parameter), settings_(settings), changed_(std::move(changed)) {
    Style_t style;
    style.background_color = 0xFFFFFFFF; style.text_color = 0x102030FF;
    style.border_color = 0x90A8B8FF; style.border_width = 1; style.padding = 2;
    input_ = std::make_unique<TextInput_t>(ctx, "", style);
    input_->set_text_renderer(ctx->text_render);
    input_->set_max_length(18);
    input_->set_enter_handler([this](const SDL_Event&) { if (commit_text()) input_->set_edit_active(false); return true; });
    size(700, 48);
}

SDL_FRect NumericSlider_t::track() const { return {tp.x + 6, tp.y + 31, std::max(30.f, tp.w - 128), 8}; }
void NumericSlider_t::update_geometry() {
    input_->set_position(tp.x + tp.w - 106, tp.y + 23);
    input_->size(102, 24);
}
void NumericSlider_t::assign(double value) {
    value = std::clamp(value, parameter_.minimum, parameter_.maximum);
    if (parameter_.kind != ParameterKind::Number) value = std::round(value);
    if (parameter_.get(settings_) == value) return;
    parameter_.set(settings_, value); invalid_ = false; changed_();
}
bool NumericSlider_t::commit_text() {
    char* end = nullptr;
    const auto text = input_->get_text();
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value) || value < parameter_.minimum ||
        value > parameter_.maximum || (parameter_.kind == ParameterKind::Integer && std::trunc(value) != value)) {
        invalid_ = true; return false;
    }
    assign(value); invalid_ = false; return true;
}
void NumericSlider_t::finish_edit() {
    if (input_->is_edit_active()) commit_text();
    input_->set_edit_active(false); dragging_ = false;
}
void NumericSlider_t::render() {
    if (!visible) return;
    update_geometry();
    ctx->fill_rect({tp.x, tp.y, tp.w, tp.h}, 0xEAF0F4FF);
    const std::string label = std::string(parameter_.group) + " / " + parameter_.label;
    ctx->text_render->set_color(0x10, 0x20, 0x30, 0xFF);
    ctx->text_render->render(label, tp.x + 5, tp.y + 1);
    const double value = parameter_.get(settings_);
    if (parameter_.kind == ParameterKind::Boolean) {
        ctx->text_render->set_color(0x00, 0x60, 0x80, 0xFF);
        ctx->text_render->render(value ? "On" : "Off", tp.x + 10, tp.y + 24);
        return;
    }
    const auto bar = track();
    ctx->fill_rect(bar, 0xB2C5D1FF);
    const float normalized = static_cast<float>((value - parameter_.minimum) / (parameter_.maximum - parameter_.minimum));
    ctx->fill_rect({bar.x, bar.y, bar.w * normalized, bar.h}, 0x177FA8FF);
    ctx->fill_rect({bar.x + bar.w * normalized - 4, bar.y - 4, 8, 16}, 0x10506BFF);
    if (!input_->is_edit_active()) {
        char buffer[32]; std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        input_->set_text(buffer);
    }
    input_->style.border_color = invalid_ ? 0xCC3030FF : 0x90A8B8FF;
    input_->render();
}
bool NumericSlider_t::handle_mouse_event(const SDL_Event& event) {
    if (!visible) return false;
    update_geometry();
    if (parameter_.kind != ParameterKind::Boolean) {
        const bool editing = input_->is_edit_active();
        if (input_->handle_mouse_event(event)) return true;
        if (editing && !input_->is_edit_active()) commit_text();
    }
    const auto bar = track();
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        const SDL_FPoint point{event.button.x, event.button.y};
        const SDL_FRect bounds{tp.x, tp.y, tp.w, tp.h};
        if (!SDL_PointInRectFloat(&point, &bounds)) return false;
        if (parameter_.kind == ParameterKind::Boolean) { assign(parameter_.get(settings_) ? 0 : 1); return true; }
        if (point.y >= tp.y + 22 && point.x <= bar.x + bar.w + 6) dragging_ = true;
        else return false;
    }
    if (dragging_ && (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
        const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
        double value = parameter_.minimum + std::clamp((x - bar.x) / bar.w, 0.f, 1.f) * (parameter_.maximum - parameter_.minimum);
        value = std::round(value / parameter_.step) * parameter_.step;
        assign(value); return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && dragging_) { dragging_ = false; return true; }
    if (event.type == SDL_EVENT_KEY_DOWN && input_->is_edit_active() && event.key.key == SDLK_ESCAPE) {
        input_->set_edit_active(false); invalid_ = false; return true;
    }
    return false;
}
