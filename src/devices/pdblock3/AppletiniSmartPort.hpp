#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

// Apple-visible byte protocol from Appletini ONE F1.0.8, c5044416.
// The host supplies mounted block devices; hardware DMA is intentionally absent.
class AppletiniSmartPort {
public:
    static constexpr uint8_t max_units = 8;
    static constexpr uint32_t ram_blocks = 65535;
    static constexpr uint8_t ok = 0, badctl = 0x21, io_error = 0x27,
                             no_device = 0x28, no_write = 0x2B;
    struct Unit {
        bool present = false;
        uint32_t blocks = 0;
        bool protected_media = false;
        bool configured = false; // Failed configured images must not become RAM32.
    };
    using Describe = std::function<Unit(uint8_t)>;
    using Read = std::function<uint8_t(uint8_t, uint32_t, uint8_t*)>;
    using Write = std::function<uint8_t(uint8_t, uint32_t, const uint8_t*)>;

    AppletiniSmartPort(uint8_t slot, Describe describe, Read read, Write write)
        : slot_(slot), describe_(std::move(describe)), read_(std::move(read)),
          write_(std::move(write)) {}

    void reset() { command_.clear(); response_.clear(); ready_ = false; }
    void data_write(uint8_t value) {
        // Match the hardware's 1024-byte FIFO: writes when full are ignored.
        if (command_.size() < 1024) command_.push_back(value);
    }
    uint8_t data_read() const { return response_.empty() ? 0 : response_.front(); }
    void pop() { if (!response_.empty()) response_.pop_front(); }
    uint8_t control_read() const { return ready_ ? 0x80 : 0; }

    void enable_ramdisk(bool enabled) {
        ram_enabled_ = enabled;
        if (!enabled) { ram_.clear(); ram_.shrink_to_fit(); ram_unit_ = -1; }
    }
    bool ramdisk_enabled() const { return ram_enabled_; }
    int ramdisk_unit() { refresh_ramdisk(); return ram_unit_; }

    void execute(uint8_t family) {
        ready_ = false;
        response_.clear();
        refresh_ramdisk();
        if (family == 1 && command_.size() >= 6) {
            const uint8_t drive = ((command_[1] >> 4) & 7) == slot_
                ? command_[1] >> 7 : max_units;
            if (command_[0] == 0) {
                const Unit info = unit(drive);
                const uint32_t blocks = std::min(info.blocks, uint32_t{65535});
                append({info.present ? ok : no_device,
                        static_cast<uint8_t>(blocks), static_cast<uint8_t>(blocks >> 8)});
            } else {
                block_command(command_[0], drive,
                    command_[4] | (uint32_t{command_[5]} << 8), 6);
            }
        } else if (family == 2 && command_.size() >= 10) {
            if (command_[0] == 0) status(command_[2], command_[5]);
            else block_command(command_[0], static_cast<uint8_t>(command_[2] - 1),
                command_[5] | (uint32_t{command_[6]} << 8) |
                    (uint32_t{command_[7]} << 16), 10);
        } else {
            response_.push_back(badctl); // Includes unsupported $40 config family.
        }
        command_.clear();
        ready_ = true;
    }

private:
    uint8_t slot_;
    Describe describe_;
    Read read_;
    Write write_;
    std::vector<uint8_t> command_;
    std::deque<uint8_t> response_;
    bool ready_ = false, ram_enabled_ = false;
    int ram_unit_ = -1;
    std::vector<uint8_t> ram_;

    void append(std::initializer_list<uint8_t> bytes) {
        response_.insert(response_.end(), bytes.begin(), bytes.end());
    }
    Unit unit(uint8_t index) const {
        if (index >= max_units) return {};
        if (index == ram_unit_) return {true, ram_blocks, false, true};
        return describe_(index);
    }
    void refresh_ramdisk() {
        if (!ram_enabled_) return;
        // File mounts are authoritative. Move RAM32 only when its unit is
        // explicitly occupied. Apple reset alone preserves its contents.
        if (ram_unit_ >= 0) {
            const Unit existing = describe_(static_cast<uint8_t>(ram_unit_));
            if (!existing.present && !existing.configured) return;
            // Firmware closes the old RAM device before assigning a real path;
            // a new free unit gets a freshly formatted volatile volume.
            ram_.clear();
        }
        ram_unit_ = -1;
        for (uint8_t i = 0; i < max_units; ++i) {
            const Unit info = describe_(i);
            if (!info.present && !info.configured) { ram_unit_ = i; break; }
        }
        if (ram_unit_ < 0 || !ram_.empty()) return;
        ram_.assign(size_t{ram_blocks} * 512, 0);
        for (uint8_t i = 2; i <= 5; ++i) {
            ram_[i * 512] = i == 2 ? 0 : i - 1;
            ram_[i * 512 + 2] = i == 5 ? 0 : i + 1;
        }
        uint8_t* header = ram_.data() + 1028;
        header[0] = 0xF5;
        std::memcpy(header + 1, "RAM32", 5);
        header[0x1E] = 0xC3; header[0x1F] = 0x27; header[0x20] = 0x0D;
        header[0x23] = 6; header[0x25] = 0xFF; header[0x26] = 0xFF;
        std::fill(ram_.begin() + 6 * 512, ram_.begin() + 22 * 512, 0xFF);
        for (uint32_t i = 0; i < 22; ++i)
            ram_[6 * 512 + (i >> 3)] &= static_cast<uint8_t>(~(0x80 >> (i & 7)));
        ram_[22 * 512 - 1] &= 0xFE; // Nonexistent block 65535 is not free.
    }
    void status(uint8_t number, uint8_t code) {
        std::array<uint8_t, 29> payload{};
        size_t size = 0;
        if (number == 0) {
            for (uint8_t i = 0; i < max_units; ++i) if (unit(i).present) ++payload[0];
            if (code == 0) size = 8;
            else if (code == 3) {
                payload[8] = 12;
                std::memcpy(payload.data() + 9, "Appletini SP", 12);
                std::fill(payload.begin() + 21, payload.begin() + 25, ' ');
                payload[27] = 1; size = 29; // Firmware's DIB version remains 1,0.
            }
        } else {
            const Unit info = unit(number - 1);
            if (!info.present) { response_.push_back(no_device); return; }
            payload[0] = info.protected_media ? 0xFC : 0xF8;
            payload[1] = info.blocks; payload[2] = info.blocks >> 8;
            payload[3] = info.blocks >> 16;
            if (code == 0) size = 4;
            else if (code == 3) {
                payload[4] = 12;
                std::memcpy(payload.data() + 5, "Appletini HD", 12);
                std::fill(payload.begin() + 17, payload.begin() + 21, ' ');
                payload[21] = 2; payload[22] = 0x20; payload[23] = 1; size = 25;
            }
        }
        if (!size) { response_.push_back(badctl); return; }
        append({ok, static_cast<uint8_t>(size), 0});
        response_.insert(response_.end(), payload.begin(), payload.begin() + size);
    }
    void block_command(uint8_t command, uint8_t index, uint32_t block, size_t prefix) {
        if (command == 3 && prefix == 10) { response_.push_back(no_write); return; }
        if (command != 1 && command != 2) { response_.push_back(badctl); return; }
        const Unit info = unit(index);
        uint8_t result = !info.present ? no_device :
            (command == 2 && info.protected_media ? no_write :
             (block >= info.blocks ? io_error : ok));
        std::array<uint8_t, 512> data{};
        if (result == ok && command == 1) {
            if (index == ram_unit_) std::memcpy(data.data(), ram_.data() + size_t{block} * 512, 512);
            else result = read_(index, block, data.data());
        } else if (result == ok && command == 2) {
            if (command_.size() < prefix + 512) result = io_error;
            else if (index == ram_unit_) std::memcpy(ram_.data() + size_t{block} * 512, command_.data() + prefix, 512);
            else result = write_(index, block, command_.data() + prefix);
        }
        response_.push_back(result);
        if (command == 1 && result == ok) response_.insert(response_.end(), data.begin(), data.end());
    }
};
