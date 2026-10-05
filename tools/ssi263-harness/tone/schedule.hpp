// The tone stage's clock schedule, shared by tone_check (C++ model) and
// tone_rtl (the verbatim RTL block under Verilator): which fabric clock edge
// each 48 kHz tick falls on, when each new speech sample reaches the mixer,
// and after which edge the card output is read. Mirrors rtl/rtl_card.hpp.
//
// Edges are counted from the first clock after the bench's power-on reset
// (the tone registers are all 0 until then). Apple cycle 0 is 130 clocks and
// carries tick 0 on its last edge (129) in both clockings.
//   card  (bin/card_rtl's default, the real card): tick k+1 is 2777 or 2778
//         edges after tick k (CardTick, accumulator 0 at tick 0); the card
//         output of sample k is audio_l after edge tick_k - 3 (the top
//         level's sampling, see rtl_card.hpp).
//   fixed (SSI_CLOCK=fixed, the old harness): 130 edges an Apple cycle, the
//         Apple cycles of each sample period from the driver's accumulator
//         (21 or 22); output after edge tick_k - 1.
// The speech sample k (sec.pcm/pri.pcm[k], the voice output as it was
// before tick k) is the mixer input from edge tick_{k-1} + D + 1 on, D
// being the voice's output latency after the tick (measured: see README).
#pragma once
#include "tone_model.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

namespace tone {

struct Schedule {
    std::vector<int64_t> tick;     // edge of tick k
    std::vector<int64_t> out;      // edge after which sample k is read
    std::vector<int64_t> sw;       // first edge at which input k applies
};

inline Schedule makeSchedule(size_t nsamples, bool card, int d) {
    Schedule s;
    s.tick.resize(nsamples); s.out.resize(nsamples); s.sw.resize(nsamples);
    CardTick ct;
    uint64_t phase = 1020484 - 48000;
    // the driver's accumulator: cycle 0 is due; count the cycles of each period
    auto cyclesToNext = [&]() {
        int n = 0;
        do { phase += 48000; ++n; } while (phase < 1020484);
        phase -= 1020484;
        return n;
    };
    cyclesToNext();   // cycle 0 (tick 0)
    for (size_t k = 0; k < nsamples; ++k) {
        if (k == 0) s.tick[k] = 129;
        else {
            const int n = cyclesToNext();
            s.tick[k] = s.tick[k - 1] + (card ? ct.next() : 130 * n);
        }
        s.out[k] = s.tick[k] - (card ? 3 : 1);
        s.sw[k] = k == 0 ? 0 : s.tick[k - 1] + d + 1;
    }
    return s;
}

}  // namespace tone
