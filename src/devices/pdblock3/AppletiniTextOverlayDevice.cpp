#include "devices/displaypp/generate/AppletiniTextOverlay.hpp"

#include "NClock.hpp"
#include "computer.hpp"
#include "display/display.hpp"
#include "videosystem.hpp"

namespace {
struct OverlayDevice {
    computer_t *computer;
    AppletiniTextOverlay overlay;

    uint64_t cycle() const {
        return computer->clock ? computer->clock->get_cycles() : 0;
    }
    bool shr_canvas() const {
        const auto *display = static_cast<const display_state_t *>(
            computer->get_module_state(MODULE_DISPLAY));
        if (!display) return false;
        return display->appletini_video_enabled ? (display->new_video & 0xC0) == 0xC0
                                               : (display->new_video & 0x80) != 0;
    }
};

uint8_t read_overlay(void *context, uint32_t address) {
    auto &device = *static_cast<OverlayDevice *>(context);
    device.overlay.set_canvas(device.shr_canvas());
    return device.overlay.read(address & 0x0F, device.cycle());
}

void write_overlay(void *context, uint32_t address, uint8_t value) {
    auto &device = *static_cast<OverlayDevice *>(context);
    device.overlay.write(address & 0x0F, value, device.cycle());
}

void capture_overlay_write(void *context, uint32_t resolved_address, uint8_t value) {
    auto &device = *static_cast<OverlayDevice *>(context);
    device.overlay.capture_write(resolved_address, value, device.cycle());
}
}

void init_appletini_text_overlay(computer_t *computer) {
    auto *device = new OverlayDevice{computer, {}};
    // SmartPort occupies C7xx/C8xx. Its own native linear-text interface owns
    // C0F0..C0FF; no VOC/Second Sight device or additional slot is installed.
    for (uint16_t address = 0xC0F0; address <= 0xC0FF; ++address) {
        computer->mmu->set_C0XX_read_handler(address, {read_overlay, device});
        computer->mmu->set_C0XX_write_handler(address, {write_overlay, device});
    }
    computer->mmu->set_ram_write_observer({capture_overlay_write, device});
    computer->video_system->set_guest_overlay_provider([device]() {
        const auto frame = device->overlay.frame(device->shr_canvas(), device->cycle(), SDL_GetTicks());
        return GuestOverlayFrame{
            frame.pixels, frame.width, frame.height, frame.generation, frame.visible};
    });
    computer->register_reset_handler([device](bool) {
        device->overlay.reset();
        return true;
    });
    computer->register_shutdown_handler([device]() {
        device->computer->mmu->set_ram_write_observer({nullptr, nullptr});
        device->computer->video_system->set_guest_overlay_provider({});
        delete device;
        return true;
    });
}
