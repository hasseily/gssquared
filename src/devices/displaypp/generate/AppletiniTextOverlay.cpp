#include "AppletiniTextOverlay.hpp"
#include "AppletiniTextOverlayFont.hpp"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t AUX = 0x01, CP437 = 0x02, FONT16 = 0x04;
constexpr uint8_t BLINK = 0x08, TRANSPARENT = 0x10;
constexpr uint8_t palette[16][3] = {
    {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255},
};

uint32_t rgba(uint8_t color) {
    const uint8_t bytes[4] = {palette[color][0], palette[color][1], palette[color][2], 255};
    uint32_t result;
    std::memcpy(&result, bytes, sizeof(result));
    return result;
}

const uint8_t *glyph(uint8_t character, bool cp437, bool font16, bool &underline) {
    underline = !cp437 && (character & 0x80) != 0;
    if (!cp437) {
        character &= 0x7F;
        if (character < 0x20) {
            return font16 ? linear_text_overlay_dec_font_8x16[character]
                          : linear_text_overlay_dec_font_8x14[character];
        }
        if (character == 0x7F) character = 0x20;
    }
    return font16 ? linear_text_overlay_cp437_font_8x16[character]
                  : linear_text_overlay_cp437_font_8x14[character];
}
}

AppletiniTextOverlay::AppletiniTextOverlay() {
    for (auto &buffer : shadow_) buffer.resize(0xC000);
    reset();
}

void AppletiniTextOverlay::reset() {
    staged_ = preparing_ = armed_config_ = active_ = Config{};
    index_ = cursor_x_ = cursor_y_ = cursor_control_ = 0;
    armed_slot_ = active_slot_ = 0;
    busy_ = stale_ = error_ = armed_ = visible_ = false;
    pending_ = Pending::None;
    arm_ready_cycle_ = 0;
    dirty_ = true;
    ++generation_;
}

void AppletiniTextOverlay::set_canvas(bool shr) {
    const int width = shr ? 1280 : 1120;
    const int height = shr ? 800 : 768;
    if (width_ != width || height_ != height) {
        width_ = width;
        height_ = height;
        dirty_ = true;
    }
}

uint8_t AppletiniTextOverlay::status() const {
    return (visible_ ? VISIBLE : 0) | (armed_ ? ARMED : 0)
         | (pending_ != Pending::None ? FRAME_PENDING : 0)
         | (error_ ? CONFIG_ERROR : 0) | (stale_ ? STALE : 0)
         | (busy_ ? BUSY : 0);
}

uint8_t AppletiniTextOverlay::indexed_read() const {
    switch (index_) {
        case 0x00: return staged_.base & 0xFF;
        case 0x01: return staged_.base >> 8;
        case 0x02: return staged_.config;
        case 0x03: return staged_.cols;
        case 0x04: return staged_.rows;
        case 0x05: return staged_.x & 0xFF;
        case 0x06: return staged_.x >> 8;
        case 0x07: return staged_.y & 0xFF;
        case 0x08: return staged_.y >> 8;
        case 0x09: return staged_.scale;
        case 0x0A: return cursor_x_;
        case 0x0B: return cursor_y_;
        case 0x0C: return cursor_control_;
        case 0x0D: return staged_.fill_char;
        case 0x0E: return staged_.fill_attr;
        case 0x10: return width_ & 0xFF;
        case 0x11: return width_ >> 8;
        case 0x12: return height_ & 0xFF;
        case 0x13: return height_ >> 8;
        case 0x14: return active_.base & 0xFF;
        case 0x15: return active_.base >> 8;
        case 0x16: return active_.config;
        case 0x17: return active_.cols;
        case 0x18: return active_.rows;
        case 0x19: return active_.x & 0xFF;
        case 0x1A: return active_.x >> 8;
        case 0x1B: return active_.y & 0xFF;
        case 0x1C: return active_.y >> 8;
        case 0x1D: return active_.scale;
        case 0x1E: return 0x7F;
        default: return 0;
    }
}

void AppletiniTextOverlay::indexed_write(uint8_t value) {
    switch (index_) {
        case 0x00: staged_.base = (staged_.base & 0xFF00) | value; break;
        case 0x01: staged_.base = (staged_.base & 0x00FF) | (uint16_t(value) << 8); break;
        case 0x02: staged_.config = value; break;
        case 0x03: staged_.cols = value; break;
        case 0x04: staged_.rows = value; break;
        case 0x05: staged_.x = (staged_.x & 0xFF00) | value; break;
        case 0x06: staged_.x = (staged_.x & 0x00FF) | (uint16_t(value) << 8); break;
        case 0x07: staged_.y = (staged_.y & 0xFF00) | value; break;
        case 0x08: staged_.y = (staged_.y & 0x00FF) | (uint16_t(value) << 8); break;
        case 0x09: staged_.scale = value; break;
        case 0x0A: cursor_x_ = value; dirty_ = true; break;
        case 0x0B: cursor_y_ = value & 0x7F; dirty_ = true; break;
        case 0x0C: cursor_control_ = value & 0x0F; dirty_ = true; break;
        case 0x0D: staged_.fill_char = value; break;
        case 0x0E: staged_.fill_attr = value; break;
        default: break;
    }
}

uint8_t AppletiniTextOverlay::read(uint8_t offset, uint64_t cycle) {
    advance(cycle);
    switch (offset & 0x0F) {
        case 0: return index_;
        case 1: return indexed_read();
        case 2: { const uint8_t value = indexed_read(); ++index_; return value; }
        case 4: return status();
        case 8: return 'L';
        case 9: return 'I';
        case 10: return 'N';
        case 11: return 'T';
        case 12: return 'X';
        case 13: return 'T';
        case 14: return 0x4C;
        case 15: return 0x10;
        default: return 0;
    }
}

void AppletiniTextOverlay::write(uint8_t offset, uint8_t value, uint64_t cycle) {
    advance(cycle);
    switch (offset & 0x0F) {
        case 0: index_ = value; break;
        case 1: indexed_write(value); break;
        case 2: indexed_write(value); ++index_; break;
        case 3: command(value, cycle); break;
        default: break;
    }
}

void AppletiniTextOverlay::command(uint8_t value, uint64_t cycle) {
    if (busy_) return;
    switch (value) {
        case 0: pending_ = Pending::Off; break;
        case 1:
            // The pending frame still owns the armed snapshot.
            if (pending_ != Pending::None) return;
            if (!staged_.cols || !staged_.rows || staged_.rows > 127
                || !(staged_.scale & 0x0F) || !(staged_.scale >> 4)
                || staged_.base < 0x0200 || (staged_.config & 0xE0)) {
                error_ = true;
                return;
            }
            preparing_ = staged_;
            busy_ = true;
            arm_ready_cycle_ = cycle + ARM_CLEAR_CYCLES;
            break;
        case 2:
            if (armed_ && !stale_ && !error_) pending_ = Pending::Show;
            break;
        case 3: pending_ = Pending::Hide; break;
        default: break;
    }
}

void AppletiniTextOverlay::advance(uint64_t cycle) {
    if (!busy_ || cycle < arm_ready_cycle_) return;
    busy_ = false;
    const uint32_t count = 2u * preparing_.cols * preparing_.rows;
    if (uint32_t(preparing_.base) + count > 0xC000u) {
        error_ = true;
        return;
    }
    armed_config_ = preparing_;
    armed_slot_ = active_slot_ ^ 1;
    auto &buffer = shadow_[armed_slot_];
    for (uint32_t i = 0; i < count; i += 2) {
        buffer[i] = preparing_.fill_char;
        buffer[i + 1] = preparing_.fill_attr;
    }
    armed_ = true;
    stale_ = error_ = false;
}

void AppletiniTextOverlay::capture_write(uint32_t address, uint8_t value, uint64_t cycle) {
    advance(cycle);
    if (!armed_ || busy_ || (address & 0xFFFE0000u)) return;
    if (((address >> 16) & 1u) != (armed_config_.config & AUX)) return;
    const uint32_t lo = address & 0xFFFFu;
    const uint32_t count = 2u * armed_config_.cols * armed_config_.rows;
    if (lo < armed_config_.base || lo - armed_config_.base >= count) return;
    auto &cell = shadow_[armed_slot_][lo - armed_config_.base];
    if (cell != value) {
        cell = value;
        if (armed_slot_ == active_slot_) dirty_ = true;
    }
}

void AppletiniTextOverlay::capture_gap() {
    if (!armed_) return;
    stale_ = true;
    armed_ = false;
    pending_ = Pending::Hide;
}

AppletiniTextOverlay::Frame AppletiniTextOverlay::frame(bool shr, uint64_t cycle, uint64_t milliseconds) {
    advance(cycle);
    set_canvas(shr);
    if (pending_ != Pending::None) {
        if (pending_ == Pending::Show) {
            active_ = armed_config_;
            active_slot_ = armed_slot_;
            visible_ = true;
        } else {
            visible_ = false;
            if (pending_ == Pending::Off) armed_ = false;
        }
        pending_ = Pending::None;
        dirty_ = true;
        ++generation_;
    }
    const bool blink = ((milliseconds / 500) & 1) == 0;
    if (visible_ && (dirty_ || blink != blink_on_)) {
        rasterize(blink);
        blink_on_ = blink;
        dirty_ = false;
        ++generation_;
    }
    return {pixels_.data(), width_, height_, generation_, visible_};
}

void AppletiniTextOverlay::rasterize(bool blink_on) {
    pixels_.assign(size_t(width_) * height_, 0);
    const int sx = active_.scale & 15;
    const int sy = active_.scale >> 4;
    const int font_height = (active_.config & FONT16) ? 16 : 14;
    const bool transparent = (active_.config & TRANSPARENT) != 0;
    const auto &cells = shadow_[active_slot_];
    for (int row = 0; row < active_.rows; ++row) {
        const int y0 = active_.y + row * font_height * sy;
        if (y0 >= height_) break;
        for (int col = 0; col < active_.cols; ++col) {
            const int x0 = active_.x + col * 8 * sx;
            if (x0 >= width_) break;
            const size_t index = 2u * (size_t(row) * active_.cols + col);
            const uint8_t attr = cells[index + 1];
            uint8_t fg = attr & 15;
            uint8_t bg = (attr >> 4) & ((active_.config & BLINK) ? 7 : 15);
            const bool attr_blink = (active_.config & BLINK) && (attr & 0x80);
            bool underline = false;
            const uint8_t *bits = glyph(cells[index], (active_.config & CP437) != 0,
                                       font_height == 16, underline);
            const bool cursor = (cursor_control_ & 1) && cursor_x_ == col && cursor_y_ == row
                && (!(cursor_control_ & 2) || blink_on);
            const uint8_t shape = (cursor_control_ >> 2) & 3;
            if (cursor && shape == 0) std::swap(fg, bg);
            for (int gy = 0; gy < font_height; ++gy) {
                uint8_t mask = bits[gy];
                if ((underline || (cursor && shape == 1)) && gy == font_height - 2) mask = 0xFF;
                if (attr_blink && !blink_on) mask = 0;
                for (int ry = 0; ry < sy; ++ry) {
                    const int y = y0 + gy * sy + ry;
                    if (y >= height_) break;
                    for (int gx = 0; gx < 8; ++gx) {
                        const bool set = (mask & (0x80u >> gx)) || (cursor && shape == 2 && gx == 0);
                        if (!set && transparent) continue;
                        const uint32_t color = rgba(set ? fg : bg);
                        for (int rx = 0; rx < sx; ++rx) {
                            const int x = x0 + gx * sx + rx;
                            if (x >= width_) break;
                            pixels_[size_t(y) * width_ + x] = color;
                        }
                    }
                }
            }
        }
    }
}
