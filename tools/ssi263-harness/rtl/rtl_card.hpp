// The RTL card (tb_card under Verilator) behind the Card interface; shared
// by card_rtl (the replay renderer) and a2vm's --phasor-rtl.
#pragma once
#include "Vtb_card.h"
#include "verilated.h"
#include "../common/driver.hpp"
#include <memory>
#include <cstring>

class RtlCard : public Card {
public:
    explicit RtlCard(VerilatedContext *ctx) : top_(new Vtb_card(ctx)) {
        const char *f = std::getenv("SSI_FABRIC");
        F_ = f ? std::atoi(f) : 130;
        if (F_ < 20) F_ = 20;
        auto sc = [&](int x) { return x * F_ / 130; };
        o_q3a_ = 0; o_q3a_end_ = sc(37);
        o_q3b_ = F_ / 2; o_q3b_end_ = F_ / 2 + sc(37);
        o_addr_ = sc(25); o_sss_ = sc(26);
        o_serve_ = sc(73); o_data_ = sc(124);
        o_rd_ = (o_serve_ + o_data_) / 2;
        o_tick_ = F_ - 1;
        // Fabric clocking (see cycle()). "card" (the default at F=130): the
        // clocks between two sample ticks are the card's own, 2777 or 2778
        // from appletini_yarz_top.sv's 32-bit accumulator (+1546188 a
        // 133.33 MHz clock, the tick on its carry), spread over the Apple
        // cycles of that sample period, and the card output is the value the
        // top level samples (see sample_lr_). "fixed": F clocks every Apple
        // cycle and the output read at the tick edge (the old harness; it is
        // also what SSI_FABRIC != 130 uses).
        const char *ck = std::getenv("SSI_CLOCK");
        card_clock_ = F_ == 130 && !(ck && std::strcmp(ck, "fixed") == 0);
        const char *ac = std::getenv("SSI_AUDIO_CONTROL");
        // Default: tone controls 0 except warmth +8 (GSSquared's fixed card
        // setting), AY-3-8913 volume table, Mockingboard-only off.
        audio_control_ = ac ? (uint32_t)std::strtoul(ac, nullptr, 0)
                            : ((8u << 15) | (1u << 25));
        top_->audio_control = audio_control_;
        // The PSGs keep their bus protocol but are muted (rtl/ym2149_mute.sv)
        // so card.pcm is the speech path alone, as GSSquared's card mirror
        // renders it. SSI_PSG=1 keeps them audible (listening only).
        const char *psg = std::getenv("SSI_PSG");
        top_->psg_mute = (psg && std::atoi(psg)) ? 0 : 1;
        top_->rstn = 0; top_->res = 1; top_->rw = 1; top_->addr = 0; top_->data = 0;
        top_->q3 = 0; top_->tick = 0;
        top_->sss_en = top_->serve_en = top_->data_en = top_->addr_en = 0;
        const char *dbg = std::getenv("SSI_DBG");
        if (dbg) {
            dbg_ = std::fopen(dbg, "w");
            const char *sel = std::getenv("SSI_DBG_SEL");
            top_->dbg_sel = sel ? std::atoi(sel) : 1;
        }
        for (int i = 0; i < 2 * F_; ++i) clk();      // power-on reset
        top_->rstn = 1;
    }
    ~RtlCard() override { top_->final(); if (dbg_) std::fclose(dbg_); }

    uint8_t cycle(const Access *acc, bool due, bool res_low) override {
        uint8_t got = 0x00;
        bool got_set = false;
        top_->res = res_low ? 0 : 1;
        const int L = card_clock_ ? cycleLength(due) : F_;
        o_tick_ = L - 1;
        for (int o = 0; o < L; ++o) {
            // appletini_yarz_top.sv samples mixed_audio at the clock edge E
            // where audio_sample_acc_next carries; the mockingboard sees
            // audio_sample_tick at E+1 (the tick edge here, o_tick_), and
            // between them sits apple_top.sv's mockingboard_audio_l_q
            // register. The DAC value is therefore mockingboard_audio_l_q
            // before edge E = o_tick_-1, which is audio_l before edge
            // o_tick_-2, i.e. audio_l after edge o_tick_-3: pre_ at the start
            // of iteration o_tick_-2.
            if (card_clock_ && due && o == o_tick_ - 2) { lr_l_ = pre_.l; lr_r_ = pre_.r; }
            top_->q3 = ((o >= o_q3a_ && o < o_q3a_end_) || (o >= o_q3b_ && o < o_q3b_end_)) ? 1 : 0;
            top_->addr_en = (o == o_addr_);
            top_->sss_en = (o == o_sss_);
            top_->serve_en = (o == o_serve_);
            top_->data_en = (o == o_data_);
            top_->tick = (due && o == o_tick_);
            if (acc) {
                top_->addr = acc->addr;
                top_->rw = acc->rw ? 1 : 0;
                top_->data = acc->rw ? 0x00 : acc->data;
            } else {
                // An idle cycle: a read of a page-zero address no card decodes.
                top_->addr = 0x0000; top_->rw = 1; top_->data = 0x00;
            }
            if (o == o_serve_) {
                view_.irq = top_->irq; view_.d7[0] = top_->d7_sec; view_.d7[1] = top_->d7_pri;
                view_.dirq[0] = top_->dirq_sec; view_.dirq[1] = top_->dirq_pri;
            }
            if (o == o_rd_ && acc && acc->rw && !got_set) {
                got = top_->rdata_en ? top_->rdata : 0x00;
                got_set = true;
            }
            clk();
            if (due && o == o_tick_) {
                // audio_sample_tick samples audio_q at this edge (the RTL
                // mixer's view): read the outputs as they were before it.
                s_ = pre_;
                if (card_clock_) { s_.l = lr_l_; s_.r = lr_r_; }
                if (dbg_) {
                    // Excitation view: state the tick latched (pre-edge
                    // mirrors, post-edge synth latches).
                    std::fprintf(dbg_, "%llu P pitch=%u ticks=%u clos=%u gate=%u nz=%u upd=%u filt=%08X cur=%08X synth=%05X vsrc=%d out=%d\n",
                        (unsigned long long)nsamp_, pre_pitch_, pre_ticks_, pre_clos_, pre_gate_, pre_noise_, pre_upd_, pre_filt_, pre_cur_,
                        (unsigned)top_->dbg_synth, (int)(int16_t)top_->dbg_vsrc, (int)s_.pri);
                    dbg_post_ = 8;
                }
                ++nsamp_;
            }
            if (dbg_) {
                if (dbg_post_ && --dbg_post_ == 0) {
                    std::fprintf(dbg_, "%llu F filt=%08X\n", (unsigned long long)(nsamp_ - 1), (unsigned)top_->dbg_filt);
                }
                pre_pitch_ = top_->dbg_pitch; pre_ticks_ = top_->dbg_ticks; pre_clos_ = top_->dbg_closure;
                pre_gate_ = top_->dbg_gate; pre_noise_ = top_->dbg_noise; pre_upd_ = top_->dbg_upd;
                pre_filt_ = top_->dbg_filt; pre_cur_ = top_->dbg_cur;
            }
            pre_.sec = top_->ssi_sec; pre_.pri = top_->ssi_pri;
            pre_.l = top_->out_l; pre_.r = top_->out_r;
        }
        return got;
    }
    View view() override { return view_; }
    Sample sample() override { return s_; }
    const char *name() override { return card_clock_ ? "rtl" : (F_ == 130 ? "rtl-fixed130" : "rtl-fast"); }
    // Every setting read from the environment above, as used (events.txt's
    // "# settings" line; run_suite.py's reference cache checks it).
    std::string settings() override {
        char b[160];
        std::snprintf(b, sizeof b, "fabric=%d clock=%s audio_control=0x%08X psg=%s dbg=%s dbg_sel=%d",
                      F_, card_clock_ ? "card" : "fixed", (unsigned)audio_control_,
                      top_->psg_mute ? "muted" : "audible", dbg_ ? "on" : "off", dbg_ ? (int)top_->dbg_sel : 0);
        return b;
    }

private:
    // The Apple cycle's fabric clocks in "card" clocking. The driver's 48 kHz
    // cadence (Driver::step: phase += 48000, due on reaching 1020484) is
    // replayed here to know how many Apple cycles the current sample period
    // has (21 or 22); the card's accumulator gives how many fabric clocks it
    // has (2777 or 2778). The cycle that carries the tick keeps 130 clocks
    // (so a write's data_en, offset 124, stays 5 clocks ahead of the tick,
    // as in the fixed clocking: the voice needs those clocks to take a
    // write before the sample it lands with, which is GSSquared's [access,
    // sample] order); the other n-1 cycles share the rest evenly, cycle j
    // getting floor((j+1)M/(n-1)) - floor(jM/(n-1)) of M = N - 130: 126 or
    // 127 clocks in a 22-cycle period, 132 or 133 in a 21-cycle one, so
    // every bus strobe (offsets up to 124) keeps its place. Before the first
    // tick, cycles are 130 clocks; the accumulator starts at 0 at the first
    // tick.
    int cycleLength(bool due) {
        phase_ += kSampleRate;
        bool mine = false;
        if (phase_ >= kRate) { phase_ -= kRate; mine = true; }
        if (mine != due) {
            std::fprintf(stderr, "rtl card: sample cadence differs from the driver's at cycle %llu\n",
                         (unsigned long long)ncycle_);
            std::exit(4);
        }
        ++ncycle_;
        if (!started_) {
            if (due) { started_ = true; startPeriod(); }
            return F_;
        }
        const int j = pos_++;
        const int m = per_n_ - 1;
        const uint64_t M = per_N_ - F_;
        const int len = due ? F_ : (int)(((uint64_t)(j + 1) * M) / m - ((uint64_t)j * M) / m);
        if (due) {
            if (pos_ != per_n_) { std::fprintf(stderr, "rtl card: period bookkeeping\n"); std::exit(4); }
            startPeriod();
        }
        return len;
    }
    void startPeriod() {
        // Apple cycles to the next tick, inclusive.
        uint64_t p = phase_;
        int n = 0;
        do { p += kSampleRate; ++n; } while (p < kRate);
        // Fabric clocks to the next carry of the card's accumulator.
        int N = 0;
        do { ++N; acc_ += kAccStep; } while (acc_ < (1ull << 32));
        acc_ -= (1ull << 32);
        per_n_ = n; per_N_ = N; pos_ = 0;
        ++periods_;
    }
    static constexpr uint64_t kRate = 1020484, kSampleRate = 48000, kAccStep = 1546188;
    bool card_clock_ = false, started_ = false;
    uint64_t phase_ = kRate - kSampleRate, acc_ = 0, ncycle_ = 0, periods_ = 0;
    int per_n_ = 1, per_N_ = 130, pos_ = 0;
    int16_t lr_l_ = 0, lr_r_ = 0;

    void clk() {
        top_->clk = 1; top_->eval();
        top_->clk = 0; top_->eval();
    }
    std::unique_ptr<Vtb_card> top_;
    int F_;
    int o_q3a_, o_q3a_end_, o_q3b_, o_q3b_end_, o_addr_, o_sss_, o_serve_, o_data_, o_rd_, o_tick_;
    uint32_t audio_control_;
    Sample s_{0, 0, 0, 0}, pre_{0, 0, 0, 0};
    View view_{false, {false, false}, {false, false}};
    FILE *dbg_ = nullptr;
    int dbg_post_ = 0;
    unsigned long long nsamp_ = 0;
    unsigned pre_pitch_ = 0, pre_ticks_ = 0, pre_clos_ = 0, pre_gate_ = 0, pre_noise_ = 0, pre_upd_ = 0, pre_filt_ = 0, pre_cur_ = 0;
};

