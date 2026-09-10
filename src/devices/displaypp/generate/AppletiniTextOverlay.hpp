#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Appletini ONE F1.0.8 Linear RAM Text Overlay Interface 1.0. The core has
// no slot/renderer dependency: it sees only resolved Apple RAM bus writes.
class AppletiniTextOverlay {
public:
    struct Frame {
        // Straight-alpha RGBA bytes, suitable for SDL_PIXELFORMAT_RGBA32.
        const uint32_t *pixels = nullptr;
        int width = 0;
        int height = 0;
        uint64_t generation = 0;
        bool visible = false;
    };

    static constexpr uint8_t VISIBLE = 0x01;
    static constexpr uint8_t ARMED = 0x02;
    static constexpr uint8_t FRAME_PENDING = 0x10;
    static constexpr uint8_t CONFIG_ERROR = 0x20;
    static constexpr uint8_t STALE = 0x40;
    static constexpr uint8_t BUSY = 0x80;
    // The firmware's shadow clearing is asynchronous. A bounded emulated
    // delay keeps BUSY observable without adding host scheduling to the ABI.
    static constexpr uint64_t ARM_CLEAR_CYCLES = 32;

    AppletiniTextOverlay();
    void reset();
    void set_canvas(bool shr);
    uint8_t read(uint8_t offset, uint64_t cycle);
    void write(uint8_t offset, uint8_t value, uint64_t cycle);
    void advance(uint64_t cycle);
    void capture_write(uint32_t resolved_address, uint8_t value, uint64_t cycle);
    void capture_gap();
    Frame frame(bool shr_canvas, uint64_t cycle, uint64_t milliseconds);
    uint8_t status() const;

private:
    struct Config {
        uint16_t base = 0;
        uint8_t config = 0x08;
        uint8_t cols = 80;
        uint8_t rows = 24;
        uint16_t x = 0;
        uint16_t y = 0;
        uint8_t scale = 0x11;
        uint8_t fill_char = 0x20;
        uint8_t fill_attr = 0x07;
    };
    enum class Pending : uint8_t { None, Show, Hide, Off };

    uint8_t indexed_read() const;
    void indexed_write(uint8_t value);
    void command(uint8_t value, uint64_t cycle);
    void rasterize(bool blink_on);

    Config staged_, preparing_, armed_config_, active_;
    std::array<std::vector<uint8_t>, 2> shadow_;
    std::vector<uint32_t> pixels_;
    uint8_t index_ = 0;
    uint8_t cursor_x_ = 0;
    uint8_t cursor_y_ = 0;
    uint8_t cursor_control_ = 0;
    uint8_t armed_slot_ = 0;
    uint8_t active_slot_ = 0;
    bool busy_ = false;
    bool stale_ = false;
    bool error_ = false;
    bool armed_ = false;
    bool visible_ = false;
    bool dirty_ = true;
    bool blink_on_ = true;
    int width_ = 1120;
    int height_ = 768;
    Pending pending_ = Pending::None;
    uint64_t arm_ready_cycle_ = 0;
    uint64_t generation_ = 0;
};

struct computer_t;
// Appletini owns this helper and its slot-7 DEVSEL block. It adds no card.
void init_appletini_text_overlay(computer_t *computer);
