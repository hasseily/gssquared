// C interface to the RTL card for a2vm (--phasor-rtl): one call an Apple
// cycle, the 48 kHz tick computed with the replay driver's accumulator so a
// capture and its replay see the same sample cadence.
#include "rtl_card.hpp"
#include <cstdlib>

namespace {
struct Handle {
    std::unique_ptr<VerilatedContext> ctx;
    std::unique_ptr<RtlCard> card;
    uint64_t phase = Driver::kRate - Driver::kSampleRate;
};
}

extern "C" {

void *rtlcard_new(int fabric)
{
    if (fabric > 0) {
        static char buf[32];
        std::snprintf(buf, sizeof buf, "%d", fabric);
        setenv("SSI_FABRIC", buf, 1);
    }
    Handle *h = new Handle;
    h->ctx = std::make_unique<VerilatedContext>();
    h->card = std::make_unique<RtlCard>(h->ctx.get());
    return h;
}

unsigned rtlcard_cycle(void *vh, int has_access, unsigned addr, int rw, unsigned data, int res_low)
{
    Handle *h = static_cast<Handle *>(vh);
    h->phase += Driver::kSampleRate;
    bool due = false;
    if (h->phase >= Driver::kRate) { h->phase -= Driver::kRate; due = true; }
    Access a{(uint16_t)addr, rw != 0, (uint8_t)data};
    return h->card->cycle(has_access ? &a : nullptr, due, res_low != 0);
}

int rtlcard_irq(void *vh) { return static_cast<Handle *>(vh)->card->view().irq; }
int rtlcard_d7(void *vh, int chip) { return static_cast<Handle *>(vh)->card->view().d7[chip & 1]; }

void rtlcard_free(void *vh) { delete static_cast<Handle *>(vh); }

}
