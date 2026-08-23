/*
 * SSI-263A formant speech synthesis for GSSquared.
 *
 * The SC-01A digital-control structure, ROM decoding, analog vocal-tract
 * topology, and switched-capacitor filter equations are based on Olivier
 * Galibert's vsim and MAME Votrax work. The SSI-263 register controls and
 * SC-01A parameter mapping follow the Appletini SystemVerilog implementation.
 *
 * Copyright (c) 2015 Olivier Galibert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of vsim nor the names of its contributors may be used to
 *   endorse or promote products derived from this software without specific
 *   prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "SSI263.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
// The digital parameter core advances at the SC-01A-compatible 20 kHz rate.
// FILFREQ does not change that timing; it programs the separate switched-cap
// clock used by every analog vocal-tract filter.
constexpr double kControlClockHz = 20000.0;
constexpr double kSsi263XckHz = 1000000.0;
constexpr uint32_t kFilterTailSamples = 512;
constexpr double kOutputSoftKnee = 0.25;
constexpr double kOutputSoftRatio = 0.25;
constexpr double kOutputAnalogRail = 0.95;
constexpr double kOutputSlewStep = 3000.0 / 32768.0;

double filterClockHz(uint8_t filter_frequency) {
    // SSI-263A data sheet: ffilter = XCK / (2 * (256 - FF)).  $FF is
    // handled as the documented/silicon-compatible silence setting by the
    // output path, so avoid constructing a needlessly ultrasonic filter bank
    // for that value.
    if (filter_frequency == 0xFF) {
        filter_frequency = 0xE7;
    }
    return kSsi263XckHz /
           (2.0 * static_cast<double>(256U - filter_frequency));
}

// SSI-263 phoneme/allophone starting targets in the compatible SC-01A tract
// parameter space.  Appletini's original inverse table left nineteen valid
// SSI codes mapped to STOP.  This explicit table covers every non-hold SSI
// symbol; the few places where the two chips allocate fricative energy
// differently are refined after lookup.  HV/HVC/HFC/HN are stateful and are
// handled before this table.
constexpr std::array<uint8_t, 64> kSsi263ToSc01a = {
    // PA, E,  E1, Y,  Y1, AY, IE, I
    0x03, 0x2C, 0x00, 0x22, 0x22, 0x29, 0x29, 0x27,
    // A,  AI, EH, EH1, AE, AE1, AH, AH1
    0x05, 0x21, 0x3B, 0x02, 0x2E, 0x2F, 0x24, 0x15,
    // AW, O,  OU, OO, IU, IU1, U,  U1
    0x13, 0x26, 0x35, 0x17, 0x36, 0x17, 0x28, 0x37,
    // UH, UH1, UH2, UH3, ER, R, R1, R2
    0x33, 0x32, 0x31, 0x23, 0x3A, 0x2B, 0x2B, 0x2B,
    // L, L1, LF, W, B, D, KV, P
    0x18, 0x18, 0x18, 0x2D, 0x0E, 0x1E, 0x1C, 0x25,
    // T, K, HV, HVC, HF, HFC, HN, Z
    0x04, 0x19, 0x03, 0x03, 0x1B, 0x03, 0x03, 0x12,
    // S, J, SCH, V, F, THV, TH, M
    0x1F, 0x1A, 0x11, 0x0F, 0x1D, 0x38, 0x39, 0x0C,
    // N, NG, :A, :OH, :U, :UH, E2, LB
    0x0D, 0x14, 0x06, 0x34, 0x36, 0x36, 0x00, 0x18,
};

constexpr bool isHoldPhone(uint8_t phone) {
    // SSI-263 phonemes that explicitly retain the current vocal-tract target:
    // HV, HVC, HFC, and HN. They have no direct SC-01A equivalent and must
    // not fall through to the STOP entry in the compatibility map.
    return phone == 0x2A || phone == 0x2B ||
           phone == 0x2D || phone == 0x2E;
}

// Decoded from sc01a.bin (CRC32 fc416227, SHA1
// 1d6da90b1807a01b5e186ef08476119a862b5e6d). These are control parameters,
// not audio samples.
constexpr std::array<uint64_t, 64> kSc01aRom = {
    0x0000036174688127ULL, 0x01000161D4688127ULL,
    0x020009A1C4688127ULL, 0x030000E0F0A050A4ULL,
    0x040000FB610316E8ULL, 0x0500016164C9C1A6ULL,
    0x060007A134C9C1A6ULL, 0x07000463F3CB546CULL,
    0x08000161C4E940A3ULL, 0x09000B61806191A6ULL,
    0x0A000A61906191A6ULL, 0x0B0009A1906191A6ULL,
    0x0C0007A366A58832ULL, 0x0D000A61E6241936ULL,
    0x0E00017390E19122ULL, 0x0F000163F7D36428ULL,
    0x10000163FB8B546CULL, 0x110009A2FB8B546CULL,
    0x120001639CD15860ULL, 0x130008A0706980A3ULL,
    0x140009A0D4084B36ULL, 0x150008A184E940A3ULL,
    0x160007A130498123ULL, 0x17000A2120498123ULL,
    0x180007A1F409D0A2ULL, 0x19000A721123642CULL,
    0x1A0000E8DB7B342CULL, 0x1B000162FD2204ACULL,
    0x1C000173E041C126ULL, 0x1D0007A265832CA8ULL,
    0x1E000B7C00E89126ULL, 0x1F000468489132E0ULL,
    0x20000A2184C9C1A6ULL, 0x210005617069D326ULL,
    0x22000A6164A01226ULL, 0x230000E3548981A3ULL,
    0x24000CC184E940A3ULL, 0x250007B2631324A8ULL,
    0x26000A2184E8C1A2ULL, 0x27000A21806191A6ULL,
    0x28000A2180E8C122ULL, 0x290007A164015326ULL,
    0x2A000172E81132E0ULL, 0x2B00046354084382ULL,
    0x2C000A207049D326ULL, 0x2D000A661460C122ULL,
    0x2E000A2074E880A7ULL, 0x2F0007A074E880A7ULL,
    0x30000461606980A3ULL, 0x31000163548981A3ULL,
    0x320007A1E48981A3ULL, 0x33000A21B48981A3ULL,
    0x34000A6134E8C1A2ULL, 0x350009A180E8C1A2ULL,
    0x36000366106083A2ULL, 0x3700046190E8C122ULL,
    0x38000A6388E15220ULL, 0x39000168183800A4ULL,
    0x3A0008A12448C382ULL, 0x3B000A2194688127ULL,
    0x3C0009A19049D326ULL, 0x3D000CC1B06980A3ULL,
    0x3E000A2300A050A4ULL, 0x3F0000F030A058A4ULL,
};

constexpr uint8_t bit(uint64_t word, unsigned position) {
    return static_cast<uint8_t>((word >> position) & 1ULL);
}

constexpr uint8_t bits4(uint64_t word, unsigned b3, unsigned b2,
                        unsigned b1, unsigned b0) {
    return static_cast<uint8_t>((bit(word, b3) << 3) |
                                (bit(word, b2) << 2) |
                                (bit(word, b1) << 1) |
                                bit(word, b0));
}

struct PhoneParameters {
    uint8_t fa = 0;
    uint8_t fc = 0;
    uint8_t va = 0;
    uint8_t f1 = 0;
    uint8_t f2 = 0;
    uint8_t f2q = 0;
    uint8_t f3 = 0;
    uint8_t closure_delay = 0;
    uint8_t voice_delay = 0;
    uint8_t duration = 0;
    bool closure = false;
    bool pause = false;
};

PhoneParameters decodePhone(uint8_t phone) {
    const uint64_t word = kSc01aRom[phone & 0x3F];
    PhoneParameters result;
    result.f1 = bits4(word, 0, 7, 14, 21);
    result.va = bits4(word, 1, 8, 15, 22);
    result.f2 = bits4(word, 2, 9, 16, 23);
    result.fc = bits4(word, 3, 10, 17, 24);
    result.f2q = bits4(word, 4, 11, 18, 25);
    result.f3 = bits4(word, 5, 12, 19, 26);
    result.fa = bits4(word, 6, 13, 20, 27);
    result.closure_delay = bits4(word, 34, 32, 30, 28);
    result.voice_delay = bits4(word, 35, 33, 31, 29);
    result.closure = bit(word, 36) != 0;
    result.duration = static_cast<uint8_t>(((!bit(word, 37)) << 6) |
        ((!bit(word, 38)) << 5) | ((!bit(word, 39)) << 4) |
        ((!bit(word, 40)) << 3) | ((!bit(word, 41)) << 2) |
        ((!bit(word, 42)) << 1) | (!bit(word, 43)));
    result.pause = phone == 0x03 || phone == 0x3E;
    return result;
}

class DigitalFilter {
public:
    void configure(const std::array<double, 4> &a,
                   const std::array<double, 4> &b,
                   size_t numerator_count, size_t denominator_count) {
        a_ = a;
        b_ = b;
        numerator_count_ = numerator_count;
        denominator_count_ = denominator_count;
    }

    void reset() {
        x_.fill(0.0);
        y_.fill(0.0);
    }

    double process(double input) {
        for (size_t i = x_.size() - 1; i > 0; --i) {
            x_[i] = x_[i - 1];
        }
        x_[0] = input;

        double total = 0.0;
        for (size_t i = 0; i < numerator_count_; ++i) {
            total += x_[i] * a_[i];
        }
        for (size_t i = 1; i < denominator_count_; ++i) {
            total -= y_[i - 1] * b_[i];
        }

        double output = total / b_[0];
        if (!std::isfinite(output)) {
            reset();
            output = 0.0;
        }
        for (size_t i = y_.size() - 1; i > 0; --i) {
            y_[i] = y_[i - 1];
        }
        y_[0] = output;
        return output;
    }

private:
    std::array<double, 4> a_{};
    std::array<double, 4> b_{1.0, 0.0, 0.0, 0.0};
    std::array<double, 4> x_{};
    std::array<double, 4> y_{};
    size_t numerator_count_ = 1;
    size_t denominator_count_ = 1;
};

double bitsToCaps(uint32_t value, std::initializer_list<double> caps) {
    double total = 0.0;
    for (double cap : caps) {
        if (value & 1U) {
            total += cap;
        }
        value >>= 1;
    }
    return total;
}

void buildStandardFilter(DigitalFilter &filter, double filter_clock_hz,
                         double c1t, double c1b, double c2t, double c2b,
                         double c3, double c4) {
    const double k0 = c1t / (filter_clock_hz * c1b);
    const double k1 = c4 * c2t / (filter_clock_hz * c1b * c3);
    const double k2 = c4 * c2b /
                      (filter_clock_hz * filter_clock_hz * c1b * c3);
    const double peak = std::sqrt(std::fabs(k0 * k1 - k2)) /
                        (2.0 * kPi * k2);
    const double zc = 2.0 * kPi * peak /
                      std::tan(kPi * peak / SSI263::kSampleRate);
    const double m0 = zc * k0;
    const double m1 = zc * k1;
    const double m2 = zc * zc * k2;

    // The expanded bilinear form has an exact common (1 + z^-1) factor
    // in its numerator and denominator. Leaving that canceled pair in a
    // direct-form implementation creates a neutral pole at z=-1. Normal
    // excitation cannot reach it, but changing formant coefficients while
    // retaining analog history does; its Nyquist component then accumulates
    // across phones until the output becomes alternating garbage/beeps.
    // Divide out the common factor and implement the mathematically
    // equivalent, internally stable second-order section.
    filter.configure({1.0 + m0, 2.0, 1.0 - m0, 0.0},
                     {1.0 + m1 + m2, 2.0 - 2.0 * m2,
                      1.0 - m1 + m2, 0.0}, 3, 3);
}

void buildLowpassFilter(DigitalFilter &filter, double filter_clock_hz,
                        double c1t, double c1b) {
    const double k = c1b / (filter_clock_hz * c1t) * (150.0 / 4000.0);
    const double peak = 1.0 / (2.0 * kPi * k);
    const double zc = 2.0 * kPi * peak /
                      std::tan(kPi * peak / SSI263::kSampleRate);
    const double m = zc * k;
    filter.configure({1.0, 0.0, 0.0, 0.0},
                     {1.0 + m, 1.0 - m, 0.0, 0.0}, 1, 2);
}

void buildNoiseShaperFilter(DigitalFilter &filter, double filter_clock_hz,
                            double c1, double c2t, double c2b, double c3,
                            double c4) {
    const double k0 = c2t * c3 * c2b / c4;
    const double k1 = c2t * (filter_clock_hz * c2b);
    const double k2 = c1 * c2t * c3 / (filter_clock_hz * c4);
    const double peak = std::sqrt(1.0 / k2) / (2.0 * kPi);
    const double zc = 2.0 * kPi * peak /
                      std::tan(kPi * peak / SSI263::kSampleRate);
    const double m0 = zc * k0;
    const double m1 = zc * k1;
    const double m2 = zc * zc * k2;
    filter.configure({m0, 0.0, -m0, 0.0},
                     {1.0 + m1 + m2, 2.0 - 2.0 * m2,
                      1.0 - m1 + m2, 0.0}, 3, 3);
}

uint16_t inflectionWord(const std::array<uint8_t, 5> &registers) {
    return static_cast<uint16_t>(((registers[2] & 0x08) ? 0x0800 : 0) |
        (static_cast<uint16_t>(registers[1]) << 3) | (registers[2] & 0x07));
}

uint16_t pitchPeriod(uint16_t inflection) {
    const uint32_t span = 4096U - inflection;
    return static_cast<uint16_t>(std::max<uint32_t>(1, (span * 5U) >> 5));
}

uint8_t articulationShift(uint8_t articulation) {
    switch (articulation & 0x07) {
        case 0:
        case 1: return 5;
        case 2:
        case 3: return 4;
        case 4:
        case 5: return 3;
        case 6: return 2;
        default: return 1;
    }
}

uint8_t interpolate8(uint8_t current, uint8_t target,
                     uint8_t articulation) {
    const uint16_t target_value = static_cast<uint16_t>(target) << 4;
    if (target_value == current) {
        return current;
    }
    const uint16_t delta = target_value > current
        ? target_value - current : current - target_value;
    const uint16_t step = std::max<uint16_t>(1,
        delta >> articulationShift(articulation));
    if (target_value > current) {
        return static_cast<uint8_t>(std::min<uint16_t>(255, current + step));
    }
    return static_cast<uint8_t>(step > current ? 0 : current - step);
}

struct FormantCore {
    PhoneParameters phone{};
    uint8_t sc01_phone = 0x3F;

    uint32_t control_accumulator = 0;
    uint16_t phone_tick = 0;
    uint8_t ticks = 0;
    uint8_t update_counter = 0;
    uint8_t duration_mod4 = 0;
    uint8_t rate_accumulator = 0;

    uint16_t pitch = 0;
    uint16_t pitch_limit = 255;
    bool pitch_noise_gate = false;
    uint16_t noise = 0;
    bool noise_bit = false;
    bool closure_active = true;
    uint8_t closure_age = 0;

    uint16_t target_inflection = 0;
    uint16_t active_inflection = 0;

    uint8_t cur_fa = 0;
    uint8_t cur_fc = 0;
    uint8_t cur_va = 0;
    uint8_t cur_f1 = 0;
    uint8_t cur_f2 = 0;
    uint8_t cur_f2q = 0;
    uint8_t cur_f3 = 0;

    uint8_t filt_fa = 0;
    uint8_t filt_fc = 0;
    uint8_t filt_va = 0;
    uint8_t filt_f1 = 0;
    uint8_t filt_f2 = 0;
    uint8_t filt_f2q = 0;
    uint8_t filt_f3 = 0;

    bool filter_dirty = true;
    bool phone_done = false;
    bool hold_parameters = false;

    uint8_t noiseStopBurstGain() const {
        if (ticks <= phone.voice_delay) {
            return 0;
        }
        switch (ticks - phone.voice_delay) {
            case 1:
            case 2: return 7;
            case 3: return 5;
            case 4: return 3;
            case 5: return 2;
            default: return 0;
        }
    }

    uint8_t voicedStopAttackGain() const {
        if (ticks < phone.closure_delay) {
            return 0;
        }
        switch (ticks - phone.closure_delay) {
            case 0:
            case 1: return 7;
            case 2: return 6;
            case 3: return 4;
            case 4: return 2;
            case 5: return 1;
            default: return 0;
        }
    }

    uint8_t closureGain() const {
        const bool voiced_stop = phone.closure && phone.fa == 0 && phone.va != 0;
        if (voiced_stop) {
            return voicedStopAttackGain();
        }
        if (filt_fa != 0 && filt_va == 0) {
            return phone.closure ? noiseStopBurstGain() : 7;
        }
        return static_cast<uint8_t>(7U ^ (closure_age >> 2));
    }

    void reset() {
        *this = FormantCore{};
        phone = decodePhone(0x3F);
    }

    static uint8_t inflectionSlopeStep(uint8_t slope) {
        constexpr std::array<uint8_t, 8> steps = {1, 2, 3, 4, 6, 8, 12, 16};
        return steps[slope & 0x07];
    }

    void start(uint8_t ssi_phone, uint8_t current_function,
               const std::array<uint8_t, 5> &registers) {
        const uint8_t requested_phone = ssi_phone & 0x3F;
        hold_parameters = isHoldPhone(requested_phone);
        if (!hold_parameters) {
            sc01_phone = kSsi263ToSc01a[requested_phone];
            phone = decodePhone(sc01_phone);
            // SC-01A's S, TH, and T rows all use FC=0 and therefore collapse
            // onto the same high-only noise path. SSI-263 reference speech
            // keeps S sibilant but gives TH/T progressively lower spectra.
            // Retain the decoded formants, durations, and envelopes while
            // applying the SSI-compatible four-bit fricative split. T uses
            // the SC-01A DT row above because it provides the corresponding
            // low-frequency stop target and single release envelope.
            if (requested_phone == 0x30) {
                phone.fc = 8;
                phone.fa = 6;
            } else if (requested_phone == 0x36) {
                phone.fc = 15;
                phone.fa = 1;
            }
        } else {
            // SSI hold allophones preserve the instantaneous vocal-tract
            // state at the boundary, not merely the preceding ROM target.
            // This distinction matters when articulation is still moving --
            // M followed quickly by HN must sustain the nasal sound reached
            // so far rather than continue converging on a fresh M target.
            // HVC/HFC retain that same state while engaging closure.
            phone.pause = false;
            phone.closure = requested_phone == 0x2B ||
                            requested_phone == 0x2D;
            phone.closure_delay = 0;
        }
        phone_tick = 0;
        ticks = 0;
        duration_mod4 = 0;
        rate_accumulator = 0;
        phone_done = false;

        const uint16_t next_inflection = inflectionWord(registers);
        target_inflection = next_inflection;
        if (current_function != 3) {
            active_inflection = next_inflection;
        } else {
            active_inflection = static_cast<uint16_t>(
                (next_inflection & 0x083F) | (active_inflection & 0x07C0));
        }
        pitch_limit = pitchPeriod(active_inflection);
        if (phone.closure_delay == 0) {
            closure_active = phone.closure;
        }
    }

    bool durationOneSkipMode(uint8_t current_function,
                             const std::array<uint8_t, 5> &registers) const {
        return current_function != 1 && (registers[0] >> 6) == 1;
    }

    uint8_t durationSpeedStep(uint8_t current_function,
                              const std::array<uint8_t, 5> &registers) const {
        const uint8_t duration = current_function == 1
            ? 3 : static_cast<uint8_t>(registers[0] >> 6);
        uint8_t result = duration == 2 ? 2 : (duration == 3 ? 4 : 1);
        if (durationOneSkipMode(current_function, registers) &&
            duration_mod4 == 2) {
            result = 2;
        }
        return result;
    }

    uint8_t rateScaledStep(uint8_t base,
                           const std::array<uint8_t, 5> &registers) {
        const uint8_t rate = registers[2] >> 4;
        const uint8_t period = rate == 0 ? 16 : 16 - rate;
        const uint16_t numerator = rate_accumulator + base * 6U;
        rate_accumulator = static_cast<uint8_t>(numerator % period);
        return static_cast<uint8_t>(numerator / period);
    }

    void advanceInflection(uint8_t current_function,
                           const std::array<uint8_t, 5> &registers) {
        const uint16_t live = inflectionWord(registers);
        target_inflection = live;
        uint16_t next = live;
        if (current_function == 3) {
            const uint8_t active_target = (active_inflection >> 6) & 0x1F;
            const uint8_t target = (live >> 6) & 0x1F;
            const uint8_t step = inflectionSlopeStep((live >> 3) & 0x07);
            uint8_t moved = active_target;
            if (active_target < target) {
                moved = static_cast<uint8_t>(active_target +
                    std::min<uint8_t>(target - active_target, step));
            } else if (active_target > target) {
                moved = static_cast<uint8_t>(active_target -
                    std::min<uint8_t>(active_target - target, step));
            }
            next = static_cast<uint16_t>((live & 0x083F) | (moved << 6));
        }
        active_inflection = next;
    }

    void commitFilters() {
        filt_fa = cur_fa >> 4;
        filt_fc = cur_fc >> 4;
        filt_va = cur_va >> 4;
        filt_f1 = cur_f1 >> 4;
        filt_f2 = cur_f2 >> 3;
        filt_f2q = cur_f2q >> 4;
        filt_f3 = cur_f3 >> 4;
        filter_dirty = true;
    }

    void advanceControl(uint8_t speed, uint8_t current_function,
                        const std::array<uint8_t, 5> &registers) {
        if (ticks != 0x10) {
            const uint16_t tick_limit = (phone.duration << 2) | 1U;
            const uint16_t next = phone_tick + speed;
            if (next >= tick_limit) {
                phone_tick = static_cast<uint16_t>(next - tick_limit);
                ++ticks;
                if (ticks == 0x10) {
                    phone_done = true;
                }
                if (ticks == phone.closure_delay) {
                    closure_active = phone.closure;
                }
            } else {
                phone_tick = next;
            }
        }

        update_counter = update_counter == 47 ? 0 : update_counter + 1;
        const bool tick_625 = (update_counter & 0x0F) == 0;
        const bool tick_208 = update_counter == 0x28;
        const uint8_t articulation = (registers[3] >> 4) & 0x07;

        if (!hold_parameters && tick_208 &&
            (!phone.pause || !(filt_fa || filt_va))) {
            cur_fc = interpolate8(cur_fc, phone.fc, articulation);
            cur_f1 = interpolate8(cur_f1, phone.f1, articulation);
            cur_f2 = interpolate8(cur_f2, phone.f2, articulation);
            cur_f2q = interpolate8(cur_f2q, phone.f2q, articulation);
            cur_f3 = interpolate8(cur_f3, phone.f3, articulation);
        }
        if (!hold_parameters && tick_625) {
            if ((ticks & 0x0F) >= phone.voice_delay) {
                cur_fa = interpolate8(cur_fa, phone.fa, articulation);
            }
            if ((ticks & 0x0F) >= phone.closure_delay) {
                cur_va = interpolate8(cur_va, phone.va, articulation);
            }
        }

        if (!closure_active && (filt_fa || filt_va)) {
            closure_age = 0;
        } else if (closure_age != 28) {
            ++closure_age;
        }

        duration_mod4 = durationOneSkipMode(current_function, registers)
            ? static_cast<uint8_t>((duration_mod4 + 1) & 0x03) : 0;
    }

    void advancePitchNoise() {
        const uint16_t next = pitch + 1;
        pitch = next >= pitch_limit
            ? static_cast<uint16_t>(next - pitch_limit) : next;
        pitch_noise_gate = pitch >= (pitch_limit >> 1);

        if ((pitch & 0x3F9) == 0x008) {
            commitFilters();
        }

        const bool input = noise_bit && noise != 0x7FFF;
        noise = static_cast<uint16_t>(((noise << 1) & 0x7FFE) |
                                      (input ? 1 : 0));
        noise_bit = (((noise >> 14) ^ (noise >> 13)) & 1U) == 0;
    }

    void advanceSample(uint8_t current_function,
                       const std::array<uint8_t, 5> &registers) {
        phone_done = false;
        control_accumulator += static_cast<uint32_t>(kControlClockHz);
        if (control_accumulator < SSI263::kSampleRate) {
            return;
        }
        control_accumulator -= SSI263::kSampleRate;

        advanceInflection(current_function, registers);
        pitch_limit = pitchPeriod(active_inflection);
        const uint8_t speed = rateScaledStep(
            durationSpeedStep(current_function, registers), registers);
        if (speed != 0) {
            advanceControl(speed, current_function, registers);
        }
        advancePitchNoise();
    }
};

class FormantSynthesizer {
public:
    void reset() {
        f1_.reset();
        f2_voice_.reset();
        f2_noise_.reset();
        f3_.reset();
        f4_.reset();
        noise_shaper_.reset();
        output_filter_.reset();
        cached_f1_ = cached_f2_ = cached_f2q_ = cached_f3_ = -1;
        cached_filter_frequency_ = -1;
        syncFilters(0, 0, 0, 0, 0xE7, true);
    }

    void sync(const FormantCore &core, uint8_t filter_frequency,
              bool force = false) {
        syncFilters(core.filt_f1, core.filt_f2, core.filt_f2q,
                    core.filt_f3, filter_frequency, force);
    }

    double render(const FormantCore &core, bool excitation) {
        static constexpr std::array<double, 9> glottal = {
            0.0, -4.0 / 7.0, 1.0, 6.0 / 7.0, 5.0 / 7.0,
            4.0 / 7.0, 3.0 / 7.0, 2.0 / 7.0, 1.0 / 7.0,
        };

        double voice = 0.0;
        double noise = 0.0;
        if (excitation) {
            voice = core.pitch >= 72 ? 0.0 : glottal[core.pitch >> 3];
            voice *= static_cast<double>(core.filt_va) / 15.0;
            // SSI-263 selects the pseudo-random source, rather than the
            // glottal/pitch source, for an unvoiced phoneme.  Do not carry
            // SC-01's pitch-phase noise gate into this path: it chops a
            // sustained fricative such as S into a rough periodic buzz and
            // breaks the single release of P/K into several audible puffs.
            noise = 10000.0 * (core.noise_bit ? 1.0 : -1.0);
            noise *= static_cast<double>(core.filt_fa) / 15.0;
        }

        voice = f1_.process(voice);
        voice = f2_voice_.process(voice);

        noise = noise_shaper_.process(noise);
        double f2_noise = noise * static_cast<double>(core.filt_fc) / 15.0;
        f2_noise = f2_noise_.process(f2_noise);

        double mixed = f3_.process(voice + f2_noise);
        mixed += noise * static_cast<double>(5 + (15 ^ core.filt_fc)) / 20.0;
        mixed = f4_.process(mixed);
        mixed *= static_cast<double>(core.closureGain()) / 7.0;
        mixed = output_filter_.process(mixed);
        return std::isfinite(mixed) ? mixed * 0.35 : 0.0;
    }

private:
    void syncFilters(uint8_t f1, uint8_t f2, uint8_t f2q, uint8_t f3,
                     uint8_t filter_frequency, bool force) {
        const bool clock_changed =
            cached_filter_frequency_ != filter_frequency;
        cached_filter_frequency_ = filter_frequency;
        const double filter_clock_hz = filterClockHz(filter_frequency);

        if (force || clock_changed || cached_f1_ != f1) {
            cached_f1_ = f1;
            buildStandardFilter(f1_, filter_clock_hz,
                11247, 11797, 949, 52067,
                2280 + bitsToCaps(f1, {2546, 4973, 9861, 19724}), 166272);
        }
        if (force || clock_changed || cached_f2_ != f2 ||
            cached_f2q_ != f2q) {
            cached_f2_ = f2;
            cached_f2q_ = f2q;
            const double c2t = 829 +
                bitsToCaps(f2q, {1390, 2965, 5875, 11297});
            const double c3 = 2352 +
                bitsToCaps(f2, {833, 1663, 3164, 6327, 12654});
            buildStandardFilter(f2_voice_, filter_clock_hz,
                                24840, 29154, c2t, 38180, c3, 34270);
            // Appletini models the noise half as a second independently
            // stateful F2 resonator with the same pole/zero response. The
            // alternate SC-01 injection transfer function carries a large
            // residual across coefficient changes; after a fricative/pause
            // boundary it overloads voiced M even though FA and FC are zero.
            buildStandardFilter(f2_noise_, filter_clock_hz,
                                24840, 29154, c2t, 38180, c3, 34270);
        }
        if (force || clock_changed || cached_f3_ != f3) {
            cached_f3_ = f3;
            buildStandardFilter(f3_, filter_clock_hz,
                0, 17594, 868, 18828,
                8480 + bitsToCaps(f3, {2226, 4485, 9056, 18111}), 50019);
        }
        if (force || clock_changed) {
            buildStandardFilter(f4_, filter_clock_hz,
                                0, 28810, 1165, 21457, 8558, 7289);
            buildLowpassFilter(output_filter_, filter_clock_hz, 1122, 23131);
            buildNoiseShaperFilter(noise_shaper_, filter_clock_hz,
                                   15500, 14854, 8450, 9523, 14083);
        }
    }

    DigitalFilter f1_;
    DigitalFilter f2_voice_;
    DigitalFilter f2_noise_;
    DigitalFilter f3_;
    DigitalFilter f4_;
    DigitalFilter noise_shaper_;
    DigitalFilter output_filter_;
    int cached_f1_ = -1;
    int cached_f2_ = -1;
    int cached_f2q_ = -1;
    int cached_f3_ = -1;
    int cached_filter_frequency_ = -1;
};

} // namespace

class SSI263::Impl {
public:
    void reset() {
        registers = {0xC0, 0x00, 0x00, 0x80, 0xFF};
        ready = false;
        active = false;
        completion_pending = false;
        interrupts_enabled = false;
        current_function = 0;
        phoneme = 0;
        samples_remaining = 0;
        samples_total = 0;
        samples_elapsed = 0;
        tail_remaining = 0;
        output_level = 0.0;
        core.reset();
        synth.reset();
    }

    void latchModeAndInterrupts() {
        const uint8_t function = registers[0] >> 6;
        if (function != 0) {
            current_function = function;
            interrupts_enabled = true;
        } else {
            interrupts_enabled = false;
        }
    }

    void startPhoneme(uint8_t phone) {
        phoneme = phone & 0x3F;
        core.start(phoneme, current_function, registers);
        synth.sync(core, registers[4]);
        active = true;
        ready = false;
        completion_pending = false;
        tail_remaining = 0;
        samples_elapsed = 0;
        samples_total = estimateSamples();
        samples_remaining = samples_total;
    }

    uint32_t estimateSamples() const {
        const double tick_limit = static_cast<double>((core.phone.duration << 2) | 1U);
        double duration_step = 1.0;
        const uint8_t duration = current_function == 1
            ? 3 : static_cast<uint8_t>(registers[0] >> 6);
        if (duration == 1 && current_function != 1) {
            duration_step = 1.25;
        } else if (duration == 2) {
            duration_step = 2.0;
        } else if (duration == 3) {
            duration_step = 4.0;
        }
        const uint8_t rate = registers[2] >> 4;
        const double rate_step = 6.0 / static_cast<double>(rate == 0 ? 16 : 16 - rate);
        const double control_ticks = 16.0 * tick_limit /
                                     (duration_step * rate_step);
        return std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(
            control_ticks * SSI263::kSampleRate / kControlClockHz)));
    }

    float generateSample() {
        // FILFREQ is a live attribute. Captured software changes it between
        // short frames, including the tract transition into the final vowel
        // of "MY", without resetting the chip or its analog history.
        synth.sync(core, registers[4]);
        const bool excite = active;
        double sample = synth.render(core, excite);

        if (active || tail_remaining != 0) {
            core.advanceSample(current_function, registers);
            if (core.filter_dirty) {
                synth.sync(core, registers[4]);
                core.filter_dirty = false;
            }
        }

        if (active) {
            ++samples_elapsed;
            if (samples_remaining != 0) {
                --samples_remaining;
            }
            if (core.phone_done) {
                active = false;
                // CTTRAMP D7 powers the SSI output down and suppresses A/!R;
                // it does not freeze an already running formant backend. This
                // matters to captured Mockingboard composite data, which uses
                // short power-down spans between live register frames.
                if ((registers[3] & 0x80) == 0) {
                    ready = true;
                    completion_pending = true;
                }
                samples_remaining = 0;
                tail_remaining = kFilterTailSamples;
            }
        } else if (tail_remaining != 0) {
            --tail_remaining;
        }

        const double amplitude = static_cast<double>(registers[3] & 0x0F) / 15.0;
        if (registers[4] == 0xFF || (registers[3] & 0x80) != 0 ||
            amplitude == 0.0) {
            output_level = 0.0;
            return 0.0f;
        }

        double target = sample * amplitude;
        if (target > kOutputSoftKnee) {
            target = kOutputSoftKnee +
                     (target - kOutputSoftKnee) * kOutputSoftRatio;
        } else if (target < -kOutputSoftKnee) {
            target = -kOutputSoftKnee +
                     (target + kOutputSoftKnee) * kOutputSoftRatio;
        }
        // The physical SSI output amplifier approaches its rails instead of
        // wrapping into a full-scale square transient. The Appletini fixed
        // pipeline uses a soft knee plus saturation; tanh supplies the same
        // bounded analog behavior without a hard edge at the host float rail.
        target = kOutputAnalogRail *
                 std::tanh(target / kOutputAnalogRail);
        output_level += std::clamp(target - output_level,
                                   -kOutputSlewStep, kOutputSlewStep);
        return static_cast<float>(output_level);
    }

    std::array<uint8_t, 5> registers{};
    FormantCore core;
    FormantSynthesizer synth;
    bool ready = false;
    bool active = false;
    bool completion_pending = false;
    bool interrupts_enabled = false;
    uint8_t current_function = 0;
    uint8_t phoneme = 0;
    uint32_t samples_remaining = 0;
    uint32_t samples_total = 0;
    uint32_t samples_elapsed = 0;
    uint32_t tail_remaining = 0;
    double output_level = 0.0;
};

SSI263::SSI263() : impl_(std::make_unique<Impl>()) {
    reset();
}

SSI263::~SSI263() = default;
SSI263::SSI263(SSI263 &&) noexcept = default;
SSI263 &SSI263::operator=(SSI263 &&) noexcept = default;

void SSI263::reset() {
    impl_->reset();
}

void SSI263::write(uint8_t reg, uint8_t value) {
    if (reg > 7) {
        return;
    }
    if (reg >= 4) {
        impl_->registers[4] = value;
        return;
    }

    const uint8_t old_control = impl_->registers[3];
    const bool was_ready = impl_->ready;
    impl_->registers[reg] = value;

    // Writes to DURPHON, INFLECT or RATEINF acknowledge A/!R.
    if (reg <= 2) {
        impl_->ready = false;
        impl_->completion_pending = false;
    }

    switch (reg) {
        case 0:
            impl_->phoneme = value & 0x3F;
            if ((impl_->registers[3] & 0x80) == 0) {
                impl_->startPhoneme(impl_->phoneme);
            }
            break;

        case 1:
        case 2:
            // Clearing A/!R after completion repeats the latched phone.
            if (was_ready && !impl_->active && (old_control & 0x80) == 0) {
                impl_->startPhoneme(impl_->phoneme);
            }
            break;

        case 3:
            if (value & 0x80) {
                // Match the hardware/Appletini wrapper: power-down clears the
                // visible request and mutes output, while the current phoneme
                // timing continues internally. A subsequent falling edge
                // starts the currently latched DURPHON value afresh.
                impl_->ready = false;
                impl_->completion_pending = false;
            } else if (old_control & 0x80) {
                impl_->latchModeAndInterrupts();
                impl_->startPhoneme(impl_->registers[0] & 0x3F);
            }
            break;

        default:
            break;
    }
}

uint8_t SSI263::read(uint8_t /*reg*/) const {
    return impl_->ready ? 0x80 : 0x00;
}

bool SSI263::ready() const { return impl_->ready; }
bool SSI263::active() const { return impl_->active; }
bool SSI263::interruptsEnabled() const { return impl_->interrupts_enabled; }
uint8_t SSI263::phoneme() const { return impl_->phoneme; }
uint32_t SSI263::samplesRemaining() const { return impl_->samples_remaining; }
uint32_t SSI263::samplesTotal() const { return impl_->samples_total; }

float SSI263::pitchHz() const {
    const uint16_t inflection = inflectionWord(impl_->registers);
    return 125000.0f / static_cast<float>(4096U - inflection);
}

bool SSI263::takeCompletion() {
    const bool pending = impl_->completion_pending;
    impl_->completion_pending = false;
    return pending;
}

void SSI263::mixSamples(std::vector<float> &stereo, uint32_t sample_count) {
    const size_t required = static_cast<size_t>(sample_count) * 2;
    if (stereo.size() < required) {
        stereo.resize(required, 0.0f);
    }

    for (uint32_t i = 0; i < sample_count; ++i) {
        const float speech = impl_->generateSample();
        const size_t index = static_cast<size_t>(i) * 2;
        stereo[index] = std::clamp(stereo[index] + speech, -1.0f, 1.0f);
        stereo[index + 1] = std::clamp(stereo[index + 1] + speech,
                                       -1.0f, 1.0f);
    }
}
