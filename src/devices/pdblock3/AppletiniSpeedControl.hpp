#pragma once

#include <cstdint>

#include "NClock.hpp"

struct AppletiniSpeedTransition {
    bool apply = false;
    clock_mode_t mode = CLOCK_1_024MHZ;
    bool restore_cpu_per_14m = false;
    uint32_t cpu_per_14m = 1;
};

/* Appletini ONE F1.0.8 decodes the low two bits of $C074. Nonzero
   values select native speed; 3 cannot be released before Apple reset. */
class AppletiniSpeedControl {
public:
    void configure(bool enabled, bool ignore_c074) {
        enabled_ = enabled;
        ignore_c074_ = ignore_c074;
    }
    AppletiniSpeedTransition write(uint8_t value,
                                    clock_mode_t current_mode,
                                    uint32_t current_cpu_per_14m) {
        if (!enabled_ || ignore_c074_ || off_until_reset_) return {};
        value &= 3;
        if (value != 0) {
            if (!slow_locked_) {
                fast_mode_ = current_mode;
                fast_cpu_per_14m_ = current_cpu_per_14m;
                slow_locked_ = true;
            }
            off_until_reset_ = value == 3;
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
    bool off_until_reset() const { return off_until_reset_; }
    AppletiniSpeedTransition reset() {
        off_until_reset_ = false;
        if (!slow_locked_) return {};
        slow_locked_ = false;
        return {true, fast_mode_, fast_mode_ == CLOCK_FREE_RUN, fast_cpu_per_14m_};
    }

private:
    bool slow_locked_ = false;
    bool off_until_reset_ = false;
    bool enabled_ = true;
    bool ignore_c074_ = false;
    clock_mode_t fast_mode_ = CLOCK_1_024MHZ;
    uint32_t fast_cpu_per_14m_ = 1;
};
