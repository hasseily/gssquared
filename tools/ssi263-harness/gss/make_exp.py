#!/usr/bin/env python3
"""Attribution experiments (analysis only; no source tree is touched).

usage: make_exp.py SRC_TREE OUT_CPP   (build.sh exp runs it: SRC_TREE is
       d15bdbf9's src/ extracted from this repository's history, OUT_CPP
       WORK/obj/exp/SSI263_exp.cpp)

Writes OUT_CPP: a copy of SRC_TREE's SSI263.cpp (the code before the fix,
d15bdbf9; the patterns below are written against it) in which
each candidate cause of a GSSquared-vs-RTL difference can be switched to the
RTL's behaviour with a compile-time flag. Building card_gss against it with
some flags on shows how much of the measured difference each cause accounts
for (run_suite.py --gss bin/card_gss_<tag> --tag <tag>).

  EXP_FREERUN  the SC-01 control core (20 kHz update phase, pitch, noise,
               interpolation) runs from power-on and whatever the voice is
               doing, toward the RTL's reset targets before the first phone
               (sc01a_digital_core.sv:557-621, 623-725: no enable input)
  EXP_CTLRUN   CTL=1 does not stop the duration/response counters or the
               core; only A/R and the excitation are masked
               (ssi263_bus_wrapper.sv:204, backend:1585-1594)
  EXP_START    a phone start drops the in-flight sample only when the last
               sample tick was within the backend pipeline (the cycle
               before), not always (backend:1061-1076, 1564-1566)
  EXP_PITCHLAG pitch_limit from the inflection before this tick's update
               (sc01a_digital_core.sv:713-714)
  EXP_F1TAP0   F1's input-tap coefficient from the filter index before a
               commit that lands during this sample (backend:1191-1215)
  EXP_FCMUTE   noise-mix FC latched as 0 while muted (backend:1585-1594)
  EXP_VIAREAD  (card, not SSI; in gss/card_gss.cpp) a native-mode T1C-L/T2C-L
               read returns the counter before its extra tick, as the RTL
               serves the value at serve_en and ticks at data_en
  EXP_ATTACKLATE the consonant-attack stage near the end of the pipeline
               reads the live frame counter, so an XCK frame boundary that
               lands while the sample is in flight already counts
               (backend:1521-1532 SYNTH_ATTACK_BOOST, ticks_q mirror)
"""
import re, sys
from pathlib import Path

if len(sys.argv) != 3:
    sys.exit(__doc__)
SRC = Path(sys.argv[1]) / 'src' / 'devices' / 'mockingboard' / 'SSI263.cpp'
OUT = Path(sys.argv[2])


def sub(s, old, new, count=1):
    if s.count(old) < 1:
        sys.exit(f'pattern not found: {old[:80]!r}')
    return s.replace(old, new, count)


def main():
    s = SRC.read_text()
    s = sub(s, '#include "SSI263.hpp"', '#include "devices/mockingboard/SSI263.hpp"')

    # FREERUN: RTL reset targets for the core before the first phone.
    s = sub(s, """        *this = FormantCore{};
        phone = decodePhone(0x3F);""", """        *this = FormantCore{};
        phone = decodePhone(0x3F);
#ifdef EXP_FREERUN
        // sc01a_digital_core.sv reset_state: rom_* reset values.
        phone.f1 = 7; phone.f2 = 9; phone.f2q = 4; phone.f3 = 0xC;
        phone.fa = 0; phone.fc = 0; phone.va = 0;
        phone.closure_delay = 1; phone.voice_delay = 1;
        phone.closure = true; phone.pause = false;
#endif""")

    # PITCHLAG
    s = sub(s, """        advanceInflection(current_function, registers);
        pitch_limit = pitchPeriod(active_inflection);""", """#ifdef EXP_PITCHLAG
        pitch_limit = pitchPeriod(active_inflection);
        advanceInflection(current_function, registers);
#else
        advanceInflection(current_function, registers);
        pitch_limit = pitchPeriod(active_inflection);
#endif""")

    # F1TAP0
    s = sub(s, """        const int32_t f1 = f1_.process(
            voice_input, coefficients.f1[filter_core.filt_f1 & 0x0F]);""", """#ifdef EXP_F1TAP0
        auto f1_coeffs = coefficients.f1[filter_core.filt_f1 & 0x0F];
        f1_coeffs[0] = coefficients.f1[sample_core.filt_f1 & 0x0F][0];
        const int32_t f1 = f1_.process(voice_input, f1_coeffs);
#else
        const int32_t f1 = f1_.process(
            voice_input, coefficients.f1[filter_core.filt_f1 & 0x0F]);
#endif""")

    # FCMUTE: the render call knows `excitation`.
    s = sub(s, """            scale20(fn, static_cast<uint8_t>(
                5 + (0x0F ^ sample_core.filt_fc))));""", """            scale20(fn, static_cast<uint8_t>(
#ifdef EXP_FCMUTE
                5 + (0x0F ^ (excitation ? sample_core.filt_fc : 0)))));
#else
                5 + (0x0F ^ sample_core.filt_fc))));
#endif""")

    # START
    s = sub(s, """    void startPhone() {""", """    void startPhone(bool in_pipeline = true) {
#ifdef EXP_START
        if (!in_pipeline) { resetHistory(); return; }
#endif""")
    s = sub(s, """        synth.startPhone();
        active = true;""", """#ifdef EXP_START
        synth.startPhone(xck_since_render <= 1);
#else
        synth.startPhone();
#endif
        active = true;""")
    s = sub(s, """    void clockXck() {
        bool response_boundary = false;
""", """    void clockXck() {
        bool response_boundary = false;
#ifdef EXP_START
        if (xck_since_render < 1000) ++xck_since_render;
#endif
""")
    s = sub(s, """    float generateSample() {
        const uint8_t amplitude""", """    float generateSample() {
#ifdef EXP_START
        xck_since_render = 0;
#endif
        const uint8_t amplitude""")
    s = sub(s, """    uint32_t samples_elapsed = 0;
};""", """    uint32_t samples_elapsed = 0;
    uint32_t xck_since_render = 1000;
};""")

    # ATTACKLATE
    s = sub(s, """    bool filter_dirty = true;
    bool phone_done = false;""", """    bool filter_dirty = true;
    bool phone_done = false;
    uint8_t late_ticks = 0;     // EXP_ATTACKLATE: ticks the attack stage sees""")
    s = sub(s, """        if (core.ticks < start_tick) {
            return 0;
        }
        const uint8_t age = core.ticks - start_tick;""", """#ifdef EXP_ATTACKLATE
        const uint8_t ticks_seen = core.late_ticks;
#else
        const uint8_t ticks_seen = core.ticks;
#endif
        if (ticks_seen < start_tick) {
            return 0;
        }
        const uint8_t age = ticks_seen - start_tick;""")
    s = sub(s, """        const float sample = synth.render(
            sample_core, core, excite, excite ? amplitude : 0);""", """#ifdef EXP_ATTACKLATE
        // The next XCK edge lands ~69 fabric clocks after this tick, before
        // SYNTH_ATTACK_BOOST (~140): a frame boundary on it is already seen.
        core.late_ticks = core.ticks;
        if (active && duration_active && duration_ticks_left == 1)
            core.late_ticks = core.ticks == 0x0F ? 0 : core.ticks + 1;
#endif
        const float sample = synth.render(
            sample_core, core, excite, excite ? amplitude : 0);""")

    # CTLRUN: CTL=1 keeps the counters (and the core) going.
    s = sub(s, """        ready = false;
        active = false;
        completion_pending = false;
        interrupts_enabled = false;
        acknowledge_guard = false;
        response_active = false;
        duration_active = false;
        response_ticks_left = 0;
        duration_ticks_left = 0;
        response_slot = 0;
        duration_frame = 0;
        samples_remaining = 0;
        samples_total = 0;
        samples_elapsed = 0;
    }

    void latchModeAndInterrupts() {""", """#ifdef EXP_CTLRUN
        // The RTL wrapper only clears D7 and the IRQ (wrapper:358-361);
        // interrupt enable and the counters are untouched.
        ready = false;
        completion_pending = false;
        return;
#endif
        ready = false;
        active = false;
        completion_pending = false;
        interrupts_enabled = false;
        acknowledge_guard = false;
        response_active = false;
        duration_active = false;
        response_ticks_left = 0;
        duration_ticks_left = 0;
        response_slot = 0;
        duration_frame = 0;
        samples_remaining = 0;
        samples_total = 0;
        samples_elapsed = 0;
    }

    void latchModeAndInterrupts() {""")

    # FREERUN: the core advances every sample.
    s = sub(s, """        const FormantCore sample_core = core;
        if (active) {
            core.advanceSample(current_function, registers);
            core.filter_dirty = false;
        }""", """        const FormantCore sample_core = core;
#ifdef EXP_FREERUN
        if (true) {
#else
        if (active) {
#endif
            core.advanceSample(current_function, registers);
            core.filter_dirty = false;
        }""")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    text = '// GENERATED by make_exp.py from d15bdbf9 (before the fix); analysis only.\n' + s
    # Rewritten only when it changes, so its mtime (a source of every
    # bin/card_gss_<tag>, see build.sh's records) moves only with its content.
    if OUT.exists() and OUT.read_text() == text:
        print('unchanged', OUT)
    else:
        OUT.write_text(text)
        print('wrote', OUT)


if __name__ == '__main__':
    main()
