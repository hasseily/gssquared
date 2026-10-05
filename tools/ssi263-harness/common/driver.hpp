// Shared stimulus driver for the SSI-263 comparison harness.
//
// One Apple CPU cycle is the unit of time on both sides. A Card runs one
// cycle at a time: an optional bus access to the Phasor (slot 4), then the
// card's own clocks for that cycle. The 48 kHz sample cadence is computed
// here, once, with GSSquared's integer phase accumulator (mb2.cpp:236,
// PhasorLogic::advanceAudioSamplePhase, initial phase rate-48000), so both
// implementations produce sample n in the same Apple cycle.
//
// Inputs:
//   script (closed loop, each implementation follows its own responses):
//     mode mb|phasor|echo        mode-switch access ($C0C8 / $C0CD / $C0CF)
//     w ADDR VAL                 LDA #VAL; STA ADDR   (6 cycles, write last)
//     r ADDR                     LDA ADDR             (4 cycles, read last)
//     ssi P|S REG VAL            w to $C440+REG (P) or $C420+REG (S)
//     wait N                     N idle cycles
//     samples N                  idle until N more 48 kHz samples
//     waitd7 P|S [MAX]           native D7 poll: LDA $C440|$C420; BPL (7/loop)
//     waitifr P|S [MAX]          MB poll: LDA $C48D|$C40D; AND #2; BEQ (9/loop)
//     waitirq [MAX]              idle until IRQ, then 7 cycles of entry
//     reset N                    Apple RES low for N cycles
//     end
//   trace (open loop, from a2vm --phasor-log): "CYCLE R ADDR [VAL]",
//     "CYCLE W ADDR VAL", "CYCLE RES N", "CYCLE END" (hex ADDR/VAL,
//     decimal CYCLE). The recorded read value is kept for comparison.
//
// Outputs in OUTDIR: sec.pcm / pri.pcm (int16 LE mono, 48 kHz, the raw SSI
// voice outputs: socket A5 = secondary/left, A6 = primary/right), card.pcm
// (int16 LE stereo, the card output), events.txt: every write and every
// read (cycle, sample, address, value), the D7/IRQ/direct-IRQ edges, NOTE
// and TRACEREAD lines, END; "# impl" and "# settings" header lines.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>

struct Access {
    uint16_t addr;
    bool rw;       // true = read
    uint8_t data;  // write data
};

struct Sample {
    int16_t sec, pri, l, r;
};

class Card {
public:
    virtual ~Card() {}
    // Run one Apple cycle. Returns the value a read access sees (0x00 is the
    // floating bus in both harnesses).
    virtual uint8_t cycle(const Access *acc, bool sample_due, bool res_low) = 0;
    // The card state an access in the cycle just run observed: the IRQ line
    // the CPU samples, D7 of each socket (0 = secondary A5, 1 = primary A6)
    // and each socket's direct (native-mode) IRQ.
    struct View { bool irq, d7[2], dirq[2]; };
    virtual View view() = 0;
    virtual Sample sample() = 0;      // the sample produced by the last due cycle
    virtual const char *name() = 0;
    virtual std::string settings() { return ""; }
};

class Driver {
public:
    static constexpr uint64_t kRate = 1020484;  // NClock video cycles/s (NTSC)
    static constexpr uint64_t kSampleRate = 48000;

    Driver(Card &card, const std::string &outdir) : card_(card), outdir_(outdir) {
        sec_ = std::fopen((outdir + "/sec.pcm").c_str(), "wb");
        pri_ = std::fopen((outdir + "/pri.pcm").c_str(), "wb");
        crd_ = std::fopen((outdir + "/card.pcm").c_str(), "wb");
        ev_ = std::fopen((outdir + "/events.txt").c_str(), "w");
        if (!sec_ || !pri_ || !crd_ || !ev_) { std::perror("open outputs"); std::exit(2); }
        std::fprintf(ev_, "# impl %s\n", card_.name());
        // The renderer's own settings (the RTL card's clocking, tone
        // controls, PSG mute, debug output): run_suite.py checks a cached
        // reference was rendered with the settings it asks for.
        const std::string set = card_.settings();
        if (!set.empty()) std::fprintf(ev_, "# settings %s\n", set.c_str());
    }
    ~Driver() {
        std::fclose(sec_); std::fclose(pri_); std::fclose(crd_); std::fclose(ev_);
    }

    uint64_t cycle() const { return cycle_; }
    uint64_t samples() const { return samples_; }
    void setMaxCycles(uint64_t m) { max_cycles_ = m; }

    // One Apple cycle with an optional access.
    uint8_t step(const Access *acc, bool res_low = false) {
        phase_ += kSampleRate;
        bool due = false;
        if (phase_ >= kRate) { phase_ -= kRate; due = true; }
        uint8_t v = card_.cycle(acc, due, res_low);
        observe();
#ifdef INJECT_READ_CYCLE
        // Fault injection (negative tests only, never in a renderer under
        // test): the read at this cycle and address returns this value.
        if (acc && acc->rw && cycle_ == (uint64_t)(INJECT_READ_CYCLE) && acc->addr == (INJECT_READ_ADDR))
            v = (uint8_t)(INJECT_READ_VALUE);
#endif
        if (acc) {
            ++accesses_;
            // Every access is logged, reads included (cycle, sample, address,
            // value), so compare.py can require the two streams identical.
            if (acc->rw)
                std::fprintf(ev_, "%llu %llu R %04X %02X\n", (unsigned long long)cycle_,
                             (unsigned long long)samples_, acc->addr, v);
            else
                std::fprintf(ev_, "%llu %llu W %04X %02X\n", (unsigned long long)cycle_,
                             (unsigned long long)samples_, acc->addr, acc->data);
        }
        if (due) {
            Sample s = card_.sample();
            std::fwrite(&s.sec, 2, 1, sec_);
            std::fwrite(&s.pri, 2, 1, pri_);
            int16_t lr[2] = {s.l, s.r};
            std::fwrite(lr, 2, 2, crd_);
            ++samples_;
        }
        ++cycle_;
        if (cycle_ > max_cycles_) {
            std::fprintf(ev_, "%llu %llu ABORT max-cycles\n", (unsigned long long)cycle_,
                         (unsigned long long)samples_);
            finish();
            std::exit(3);
        }
        return v;
    }

    void idle(uint64_t n) { for (uint64_t i = 0; i < n; ++i) step(nullptr); }
    void write(uint16_t addr, uint8_t v) {
        idle(5);                 // LDA # (2), STA abs cycles 1-3
        Access a{addr, false, v};
        step(&a);
    }
    uint8_t read(uint16_t addr) {
        idle(3);
        Access a{addr, true, 0};
        return step(&a);
    }

    void note(const std::string &s) {
        std::fprintf(ev_, "%llu %llu NOTE %s\n", (unsigned long long)cycle_,
                     (unsigned long long)samples_, s.c_str());
    }

    void finish() {
        std::fprintf(ev_, "%llu %llu END accesses=%llu\n", (unsigned long long)cycle_,
                     (unsigned long long)samples_, (unsigned long long)accesses_);
        std::fflush(ev_);
    }

    int runScript(std::istream &in) {
        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            auto hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            std::istringstream ls(line);
            std::string op;
            if (!(ls >> op)) continue;
            if (op == "mode") {
                std::string m; ls >> m;
                uint16_t a = m == "mb" ? 0xC0C8 : (m == "echo" ? 0xC0CF : 0xC0CD);
                read(a);
            } else if (op == "w") {
                std::string a, v; ls >> a >> v;
                write(std::stoul(a, nullptr, 16), std::stoul(v, nullptr, 16));
            } else if (op == "r") {
                std::string a; ls >> a;
                read(std::stoul(a, nullptr, 16));
            } else if (op == "ssi") {
                std::string c, r, v; ls >> c >> r >> v;
                uint16_t base = (c == "P" || c == "p") ? 0xC440 : 0xC420;
                write(base + (std::stoul(r) & 7), std::stoul(v, nullptr, 16));
            } else if (op == "wait") {
                uint64_t n; ls >> n; idle(n);
            } else if (op == "samples") {
                uint64_t n; ls >> n;
                uint64_t target = samples_ + n;
                while (samples_ < target) step(nullptr);
            } else if (op == "waitd7" || op == "waitifr") {
                std::string c; ls >> c;
                uint64_t maxc = 4000000; ls >> maxc;
                bool pri = (c == "P" || c == "p");
                uint64_t start = cycle_;
                bool ok = false;
                while (cycle_ - start < maxc) {
                    if (op == "waitd7") {
                        uint8_t v = read(pri ? 0xC440 : 0xC420);
                        if (v & 0x80) { ok = true; break; }
                        idle(3);  // BPL taken
                    } else {
                        uint8_t v = read(pri ? 0xC48D : 0xC40D);
                        idle(2);  // AND #
                        if (v & 0x02) { ok = true; idle(2); break; }
                        idle(3);  // BEQ taken
                    }
                }
                if (!ok) note(op + " TIMEOUT");
            } else if (op == "waitirq") {
                uint64_t maxc = 4000000; ls >> maxc;
                uint64_t start = cycle_;
                bool ok = false;
                while (cycle_ - start < maxc) {
                    step(nullptr);
                    if (card_.view().irq) { ok = true; break; }
                }
                if (ok) idle(7); else note("waitirq TIMEOUT");
            } else if (op == "reset") {
                uint64_t n; ls >> n;
                for (uint64_t i = 0; i < n; ++i) step(nullptr, true);
            } else if (op == "note") {
                std::string rest; std::getline(ls, rest); note(rest);
            } else if (op == "end") {
                break;
            } else {
                std::fprintf(stderr, "script line %d: unknown op %s\n", lineno, op.c_str());
                return 2;
            }
        }
        finish();
        return 0;
    }

    int runTrace(std::istream &in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            unsigned long long c; std::string op;
            if (!(ls >> c >> op)) continue;
            if (c < cycle_) {
                std::fprintf(stderr, "trace goes back in time at cycle %llu\n", c);
                return 2;
            }
            while (cycle_ < c) step(nullptr);
            if (op == "R" || op == "W") {
                std::string a, v; ls >> a; ls >> v;
                Access acc{(uint16_t)std::stoul(a, nullptr, 16), op == "R",
                           (uint8_t)(op == "W" ? std::stoul(v, nullptr, 16) : 0)};
                uint8_t got = step(&acc);
                if (op == "R" && !v.empty()) {
                    unsigned exp = std::stoul(v, nullptr, 16);
                    if (exp != got)
                        std::fprintf(ev_, "%llu %llu TRACEREAD %04X trace=%02X here=%02X\n",
                                     c, (unsigned long long)samples_ - 0, acc.addr, exp, got);
                }
            } else if (op == "RES") {
                uint64_t n; ls >> n;
                for (uint64_t i = 0; i < n; ++i) step(nullptr, true);
            } else if (op == "END") {
                break;
            } else if (op == "NOTE") {
                std::string rest; std::getline(ls, rest); note(rest);
            }
        }
        finish();
        return 0;
    }

private:
    void observe() {
        Card::View w = card_.view();
        bool i = w.irq;
        bool d0 = w.d7[0], d1 = w.d7[1];
        bool q0 = w.dirq[0], q1 = w.dirq[1];
        if (first_ || i != irq_) std::fprintf(ev_, "%llu %llu IRQ %d\n", (unsigned long long)cycle_, (unsigned long long)samples_, i);
        if (first_ || d0 != d7_[0]) std::fprintf(ev_, "%llu %llu D7S %d\n", (unsigned long long)cycle_, (unsigned long long)samples_, d0);
        if (first_ || d1 != d7_[1]) std::fprintf(ev_, "%llu %llu D7P %d\n", (unsigned long long)cycle_, (unsigned long long)samples_, d1);
        if (first_ || q0 != dq_[0]) std::fprintf(ev_, "%llu %llu DIRQS %d\n", (unsigned long long)cycle_, (unsigned long long)samples_, q0);
        if (first_ || q1 != dq_[1]) std::fprintf(ev_, "%llu %llu DIRQP %d\n", (unsigned long long)cycle_, (unsigned long long)samples_, q1);
        irq_ = i; d7_[0] = d0; d7_[1] = d1; dq_[0] = q0; dq_[1] = q1;
        first_ = false;
    }

    Card &card_;
    std::string outdir_;
    FILE *sec_, *pri_, *crd_, *ev_;
    uint64_t cycle_ = 0;
    uint64_t samples_ = 0;
    uint64_t accesses_ = 0;
    uint64_t phase_ = kRate - kSampleRate;
    uint64_t max_cycles_ = 400000000ULL;
    bool first_ = true;
    bool irq_ = false, d7_[2] = {false, false}, dq_[2] = {false, false};
};

inline int driverMain(Card &card, int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s script|trace INPUT OUTDIR [max_cycles]\n", argv[0]);
        return 2;
    }
    std::string kind = argv[1];
    std::ifstream in(argv[2]);
    if (!in) { std::perror(argv[2]); return 2; }
    Driver d(card, argv[3]);
    if (argc > 4) d.setMaxCycles(std::strtoull(argv[4], nullptr, 10));
    int rc = kind == "trace" ? d.runTrace(in) : d.runScript(in);
    std::printf("%s: %llu cycles, %llu samples\n", card.name(),
                (unsigned long long)d.cycle(), (unsigned long long)d.samples());
    return rc;
}
