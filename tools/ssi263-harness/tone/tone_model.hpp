// A per-fabric-clock C++ model of one channel of the Phasor's tone stage:
// appletini-one hdl/apple/mockingboard.sv, the `final_audio_mix` block and
// the functions it calls (pcm_to_tone_fp, tone_fp_to_pcm,
// audio_control_adjust, warmth_treble_adjust, sat16_from21,
// warm_shape_from21, clamp_audio_control, mix_speech with the PSGs silent).
// Every register of the block is a member here and step() is one clock edge:
// each new value is computed from the old ones, as the nonblocking
// assignments do. tone/tone_check.cpp proves it bit-exact against the RTL
// (bin/card_rtl's card.pcm, and tone/tone_rtl's verbatim extraction).
#pragma once
#include <cstdint>

namespace tone {

inline int32_t asr(int64_t v, unsigned s) {             // >>> on a signed value (floor)
    return static_cast<int32_t>(v >= 0 ? (v >> s) : -((-v + ((int64_t{1} << s) - 1)) >> s));
}
inline int32_t clampControl(uint32_t raw5) {            // clamp_audio_control
    int32_t v = static_cast<int32_t>(raw5 & 31u);
    if (v & 16) v -= 32;
    return v > 8 ? 8 : (v < -8 ? -8 : v);
}
inline int32_t fpToPcm(int32_t s) {                     // tone_fp_to_pcm: s[27:12]
    return static_cast<int16_t>(static_cast<uint16_t>((static_cast<uint32_t>(s) >> 12) & 0xFFFFu));
}
inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
inline int32_t adjust(int32_t sample, int32_t control) { // audio_control_adjust
    const int32_t w = sample;
    const int32_t m = control < 0 ? -control : control;
    int32_t a;
    switch (m & 15) {
    case 0: a = 0; break;
    case 1: a = asr(w, 3); break;
    case 2: a = asr(w, 2); break;
    case 3: a = asr(w, 2) + asr(w, 3); break;
    case 4: a = asr(w, 1); break;
    case 5: a = asr(w, 1) + asr(w, 3); break;
    case 6: a = asr(w, 1) + asr(w, 2); break;
    case 7: a = asr(w, 1) + asr(w, 2) + asr(w, 3); break;
    default: a = w; break;
    }
    return control < 0 ? -a : a;
}
inline int32_t warmthTrebleAdjust(int32_t sample, int32_t control) { return -asr(adjust(sample, control), 2); }
inline int32_t warmShape(int32_t s, int32_t warmth) {  // warm_shape_from21
    if (warmth > 0) {
        const int32_t knee = 28672 - (warmth << 10);
        if (s > knee) { const int32_t e = s - knee; s = knee + asr(e, 1) + asr(e, 3); }
        else if (s < -knee) { const int32_t e = -knee - s; s = -knee - asr(e, 1) - asr(e, 3); }
    }
    return s;
}

struct Channel {
    // registers (one channel of each pair)
    int32_t base = 0, low = 0, warm_lp = 0, mid_lp = 0, apply_base = 0;
    int32_t bass = 0, warm = 0, mid = 0, treble = 0;
    int32_t c_bass = 0, c_mid = 0, c_treble = 0, c_warm = 0, c_volume = 0;
    int32_t base_ext = 0, a_bass = 0, a_warm = 0, a_wt = 0, a_mid = 0, a_treble = 0, a_volume = 0;
    int32_t shaped = 0, warm_shaped = 0, audio = 0;

    // One clock edge. `in` is the channel's speech input (ssiN_audio) as
    // it is before the edge; audio_control is the card's control word.
    void step(int32_t in, uint32_t audio_control) {
        Channel o = *this;
        base = sat16(0 + in);                               // mix_speech(PSG 0, speech)
        const int32_t fpb = o.base * 4096;                  // pcm_to_tone_fp(tone_base_q)
        low = o.low + asr(static_cast<int64_t>(fpb) - o.low, 16);
        warm_lp = o.warm_lp + asr(static_cast<int64_t>(fpb) - o.warm_lp, 14);
        mid_lp = o.mid_lp + asr(static_cast<int64_t>(fpb) - o.mid_lp, 13);
        const int32_t l_low = fpToPcm(o.low), l_warm = fpToPcm(o.warm_lp), l_mid = fpToPcm(o.mid_lp);
        apply_base = o.base;
        bass = l_low;
        warm = sat16(l_warm - l_low);
        mid = sat16(l_mid - l_low);
        treble = sat16(o.base - l_mid);
        c_bass = clampControl(audio_control);
        c_mid = clampControl(audio_control >> 5);
        c_treble = clampControl(audio_control >> 10);
        c_warm = clampControl(audio_control >> 15);
        c_volume = clampControl(audio_control >> 20);
        base_ext = o.apply_base;
        a_bass = adjust(o.bass, o.c_bass);
        a_warm = adjust(o.warm, o.c_warm);
        a_wt = warmthTrebleAdjust(o.treble, o.c_warm);
        a_mid = adjust(o.mid, o.c_mid);
        a_treble = adjust(o.treble, o.c_treble);
        a_volume = adjust(o.apply_base, o.c_volume);
        shaped = o.base_ext + o.a_bass + o.a_warm + o.a_mid + o.a_treble + o.a_wt + o.a_volume;
        warm_shaped = warmShape(o.shaped, o.c_warm);
        audio = sat16(o.warm_shaped);
    }
};

// Clocking. The card's 48 kHz tick (appletini_yarz_top.sv): a 32-bit
// accumulator gains 1546188 every 133.33 MHz clock and the tick is its
// carry, so ticks are 2777 or 2778 clocks apart.
struct CardTick {
    uint64_t acc = 0;
    int next() {
        int n = 0;
        do { ++n; acc += 1546188; } while (acc < (1ull << 32));
        acc -= (1ull << 32);
        return n;
    }
};

}  // namespace tone
