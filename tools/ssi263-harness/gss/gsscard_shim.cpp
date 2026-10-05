// The same C interface as rtl/rtlcard_shim.cpp, with GSSquared's card
// (gss/card_gss.cpp) behind it: a2vm_ssi_gss runs programs against
// GSSquared's Phasor speech path instead of the RTL.
#define GSS_NO_MAIN
#include "card_gss.cpp"

namespace {
struct Handle {
    GssCard card;
    uint64_t phase = Driver::kRate - Driver::kSampleRate;
};
}

extern "C" {
void *rtlcard_new(int) { return new Handle; }
unsigned rtlcard_cycle(void *vh, int has_access, unsigned addr, int rw, unsigned data, int res_low)
{
    Handle *h = static_cast<Handle *>(vh);
    h->phase += Driver::kSampleRate;
    bool due = false;
    if (h->phase >= Driver::kRate) { h->phase -= Driver::kRate; due = true; }
    Access a{(uint16_t)addr, rw != 0, (uint8_t)data};
    return h->card.cycle(has_access ? &a : nullptr, due, res_low != 0);
}
int rtlcard_irq(void *vh) { return static_cast<Handle *>(vh)->card.view().irq; }
int rtlcard_d7(void *vh, int chip) { return static_cast<Handle *>(vh)->card.view().d7[chip & 1]; }
void rtlcard_free(void *vh) { delete static_cast<Handle *>(vh); }
}
