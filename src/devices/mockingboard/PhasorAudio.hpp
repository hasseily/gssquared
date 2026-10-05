/*
 *   Copyright (c) 2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace PhasorAudio {

// Appletini's verified SSI-263 fixed-point filter bank and the combined
// Phasor card stream both run on this timebase.  Keeping one definition avoids
// silently resampling the AY and speech halves of the same card differently.
inline constexpr uint32_t kSampleRate = 48000;

// The card synth and the host playback device are independent clocks. Feeding
// SDL exactly one video frame of audio per video frame leaves no useful margin
// for a normal 1024-frame device callback, scheduler jitter, or even small
// crystal-rate error. Keep a modest queue and let SDL's stream resampler absorb
// the clock difference; the SSI and AY generators themselves always remain on
// the exact 48 kHz timebase above.
class OutputClockRecovery {
public:
    static constexpr uint32_t kPrefillMilliseconds = 50;
    static constexpr uint32_t kTargetMilliseconds = 60;
    static constexpr float kMaximumRateAdjustment = 0.005f;
    static constexpr uint32_t kPrefillFrames =
        (kSampleRate * kPrefillMilliseconds) / 1000;
    static constexpr uint32_t kTargetFrames =
        (kSampleRate * kTargetMilliseconds) / 1000;

    void reset() {
        primed_ = false;
        ratio_ = 1.0f;
    }

    bool needsPrefill(uint32_t queued_frames) const {
        return !primed_ || queued_frames == 0;
    }

    void markPrefilled() { primed_ = true; }

    float update(uint32_t queued_frames) {
        if (!primed_ || kTargetFrames == 0) {
            ratio_ = 1.0f;
            return ratio_;
        }

        float error =
            (static_cast<float>(queued_frames) -
             static_cast<float>(kTargetFrames)) /
            static_cast<float>(kTargetFrames);
        if (error > 1.0f) error = 1.0f;
        if (error < -1.0f) error = -1.0f;

        // SDL consumes input faster for ratios above 1.0 and slower below
        // 1.0. A deep queue therefore speeds playback up slightly; a shallow
        // queue slows it slightly. The bound is deliberately inaudible while
        // comfortably covering ordinary audio-clock error.
        ratio_ = 1.0f + error * kMaximumRateAdjustment;
        return ratio_;
    }

    float ratio() const { return ratio_; }
    bool primed() const { return primed_; }

private:
    bool primed_ = false;
    float ratio_ = 1.0f;
};

namespace warmth {

// Fabric clocks from one output read to the next during which the poles
// still integrate the previous input, by the tract passes (0, 1 or 2) of
// the new speech sample: the backend's 9/151/293-clock latency + 9.
inline constexpr uint32_t kHeadClocks[3] = {18, 160, 302};
inline constexpr uint32_t kShortPeriod = 2777;

// (1 - 2^-shift)^clocks in Q1.31: a pole's decay over that many clocks.
constexpr int32_t decayQ31(unsigned shift, uint32_t clocks) {
    double base = 1.0 - 1.0 / static_cast<double>(uint64_t{1} << shift);
    double result = 1.0;
    while (clocks != 0) {
        if (clocks & 1) result *= base;
        base *= base;
        clocks >>= 1;
    }
    return static_cast<int32_t>(result * 2147483648.0 + 0.5);
}

struct PoleDecay {
    int32_t head[3];      // kHeadClocks[passes]
    int32_t tail[3][2];   // the rest of a 2777- or 2778-clock period
};

constexpr PoleDecay poleDecay(unsigned shift) {
    PoleDecay decay{};
    for (unsigned passes = 0; passes < 3; ++passes) {
        decay.head[passes] = decayQ31(shift, kHeadClocks[passes]);
        for (unsigned tail = 0; tail < 2; ++tail) {
            decay.tail[passes][tail] = decayQ31(
                shift, kShortPeriod + tail - kHeadClocks[passes]);
        }
    }
    return decay;
}

} // namespace warmth

// Appletini's Phasor output uses a fixed +8 warmth setting after the completed
// AY+speech card mix (appletini-one mockingboard.sv final_audio_mix).  Its
// three one-poles (Q12 states, x += (target - x) >>> 16, 14 and 13) run on
// every 133.333 MHz fabric clock, 2777 or 2778 of them between two 48 kHz
// ticks.  Each sample period is collapsed here into two closed-form steps per
// pole, so the cost per sample is constant:
//
//  1. The previous input, for the clocks before the new speech sample reaches
//     the mixer.  The SSI-263 backend delivers a sample 9 fabric clocks after
//     the tick with no tract pass, 151 with one and 293 with two
//     (ssi263_formant_backend.sv, FILFREQ); the card output is read at
//     tick - 3, from tone registers five stages behind it, and the poles see
//     the mixer input one clock late, so the poles integrate the previous
//     input for 9 more clocks than that latency from one output read to the
//     next (kHeadClocks).
//  2. The new input, for the rest of the period.
//
// The period alternates as the card's tick does (appletini_yarz_top.sv: a
// 32-bit accumulator gaining 1546188 a clock, the tick on its carry).
//
// The poles truncate (>>> floors), which the closed form models as well: a
// pole moves by floor(d / 2^s) a clock for a distance d to its target, so it
// does not move at all for 0 <= d < 2^s (the dead band: 16 PCM LSB for the
// low pole, 4 and 2 for the others), it decays toward the top of that band
// while rising and onto the target while falling, and on the way the floor
// loses half a step a clock on average.  The closed form decays linearly
// toward d = 2^(s-1) and stops at those two limits; against the per-clock
// stage it measured at most 5 PCM LSB (tools/ssi263-harness README, "The
// warmth fix").
// This is card coloration, not part of the SSI-263 synthesis backend.
class WarmthChannel {
public:
    static constexpr int32_t kWarmthKnee = 20480;

    // The tick accumulator (appletini_yarz_top.sv).
    static constexpr uint32_t kTickStep = 1546188;
    // The accumulator after the tick before the first sample's, chosen so
    // that the first sample's tick clears it to 0 as the card's power-on
    // does: the period of every later sample is then the card's
    // (1203220 + 2777 * 1546188 == 2^32).
    static constexpr uint32_t kResetTickPhase = 1203220;

    void reset() {
        low_q12_ = 0;
        warm_q12_ = 0;
        mid_q12_ = 0;
        previous_input_ = 0;
        tick_phase_ = kResetTickPhase;
    }

    // One 48 kHz sample.  speech_passes: the tract passes (0, 1 or 2) of the
    // SSI-263 sample in this input (SSI263::renderedSamplePasses()), which set
    // when it reaches the mixer within the sample period.
    int16_t processPcm(int16_t input, uint8_t speech_passes) {
        const unsigned passes = speech_passes > 2 ? 2 : speech_passes;
        const unsigned tail = nextPeriod() == kShortPeriod ? 0 : 1;
        // Multiplication is defined for negative PCM values; left-shifting a
        // negative signed integer is undefined in C++.
        const int64_t previous = static_cast<int64_t>(previous_input_) * 4096;
        const int64_t target = static_cast<int64_t>(input) * 4096;
        low_q12_ = advancePole<16>(low_q12_, previous, kLowDecay.head[passes]);
        low_q12_ = advancePole<16>(low_q12_, target, kLowDecay.tail[passes][tail]);
        warm_q12_ = advancePole<14>(warm_q12_, previous, kWarmDecay.head[passes]);
        warm_q12_ = advancePole<14>(warm_q12_, target, kWarmDecay.tail[passes][tail]);
        mid_q12_ = advancePole<13>(mid_q12_, previous, kMidDecay.head[passes]);
        mid_q12_ = advancePole<13>(mid_q12_, target, kMidDecay.tail[passes][tail]);
        previous_input_ = input;

        const int32_t low = static_cast<int32_t>(floorDivPow2(low_q12_, 12));
        const int32_t warm = static_cast<int32_t>(floorDivPow2(warm_q12_, 12));
        const int32_t mid = static_cast<int32_t>(floorDivPow2(mid_q12_, 12));
        const int32_t warm_band = saturatePcm(warm - low);
        const int32_t treble_band = saturatePcm(
            static_cast<int32_t>(input) - mid);

        // With Appletini's forced +8 setting, the adjustable stage reduces to
        // base + warm band - one quarter of the treble band.
        const int32_t shaped = static_cast<int32_t>(input) + warm_band -
            static_cast<int32_t>(floorDivPow2(treble_band, 2));
        return saturatePcm(applyWarmthKnee(shaped));
    }

    float process(float input, uint8_t speech_passes) {
        return static_cast<float>(
            processPcm(quantizePcm(input), speech_passes)) / 32768.0f;
    }

    static int16_t quantizePcm(float input) {
        if (input >= 1.0f) return 32767;
        if (input <= -1.0f) return -32768;
        return saturatePcm(static_cast<int32_t>(
            std::lround(static_cast<double>(input) * 32768.0)));
    }

    static constexpr int32_t applyWarmthKnee(int32_t sample) {
        if (sample > kWarmthKnee) {
            const int32_t excess = sample - kWarmthKnee;
            return kWarmthKnee +
                static_cast<int32_t>(floorDivPow2(excess, 1)) +
                static_cast<int32_t>(floorDivPow2(excess, 3));
        }
        if (sample < -kWarmthKnee) {
            const int32_t excess = -kWarmthKnee - sample;
            return -kWarmthKnee -
                static_cast<int32_t>(floorDivPow2(excess, 1)) -
                static_cast<int32_t>(floorDivPow2(excess, 3));
        }
        return sample;
    }

private:
    static constexpr int64_t floorDivPow2(int64_t value, unsigned shift) {
        if (value >= 0) return value >> shift;
        const int64_t magnitude = -value;
        return -((magnitude + ((int64_t{1} << shift) - 1)) >> shift);
    }

    static constexpr int16_t saturatePcm(int32_t sample) {
        return sample > 32767 ? 32767 :
               (sample < -32768 ? -32768 : static_cast<int16_t>(sample));
    }

    static constexpr uint32_t kShortPeriod = warmth::kShortPeriod;
    using PoleDecay = warmth::PoleDecay;
    static constexpr PoleDecay kLowDecay = warmth::poleDecay(16);
    static constexpr PoleDecay kWarmDecay = warmth::poleDecay(14);
    static constexpr PoleDecay kMidDecay = warmth::poleDecay(13);

    // Fabric clocks to the next tick: 2777 or 2778.
    uint32_t nextPeriod() {
        const uint32_t to_carry = ~tick_phase_;            // 2^32 - 1 - phase
        const uint32_t period = to_carry / kTickStep + 1;  // first n with carry
        tick_phase_ += period * kTickStep;                 // wraps past 2^32
        return period;
    }

    // The truncating one-pole x += (target - x) >>> shift, over the clocks
    // whose decay is decay_q31, in closed form (see the class comment).
    template <unsigned shift>
    static int32_t advancePole(int32_t state, int64_t target,
                               int32_t decay_q31) {
        constexpr int64_t band = int64_t{1} << shift;
        const int64_t distance = target - static_cast<int64_t>(state);
        if (distance >= 0 && distance < band) return state;
        int64_t left = band / 2 +
            floorDivPow2((distance - band / 2) * decay_q31, 31);
        if (distance >= band) {
            if (left < band - 1) left = band - 1;
        } else if (left > 0) {
            left = 0;
        }
        return static_cast<int32_t>(target - left);
    }

    int32_t low_q12_ = 0;
    int32_t warm_q12_ = 0;
    int32_t mid_q12_ = 0;
    int16_t previous_input_ = 0;
    uint32_t tick_phase_ = kResetTickPhase;
};

struct StereoSample {
    float left;
    float right;
};

class WarmthFilter {
public:
    void reset() {
        left_.reset();
        right_.reset();
    }

    // left_passes and right_passes: the tract passes of the speech sample
    // in each channel (PhasorLogic::mixAudioSample routes them).
    StereoSample process(float left, float right, uint8_t left_passes,
                         uint8_t right_passes) {
        return {left_.process(left, left_passes),
                right_.process(right, right_passes)};
    }

private:
    WarmthChannel left_;
    WarmthChannel right_;
};

} // namespace PhasorAudio
