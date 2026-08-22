#pragma once

#include <cstddef>
#include <cstdint>

#include "devices/displaypp/RGBA.hpp"

struct AppletiniVideo7State {
    uint8_t flags = 0;
    uint8_t mode = 0;
    uint8_t sequence = 0;

    void reset();
    void access(uint16_t address, bool mixed, bool col80);
    bool mono560() const { return mode == 3; }
};

enum class appletini_shr_family_t : uint8_t {
    SHR = 0,
    SHR4,
    SHR_3200,
};

struct AppletiniSHRRenderInfo {
    appletini_shr_family_t family = appletini_shr_family_t::SHR;
    uint8_t selector_mask = 0;
    uint8_t page_mode = 0;
    bool pal256 = false;
};

/* Return the A2Li page mode for the active legacy graphics family.
   The signal always lives in main memory. */
uint8_t appletini_legacy_paged_mode(const uint8_t *main_bank,
                                    bool graphics, bool hires);

/* Render the Appletini SHR surface as 640x400 RGBA pixels. Main and aux
   point at complete, linear 64 KiB Apple II banks. */
AppletiniSHRRenderInfo appletini_render_shr(const uint8_t *main_bank,
                                            const uint8_t *aux_bank,
                                            RGBA_t *output,
                                            size_t output_stride,
                                            bool force_monochrome);
