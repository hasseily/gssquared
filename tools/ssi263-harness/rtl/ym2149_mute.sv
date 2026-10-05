`timescale 1ns / 1ps
// Bench-only YM2149 for bin/card_rtl: the real PSG with its audio muted.
//
// build.sh writes obj/YM2149_real.sv, a copy of appletini-one's
// hdl/apple/YM2149.sv with only the module name changed (YM2149 ->
// YM2149_real); the checkout itself is never modified. This wrapper takes
// the YM2149 name that mockingboard.sv instantiates, so the PSG keeps its
// whole bus protocol (register latch, writes, read-back through VIA port A,
// reset), and only its three channel outputs are forced to 0 while the
// bench's psg_mute input is high (the default; SSI_PSG=1 keeps the PSG
// audible). The card output (card.pcm) is then the speech path alone, which
// is what GSSquared's card mirror (gss/card_gss.cpp) renders: it feeds the
// mixer silent AY banks.
module YM2149 (
    input        CLK, input CE, input RESET, input BDIR, input BC,
    input  [7:0] DI, output [7:0] DO,
    output [7:0] CHANNEL_A, output [7:0] CHANNEL_B, output [7:0] CHANNEL_C,
    input        SEL, input MODE, output [5:0] ACTIVE,
    input  [7:0] IOA_in, output [7:0] IOA_out, input [7:0] IOB_in, output [7:0] IOB_out
);
    wire [7:0] a, b, c;
    YM2149_real psg (
        .CLK(CLK), .CE(CE), .RESET(RESET), .BDIR(BDIR), .BC(BC),
        .DI(DI), .DO(DO),
        .CHANNEL_A(a), .CHANNEL_B(b), .CHANNEL_C(c),
        .SEL(SEL), .MODE(MODE), .ACTIVE(ACTIVE),
        .IOA_in(IOA_in), .IOA_out(IOA_out), .IOB_in(IOB_in), .IOB_out(IOB_out)
    );
    // Upward hierarchical reference to the bench's control input.
    wire mute = tb_card.psg_mute;
    assign CHANNEL_A = mute ? 8'd0 : a;
    assign CHANNEL_B = mute ? 8'd0 : b;
    assign CHANNEL_C = mute ? 8'd0 : c;
endmodule
