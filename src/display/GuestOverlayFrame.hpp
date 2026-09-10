#pragma once
#include <cstdint>

// Optional guest pixels, composed before host UI and display effects.
struct GuestOverlayFrame {
    const uint32_t* pixels=nullptr;
    int width=0,height=0;
    uint64_t generation=0;
    bool visible=false;
};
