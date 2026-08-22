#pragma once

#include <cstdint>

#include "NClock.hpp"

struct AppletiniSpeedTransition {
    bool apply = false;
    clock_mode_t mode = CLOCK_1_024MHZ;
    bool restore_cpu_per_14m = false;
    uint32_t cpu_per_14m = 1;
};

/* Appletini implements the TransWarp $C074 speed-control convention:
   $01 locks execution to the Apple II's 1 MHz rate and $00 restores the
   speed that was active before the lock. */
class AppletiniSpeedControl {
public:
    AppletiniSpeedTransition write(uint8_t value,
                                    clock_mode_t current_mode,
                                    uint32_t current_cpu_per_14m) {
        if (value == 0x01) {
            if (!slow_locked_) {
                fast_mode_ = current_mode;
                fast_cpu_per_14m_ = current_cpu_per_14m;
                slow_locked_ = true;
            }
            return {
                true,
                CLOCK_1_024MHZ,
                false,
                1,
            };
        }

        if (value == 0x00 && slow_locked_) {
            slow_locked_ = false;
            return {
                true,
                fast_mode_,
                fast_mode_ == CLOCK_FREE_RUN,
                fast_cpu_per_14m_,
            };
        }

        return {};
    }

    bool slow_locked() const { return slow_locked_; }

private:
    bool slow_locked_ = false;
    clock_mode_t fast_mode_ = CLOCK_1_024MHZ;
    uint32_t fast_cpu_per_14m_ = 1;
};
