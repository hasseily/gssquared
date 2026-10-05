// Reference: appletini-one's Phasor RTL (hdl/apple/mockingboard.sv and the
// SSI-263 voices under it) under Verilator, driven one Apple cycle at a time.
//
// Fabric clocks: the card's 133.33 MHz clock gives 2777 or 2778 clocks
// between two 48 kHz sample ticks (appletini_yarz_top.sv's accumulator);
// the harness spreads them over the Apple cycles of each sample period
// (21 or 22, from the driver's cadence), so an Apple cycle is 126 to 133
// clocks and the tone stage, which runs every clock, sees the card's own
// clocking (rtl_card.hpp, cycleLength). SSI_CLOCK=fixed gives the old 130
// clocks every cycle (132.7 MHz); SSI_FABRIC=26 a 5x faster run with fixed
// clocking. Within a cycle (offsets in clocks, the same in every cycle):
//   0    Q3 rises (PHI1 start), high 37 clocks      -> raw XCK enable ~3
//   25   addr_en (TAP_ADDR_SNAP), 26 sss_en (VIA timer tick, TAP_SSS_READY)
//   65   Q3 rises (PHI0 start), high 37 clocks      -> raw XCK enable ~68
//   73   serve_en (PHI0 rise + TAP_ADDR_SNAP_LATE): addr/rw valid, reads served
//   124  data_en (PHI0 rise + TAP_DATA_SNAP): write data valid, VIA strobe
//   last audio_sample_tick, when the driver says a 48 kHz sample is due; the
//        card output is read 3 clocks earlier, where the top level samples it
// The core's DIV2 phase is brought up so the effective XCK edge is the one at
// ~68: each cycle is then [XCK, access, sample], which is GSSquared's
// [access, sample, XCK] order shifted by one XCK.
#include "rtl_card.hpp"

int main(int argc, char **argv) {
    auto ctx = std::make_unique<VerilatedContext>();
    ctx->commandArgs(1, argv);
    RtlCard card(ctx.get());
    return driverMain(card, argc, argv);
}
