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

#include <cstdint>

// Pure Phasor GAL and AY-bus equations.  Keeping these definitions free of
// emulator state makes the production decoder directly testable.
namespace PhasorLogic {

inline constexpr uint8_t kModeMockingboard = 0;
inline constexpr uint8_t kModePhasor = 5;
inline constexpr uint8_t kModeEchoPlus = 7;

// GSSquared's historical VIA indexing is the inverse of the address half:
// index 0 is $Cn80-$CnFF, while index 1 is $Cn00-$Cn7F.
inline constexpr uint8_t kViaHigh = 0;
inline constexpr uint8_t kViaLow = 1;

inline constexpr uint8_t kStereoLeft = 0;
inline constexpr uint8_t kStereoRight = 1;
inline constexpr uint8_t kAyChipForLowVia = kStereoLeft;
inline constexpr uint8_t kAyChipForHighVia = kStereoRight;

constexpr bool isMockingboard(uint8_t mode) {
    return mode == kModeMockingboard;
}

constexpr bool isPhasorNative(uint8_t mode) {
    return mode == kModePhasor;
}

constexpr bool isEchoPlus(uint8_t mode) {
    return mode == kModeEchoPlus;
}

constexpr bool isExtended(uint8_t mode) {
    return isPhasorNative(mode) || isEchoPlus(mode);
}

constexpr bool ssiVisible(uint8_t mode) {
    return isMockingboard(mode) || isPhasorNative(mode);
}

constexpr uint16_t modeSwitchBase(uint8_t slot) {
    return static_cast<uint16_t>(0xC080u +
                                 static_cast<uint16_t>(slot) * 0x10u);
}

// A3 clears the mode latch before A2..A0 are ORed into it.
constexpr uint8_t updateModeLatch(uint8_t current, uint16_t address) {
    const uint8_t low_nibble = static_cast<uint8_t>(address & 0x0F);
    const uint8_t retained = (low_nibble & 0x08) ? 0 : current;
    return static_cast<uint8_t>(retained | (low_nibble & 0x07));
}

struct ViaHits {
    bool high;
    bool low;
};

constexpr ViaHits decodeViaHits(uint8_t mode, uint8_t offset) {
    if (isMockingboard(mode)) {
        return {(offset & 0x80) != 0, (offset & 0x80) == 0};
    }
    if (isPhasorNative(mode)) {
        return {(offset & 0x80) != 0, (offset & 0x10) != 0};
    }
    if (isEchoPlus(mode)) {
        return {true, false};
    }
    return {false, false};
}

constexpr bool viaHit(ViaHits hits, uint8_t via) {
    return via == kViaHigh ? hits.high : hits.low;
}

constexpr bool nativeTimerReadNeedsExtraTick(uint8_t mode, uint8_t offset) {
    const uint8_t reg = offset & 0x0F;
    return isPhasorNative(mode) && (reg == 0x04 || reg == 0x08);
}

// A native-mode T1C-L or T2C-L read advances the selected VIA timer by one
// additional 1 MHz tick. Appletini serves the value at serve_en and the extra
// tick lands with the read strobe at data_en, after it (mockingboard.sv's
// read path, via6522.v timer_read_extra_tick), so the read returns the
// counter from before the extra tick. Keeping the operation in this shared
// adapter makes both the production call order and the decode directly
// testable without coupling the decoder tests to the full emulator.
template <typename Via>
uint8_t readVia(uint8_t mode, uint8_t offset, Via &via) {
    const uint8_t value = via.read(offset & 0x0F);
    if (nativeTimerReadNeedsExtraTick(mode, offset)) {
        via.incr_cycle();
    }
    return value;
}

struct SsiSelects {
    bool primary;
    bool secondary;
};

constexpr SsiSelects decodeSsiWrites(uint8_t mode, uint8_t offset) {
    if (!ssiVisible(mode)) return {false, false};
    return {(offset & 0x40) != 0, (offset & 0x20) != 0};
}

enum class SsiSocket : uint8_t {
    None,
    Primary,
    Secondary,
};

// Native D7 status is visible only outside both interleaved VIA selects.
// If A6 and A5 are both set, the secondary socket owns D7.
constexpr SsiSocket nativeStatusSocket(uint8_t mode, uint8_t offset) {
    if (!isPhasorNative(mode) || (offset & 0x80) || (offset & 0x10)) {
        return SsiSocket::None;
    }
    if (offset & 0x20) return SsiSocket::Secondary;
    if (offset & 0x40) return SsiSocket::Primary;
    return SsiSocket::None;
}

constexpr uint8_t nativeStatusValue(uint8_t floating_bus, bool ready) {
    return static_cast<uint8_t>((floating_bus & 0x7F) |
                                (ready ? 0x80 : 0x00));
}

constexpr bool advanceAudioSamplePhase(uint64_t &phase,
                                       uint64_t sample_rate,
                                       uint64_t cycle_rate) {
    phase += sample_rate;
    if (phase < cycle_rate) return false;
    phase -= cycle_rate;
    return true;
}

constexpr uint8_t ayChipForVia(uint8_t via) {
    return via == kViaHigh ? kAyChipForHighVia : kAyChipForLowVia;
}

struct AySelection {
    bool primary;
    bool secondary;
};

struct AyRoute {
    bool reset;
    bool drive_primary;
    bool drive_secondary;
    AySelection next_selection;
};

constexpr AyRoute decodeAyRoute(uint8_t mode, uint8_t via, uint8_t pb,
                                AySelection selection) {
    const bool reset = (pb & 0x04) == 0;
    const bool latch = (pb & 0x07) == 0x07;
    const bool transfer = (pb & 0x04) != 0 &&
                          ((((pb >> 1) ^ pb) & 0x01) != 0);
    const bool primary_cs = (pb & 0x10) == 0;
    const bool secondary_cs = (pb & 0x08) == 0;

    if (reset) return {true, true, true, {false, false}};

    bool drive_primary = false;
    bool drive_secondary = false;
    if (isMockingboard(mode)) {
        // A plain Mockingboard has one AY per VIA and no chip-select GAL.
        drive_primary = true;
    } else if (isPhasorNative(mode)) {
        drive_primary = latch ? (primary_cs || secondary_cs)
                              : (transfer && primary_cs && selection.primary);
        drive_secondary = latch ? secondary_cs
                                : (transfer &&
                                   (primary_cs || secondary_cs) &&
                                   selection.secondary);
    } else if (isEchoPlus(mode) && via == kViaHigh) {
        drive_primary = latch ? (primary_cs || secondary_cs) : primary_cs;
        drive_secondary = latch ? secondary_cs
                                : (transfer &&
                                   (secondary_cs ||
                                    (primary_cs && selection.secondary)));
    }

    AySelection next = selection;
    if (isExtended(mode) && latch) {
        if (secondary_cs) {
            // The secondary-select latch command is observed by both AYs.
            next = {true, true};
        } else if (primary_cs) {
            next = {true, false};
        }
    }

    return {false, drive_primary, drive_secondary, next};
}

constexpr uint8_t combineAyRead(bool primary_drove, uint8_t primary_data,
                                bool secondary_drove, uint8_t secondary_data) {
    if (!primary_drove && !secondary_drove) return 0xFF;
    return static_cast<uint8_t>((primary_drove ? primary_data : 0) |
                                (secondary_drove ? secondary_data : 0));
}

struct StereoSample {
    float left;
    float right;
    // The tract passes of the speech sample in each channel
    // (SSI263::renderedSamplePasses), routed with the speech: they set when
    // that sample reaches the card's warmth stage within the sample period
    // (PhasorAudio::WarmthFilter::process).
    uint8_t left_speech_passes;
    uint8_t right_speech_passes;
};

constexpr float limitAudioSample(float sample) {
    return sample > 1.0f ? 1.0f : (sample < -1.0f ? -1.0f : sample);
}

// Combine the two already-stereo AY banks and the two mono SSI sockets.  Keep
// every intermediate in the float mix domain and saturate only the completed
// card output; clipping after each source would make cancellation and the
// result itself depend on source order.
//
// The speech routing is Appletini's (mockingboard.sv final_audio_mix,
// mix_speech): the A5 secondary socket goes to the left channel only and the
// A6 primary socket to the right channel only, each at full level, in every
// card mode. Each socket's tract passes go with its speech.
constexpr StereoSample mixAudioSample(float ay_primary_left,
                                      float ay_primary_right,
                                      float ay_secondary_left,
                                      float ay_secondary_right,
                                      float ssi_secondary,
                                      float ssi_primary,
                                      uint8_t ssi_secondary_passes,
                                      uint8_t ssi_primary_passes) {
    return {
        limitAudioSample(ay_primary_left + ay_secondary_left +
                         ssi_secondary),
        limitAudioSample(ay_primary_right + ay_secondary_right +
                         ssi_primary),
        ssi_secondary_passes,
        ssi_primary_passes,
    };
}

} // namespace PhasorLogic
