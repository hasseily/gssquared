#pragma once

#include "NClock.hpp"
#include <optional>
#include <string_view>

// Saved with the machine; defaults follow Appletini ONE firmware F1.0.8.
struct AppletiniConfig {
    bool accelerator = false;
    bool ignore_c074 = false;
    bool ramworks = true;
    bool ram32 = false;
    clock_mode_t speed = CLOCK_33_3MHZ;
};

inline const char* appletini_speed_name(clock_mode_t speed) {
    switch (speed) {
        case CLOCK_1_024MHZ: return "1";
        case CLOCK_2_8MHZ: return "2.8";
        case CLOCK_7_159MHZ: return "7";
        case CLOCK_14_3MHZ: return "14";
        case CLOCK_33_3MHZ: return "33";
        case CLOCK_FREE_RUN: return "unlimited";
        default: return "33";
    }
}
inline std::optional<clock_mode_t> appletini_parse_speed(std::string_view value) {
    if (value == "1000000" || value == "1024000") return CLOCK_1_024MHZ;
    if (value == "2800000") return CLOCK_2_8MHZ;
    if (value == "7159000") return CLOCK_7_159MHZ;
    if (value == "14318000") return CLOCK_14_3MHZ;
    if (value == "33333333") return CLOCK_33_3MHZ;
    for (const auto mode : {CLOCK_1_024MHZ, CLOCK_2_8MHZ, CLOCK_7_159MHZ,
                           CLOCK_14_3MHZ, CLOCK_33_3MHZ, CLOCK_FREE_RUN}) {
        if (value == appletini_speed_name(mode)) return mode;
    }
    return std::nullopt;
}
