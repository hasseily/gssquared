#pragma once

#include "systemconfig.hpp"

inline bool should_enable_appletini_ramworks(const SystemConfig_t& config)
{
    if (config.platform_id != PLATFORM_APPLE_IIE
        && config.platform_id != PLATFORM_APPLE_IIE_ENHANCED) {
        return false;
    }
    if (config.slot_devices[SLOT_7] != DEVICE_ID_APPLETINI) {
        return false;
    }
    for (const device_id id : config.slot_devices) {
        if (id == DEVICE_ID_MEM_EXPANSION) {
            return false;
        }
    }
    return true;
}
