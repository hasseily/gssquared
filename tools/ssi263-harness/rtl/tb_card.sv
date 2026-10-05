`timescale 1ns / 1ps

// Card-level wrapper around appletini-one's hdl/apple/mockingboard.sv (the
// Phasor: two VIAs, four PSGs, two SSI-263AP voices) for the comparison
// harness. Nothing in the RTL is modified: the C++ driver (card_rtl.cpp)
// plays the Apple bus strobes of apple_bus_wrapper.sv into ab_read once per
// fabric clock, toggles Q3, and pulses audio_sample_tick. Internal speech
// taps are read through hierarchical references.
module tb_card (
    input  logic        clk,
    input  logic        rstn,
    input  logic        res,          // Apple RES (1 = running)
    input  logic        q3,
    input  logic        tick,         // audio_sample_tick
    input  logic        sss_en,
    input  logic        serve_en,
    input  logic        data_en,
    input  logic        addr_en,
    input  logic [15:0] addr,
    input  logic        rw,           // 1 = read
    input  logic [7:0]  data,
    input  logic [31:0] audio_control,
    input  logic        psg_mute,     // 1 = the PSGs' audio is muted (rtl/ym2149_mute.sv)
    output logic [7:0]  rdata,
    output logic        rdata_en,
    output logic        irq,
    output logic signed [15:0] ssi_sec,  // ssi0: A5 socket (left)
    output logic signed [15:0] ssi_pri,  // ssi1: A6 socket (right)
    output logic signed [15:0] out_l,
    output logic signed [15:0] out_r,
    output logic        d7_sec,
    output logic        d7_pri,
    output logic        dirq_sec,
    output logic        dirq_pri,
    output logic        via0_irq_o,
    output logic        via1_irq_o,
    output logic [2:0]  mode,
    output logic        div2_sec,
    output logic        div2_pri,
    // Debug taps of one voice (dbg_sel 0 = secondary, 1 = primary).
    input  logic        dbg_sel,
    output logic [9:0]  dbg_pitch,
    output logic [4:0]  dbg_ticks,
    output logic [4:0]  dbg_closure,
    output logic [31:0] dbg_filt,     // {va,fa,fc,f1,f2(5),f2q,f3} packed
    output logic [31:0] dbg_cur,      // {va,fa,f1,f3}
    output logic [31:0] dbg_synth,    // {voice_gain,noise_gain,noise_fc,amp,closure_gain(3)}
    output logic signed [15:0] dbg_vsrc,
    output logic [5:0]  dbg_upd,
    output logic        dbg_gate,
    output logic        dbg_noise
);
    globals::AppleBus_read ab;
    globals::AppleBus_write abw;
    globals::SoftSwitchState sss;

    always_comb begin
        ab = '0;
        ab.data = data;
        ab.addr = addr;
        ab.rw = rw;
        ab.phi0 = 1'b0;
        ab.devsel_n = 1'b1;
        ab.cycle_valid = 1'b1;
        ab.res = res;
        ab.rdy = 1'b1;
        ab.data_en = data_en;
        ab.addr_en = addr_en;
        ab.sss_en = sss_en;
        ab.serve_en = serve_en;
        ab.addr_early = addr;
        ab.rw_early = rw;
        ab.devsel_n_early = 1'b1;
        sss = '0;
        // Slot I/O space $C100-$C7FF with INTCXROM off.
        sss.slot_access = (addr[15:11] == 5'b11000) && (addr[10:8] != 3'd0);
    end

    mockingboard dut (
        .clk(clk),
        .rstn(rstn),
        .apple_q3_raw(q3),
        .ab_read(ab),
        .sss(sss),
        .slot_assign(3'd4),
        .pan(48'h888888_888888),
        .audio_control(audio_control),
        .audio_sample_tick(tick),
        .ab_write(abw),
        .audio_l(out_l),
        .audio_r(out_r),
        .dbg_ssi_irq(),
        .dbg_ssi_backend_done(),
        .dbg_ssi_enable_ints()
    );

    assign rdata = abw.wr_data;
    assign rdata_en = abw.wr_data_en;
    assign irq = abw.assert_irq;
    assign ssi_sec = dut.ssi0_audio;
    assign ssi_pri = dut.ssi1_audio;
    assign d7_sec = dut.ssi0_d7;
    assign d7_pri = dut.ssi1_d7;
    assign dirq_sec = dut.ssi0_direct_irq;
    assign dirq_pri = dut.ssi1_direct_irq;
    assign via0_irq_o = dut.via0_irq;
    assign via1_irq_o = dut.via1_irq;
    assign mode = dut.phasor_mode_q;
    assign div2_sec = dut.ssi263_secondary_i.bus_wrapper_i.formant_backend_i.digital_core_i.ssi_div2_phase_q;
    assign div2_pri = dut.ssi263_primary_i.bus_wrapper_i.formant_backend_i.digital_core_i.ssi_div2_phase_q;

`define BE(v) dut.ssi263_``v``_i.bus_wrapper_i.formant_backend_i
`define DBG(sig) (dbg_sel ? `BE(primary).sig : `BE(secondary).sig)
    assign dbg_pitch = `DBG(pitch_q);
    assign dbg_ticks = `DBG(ticks_q);
    assign dbg_closure = `DBG(closure_q);
    assign dbg_gate = `DBG(pitch_gate_q);
    assign dbg_noise = `DBG(cur_noise_q);
    assign dbg_filt = {`DBG(filt_va_q), `DBG(filt_fa_q), `DBG(filt_fc_q), `DBG(filt_f1_q),
                       `DBG(filt_f2_q), `DBG(filt_f2q_q), `DBG(filt_f3_q), 3'b0};
    assign dbg_cur = {`DBG(cur_va_q), `DBG(cur_fa_q), `DBG(cur_f1_q), `DBG(cur_f3_q)};
    assign dbg_synth = {12'd0, `DBG(synth_voice_gain_q), `DBG(synth_noise_gain_q),
                        `DBG(synth_noise_fc_q), `DBG(synth_amp_q), 1'b0, `DBG(synth_closure_gain_q)};
    assign dbg_vsrc = `DBG(synth_voice_source_q);
    assign dbg_upd = dbg_sel ? `BE(primary).digital_core_i.update_counter_q
                             : `BE(secondary).digital_core_i.update_counter_q;
endmodule
