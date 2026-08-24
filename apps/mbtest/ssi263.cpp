#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

#include "devices/mockingboard/SSI263.hpp"
#include "devices/mockingboard/W6522.hpp"

uint64_t debug_level = 0;

namespace {

bool hasAudibleSamples(const std::vector<float> &samples) {
    return std::any_of(samples.begin(), samples.end(), [](float sample) {
        return std::fabs(sample) > 0.00001f;
    });
}

bool allSamplesInRange(const std::vector<float> &samples) {
    return std::all_of(samples.begin(), samples.end(), [](float sample) {
        return std::isfinite(sample) && sample >= -1.0f && sample <= 1.0f;
    });
}

bool allSamplesSilent(const std::vector<float> &samples) {
    return std::all_of(samples.begin(), samples.end(), [](float sample) {
        return sample == 0.0f;
    });
}

float sampleEnergy(const std::vector<float> &samples) {
    float energy = 0.0f;
    for (float sample : samples) {
        energy += std::fabs(sample);
    }
    return energy;
}

float peakSample(const std::vector<float> &samples) {
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::fabs(sample));
    }
    return peak;
}

size_t clippedSamples(const std::vector<float> &samples) {
    return static_cast<size_t>(std::count_if(samples.begin(), samples.end(),
        [](float sample) { return std::fabs(sample) >= 0.9999f; }));
}

float highFrequencyRatio(const std::vector<float> &samples) {
    double signal = 0.0;
    double difference = 0.0;
    float previous = 0.0f;
    for (size_t i = 0; i < samples.size(); i += 2) {
        const float sample = samples[i];
        signal += static_cast<double>(sample) * sample;
        const double delta = static_cast<double>(sample) - previous;
        difference += delta * delta;
        previous = sample;
    }
    return signal > 0.0 ? static_cast<float>(difference / signal) : 0.0f;
}

float waveformDifference(const std::vector<float> &a,
                         const std::vector<float> &b) {
    const size_t count = std::min(a.size(), b.size());
    double difference = 0.0;
    for (size_t i = 0; i < count; ++i) {
        difference += std::fabs(static_cast<double>(a[i]) - b[i]);
    }
    return static_cast<float>(difference);
}

void configureSpeech(SSI263 &speech, uint8_t duration_phone, uint8_t inflection,
                     uint8_t rate_inflection, uint8_t control,
                     uint8_t filter = 0xE8) {
    speech.write(3, 0x80);
    speech.write(0, duration_phone);
    speech.write(1, inflection);
    speech.write(2, rate_inflection);
    speech.write(3, control);
    speech.write(4, filter);
}

// MESS capture records are stored in programming order: FILFREQ, INFLECT,
// RATEINF, CTTRAMP, then DURPHON. DURPHON is last because it starts the frame
// when the SSI-263 is already out of power-down.
void writeCapturedFrame(SSI263 &speech, const uint8_t (&frame)[5]) {
    speech.write(4, frame[0]);
    speech.write(1, frame[1]);
    speech.write(2, frame[2]);
    speech.write(3, frame[3]);
    speech.write(0, frame[4]);
}

bool finishPhoneme(SSI263 &speech, N6522 *completion_via = nullptr) {
    std::vector<float> audio(256 * 2, 0.0f);
    uint32_t generated = 0;
    while (!speech.ready() && generated < SSI263::kSampleRate * 2) {
        std::fill(audio.begin(), audio.end(), 0.0f);
        speech.mixSamples(audio, 256);
        generated += 256;
        if (speech.takeCompletion() && speech.interruptsEnabled() && completion_via) {
            completion_via->signal_ca1_falling_edge();
        }
    }
    return speech.ready();
}

struct PhoneAudioMetrics {
    double energy = 0.0;
    float peak = 0.0f;
    uint32_t samples = 0;
    uint32_t railed_samples = 0;
};

PhoneAudioMetrics renderPhone(SSI263 &speech) {
    PhoneAudioMetrics result;
    std::vector<float> audio(256 * 2, 0.0f);
    while (!speech.ready() && result.samples < SSI263::kSampleRate * 2) {
        std::fill(audio.begin(), audio.end(), 0.0f);
        speech.mixSamples(audio, 256);
        for (size_t i = 0; i < audio.size(); i += 2) {
            result.energy += std::fabs(static_cast<double>(audio[i]));
            result.peak = std::max(result.peak, std::fabs(audio[i]));
            if (std::fabs(audio[i]) > 0.94f) {
                ++result.railed_samples;
            }
        }
        result.samples += 256;
    }
    return result;
}

} // namespace

int main() {
    SSI263 speech;

    if (speech.active() || speech.ready() || speech.read(0) != 0x00) {
        std::fprintf(stderr, "SSI-263 reset state is incorrect\n");
        return 1;
    }

    // Use the same staged-start sequence as the Appletini showcase software.
    configureSpeech(speech, 0xC1, 0x40, 0xA8, 0x5A);

    if (!speech.active() || speech.ready() || speech.phoneme() != 0x01 ||
        !speech.interruptsEnabled()) {
        std::fprintf(stderr, "SSI-263 did not start the staged phoneme\n");
        return 1;
    }

    std::vector<float> audio(2048 * 2, 0.0f);
    speech.mixSamples(audio, 2048);
    if (!hasAudibleSamples(audio) || !allSamplesInRange(audio)) {
        std::fprintf(stderr, "SSI-263 did not produce valid audible samples\n");
        return 1;
    }

    unsigned generated = 2048;
    while (!speech.ready() && generated < SSI263::kSampleRate) {
        std::fill(audio.begin(), audio.end(), 0.0f);
        speech.mixSamples(audio, 2048);
        generated += 2048;
    }
    if (!speech.ready() || speech.active() || speech.read(0) != 0x80) {
        std::fprintf(stderr, "SSI-263 did not assert D7 after completion\n");
        return 1;
    }

    // Writing a new duration/phoneme clears ready and starts immediately when
    // the control bit is low.
    speech.write(0, 0xF0); // S
    if (!speech.active() || speech.ready() || speech.read(0) != 0x00 ||
        speech.phoneme() != 0x30) {
        std::fprintf(stderr, "SSI-263 immediate start/ready clear failed\n");
        return 1;
    }

    std::fill(audio.begin(), audio.end(), 0.0f);
    speech.mixSamples(audio, 2048);
    if (!hasAudibleSamples(audio) || !allSamplesInRange(audio)) {
        std::fprintf(stderr, "SSI-263 fricative synthesis failed\n");
        return 1;
    }

    // Captured Mockingboard composite speech uses CTTRAMP D7 transitions in
    // the middle of long streams (for example $47 -> $87 -> $47). Power-down
    // must mute and suppress completion without aborting the running formant
    // timing state; its falling edge starts the latched phone again.
    SSI263 power_down_speech;
    configureSpeech(power_down_speech, 0x40, 0x7C, 0xF8, 0x47, 0xE6);
    std::vector<float> power_down_lead(64 * 2, 0.0f);
    power_down_speech.mixSamples(power_down_lead, 64);
    if (!power_down_speech.active()) {
        std::fprintf(stderr, "SSI-263 power-down setup did not remain active\n");
        return 1;
    }
    power_down_speech.write(3, 0x87);
    if (!power_down_speech.active()) {
        std::fprintf(stderr, "SSI-263 power-down aborted active timing\n");
        return 1;
    }
    std::vector<float> powered_down(256 * 2, 0.0f);
    power_down_speech.mixSamples(powered_down, 256);
    if (power_down_speech.ready() || !allSamplesSilent(powered_down)) {
        std::fprintf(stderr, "SSI-263 control power-down failed\n");
        return 1;
    }
    std::vector<float> power_down_finish(256 * 2, 0.0f);
    uint32_t power_down_samples = 256;
    while (power_down_speech.active() &&
           power_down_samples < SSI263::kSampleRate * 2) {
        std::fill(power_down_finish.begin(), power_down_finish.end(), 0.0f);
        power_down_speech.mixSamples(power_down_finish, 256);
        power_down_samples += 256;
        if (!allSamplesSilent(power_down_finish)) {
            std::fprintf(stderr, "SSI-263 power-down leaked formant audio\n");
            return 1;
        }
    }
    if (power_down_speech.active() || power_down_speech.ready()) {
        std::fprintf(stderr, "SSI-263 power-down completion was exposed\n");
        return 1;
    }
    power_down_speech.write(3, 0x47);
    if (!power_down_speech.active() || power_down_speech.ready()) {
        std::fprintf(stderr, "SSI-263 power-down release did not restart\n");
        return 1;
    }

    // Mockingboard write decode is A6 plus A2..A0. It must cover the entire
    // $40-$7F region, not just the five canonical register spellings.
    for (unsigned offset = 0; offset <= 0xFF; ++offset) {
        const bool expected = offset >= 0x40 && offset <= 0x7F;
        if (SSI263::isMockingboardWriteOffset(static_cast<uint8_t>(offset)) != expected) {
            std::fprintf(stderr, "SSI-263 Mockingboard decode failed at $%02X\n", offset);
            return 1;
        }
        if (expected && SSI263::registerForOffset(static_cast<uint8_t>(offset)) !=
                            (offset & 0x07)) {
            std::fprintf(stderr, "SSI-263 register alias failed at $%02X\n", offset);
            return 1;
        }
    }

    // RATEINF[7:4] controls playback rate while the low nibble participates in
    // pitch. Keep the low nibble fixed and require a higher RATE to finish
    // sooner without changing the target pitch.
    SSI263 rate_slow;
    SSI263 rate_fast;
    configureSpeech(rate_slow, 0x81, 0x40, 0x08, 0x5A);
    configureSpeech(rate_fast, 0x81, 0x40, 0xF8, 0x5A);
    if (rate_fast.samplesTotal() >= rate_slow.samplesTotal() ||
        std::fabs(rate_fast.pitchHz() - rate_slow.pitchHz()) > 0.001f) {
        std::fprintf(stderr, "SSI-263 RATE high-nibble control is incorrect\n");
        return 1;
    }

    // With the high nibble held constant, RATEINF bit 3 changes the top bit of
    // the 12-bit inflection word. It must change pitch but not duration.
    SSI263 pitch_low;
    SSI263 pitch_high;
    configureSpeech(pitch_low, 0x81, 0x40, 0xA0, 0x5A);
    configureSpeech(pitch_high, 0x81, 0x40, 0xA8, 0x5A);
    if (pitch_low.samplesTotal() != pitch_high.samplesTotal() ||
        pitch_high.pitchHz() <= pitch_low.pitchHz() * 1.5f) {
        std::fprintf(stderr, "SSI-263 12-bit inflection composition is incorrect\n");
        return 1;
    }

    // DURPHON[7:6] changes duration and also controls whether the completion
    // IRQ function is latched on the control falling edge.
    SSI263 duration_normal;
    SSI263 duration_fast;
    configureSpeech(duration_normal, 0x01, 0x40, 0xA8, 0x5A);
    configureSpeech(duration_fast, 0x81, 0x40, 0xA8, 0x5A);
    if (duration_fast.samplesTotal() >= duration_normal.samplesTotal() ||
        duration_normal.interruptsEnabled() || !duration_fast.interruptsEnabled()) {
        std::fprintf(stderr, "SSI-263 duration/function latch is incorrect\n");
        return 1;
    }

    // FILTER=$FF mutes the output without stopping phoneme timing. Registers
    // 4..7 alias the same latch, so writing register 7 must unmute it.
    SSI263 filtered;
    configureSpeech(filtered, 0x41, 0x40, 0xA8, 0x5A, 0xFF);
    std::vector<float> muted(256 * 2, 0.0f);
    filtered.mixSamples(muted, 256);
    if (!filtered.active() || !allSamplesSilent(muted)) {
        std::fprintf(stderr, "SSI-263 filter silence failed\n");
        return 1;
    }
    filtered.write(7, 0xE8);
    // A cold formant core commits its first interpolated filter bank at the
    // next glottal phase window, so allow one low-pitch period after unmuting.
    std::vector<float> unmuted(2048 * 2, 0.0f);
    filtered.mixSamples(unmuted, 2048);
    if (!hasAudibleSamples(unmuted)) {
        std::fprintf(stderr, "SSI-263 filter-register alias failed\n");
        return 1;
    }

    // FILFREQ is not just an on/off latch. It clocks every switched-capacitor
    // vocal-tract filter, so changing it must move the tract response while
    // leaving pitch and phoneme timing alone.
    SSI263 low_filter_speech;
    SSI263 high_filter_speech;
    configureSpeech(low_filter_speech, 0xC5, 0x7F, 0xC8, 0x77, 0x60);
    configureSpeech(high_filter_speech, 0xC5, 0x7F, 0xC8, 0x77, 0xE8);
    std::vector<float> low_filter_audio(4096 * 2, 0.0f);
    std::vector<float> high_filter_audio(4096 * 2, 0.0f);
    low_filter_speech.mixSamples(low_filter_audio, 4096);
    high_filter_speech.mixSamples(high_filter_audio, 4096);
    if (!hasAudibleSamples(low_filter_audio) ||
        !hasAudibleSamples(high_filter_audio) ||
        waveformDifference(low_filter_audio, high_filter_audio) < 0.05f ||
        low_filter_speech.samplesTotal() != high_filter_speech.samplesTotal() ||
        std::fabs(low_filter_speech.pitchHz() -
                  high_filter_speech.pitchHz()) > 0.001f) {
        std::fprintf(stderr,
                     "SSI-263 FILFREQ did not retune the vocal tract\n");
        return 1;
    }

    // CTL[6:4] controls formant articulation. Slow and fast transitions from
    // reset must produce measurably different early-phone envelopes.
    SSI263 articulation_slow;
    SSI263 articulation_fast;
    // Use the normal duration and a high target inflection so the cold core
    // crosses more than one hardware filter-commit window during the probe.
    configureSpeech(articulation_slow, 0x01, 0xC0, 0xA8, 0x0A);
    configureSpeech(articulation_fast, 0x01, 0xC0, 0xA8, 0x7A);
    std::vector<float> slow_audio(4096 * 2, 0.0f);
    std::vector<float> fast_audio(4096 * 2, 0.0f);
    articulation_slow.mixSamples(slow_audio, 4096);
    articulation_fast.mixSamples(fast_audio, 4096);
    const float slow_energy = sampleEnergy(slow_audio);
    const float fast_energy = sampleEnergy(fast_audio);
    if (!hasAudibleSamples(slow_audio) || !hasAudibleSamples(fast_audio) ||
        std::fabs(slow_energy - fast_energy) < 0.001f) {
        std::fprintf(stderr,
                     "SSI-263 articulation control has no effect (slow=%g fast=%g)\n",
                     slow_energy, fast_energy);
        return 1;
    }

    // The synthesizer must retain exact state across host audio chunk
    // boundaries. Render the same phone as one buffer and as irregular chunks.
    SSI263 contiguous_speech;
    SSI263 chunked_speech;
    configureSpeech(contiguous_speech, 0x08, 0xC0, 0xA8, 0x7F);
    configureSpeech(chunked_speech, 0x08, 0xC0, 0xA8, 0x7F);
    std::vector<float> contiguous(8192 * 2, 0.0f);
    std::vector<float> chunked;
    contiguous_speech.mixSamples(contiguous, 8192);
    uint32_t rendered = 0;
    while (rendered < 8192) {
        const uint32_t count = std::min<uint32_t>(257, 8192 - rendered);
        std::vector<float> part(static_cast<size_t>(count) * 2, 0.0f);
        chunked_speech.mixSamples(part, count);
        chunked.insert(chunked.end(), part.begin(), part.end());
        rendered += count;
    }
    if (waveformDifference(contiguous, chunked) != 0.0f) {
        std::fprintf(stderr, "SSI-263 synthesis changes at host chunk boundaries\n");
        return 1;
    }

    // A vowel and a fricative must exercise distinct tract/noise paths. The
    // first-difference energy is a compact high-frequency-content proxy.
    SSI263 vowel_speech;
    SSI263 fricative_speech;
    configureSpeech(vowel_speech, 0x08, 0xC0, 0xA8, 0x7F);
    configureSpeech(fricative_speech, 0x30, 0xC0, 0xA8, 0x7F);
    std::vector<float> vowel_audio(8192 * 2, 0.0f);
    std::vector<float> fricative_audio(8192 * 2, 0.0f);
    vowel_speech.mixSamples(vowel_audio, 8192);
    fricative_speech.mixSamples(fricative_audio, 8192);
    const float vowel_hf = highFrequencyRatio(vowel_audio);
    const float fricative_hf = highFrequencyRatio(fricative_audio);
    if (!hasAudibleSamples(vowel_audio) || !hasAudibleSamples(fricative_audio) ||
        waveformDifference(vowel_audio, fricative_audio) < 0.1f ||
        fricative_hf <= vowel_hf * 1.10f) {
        std::fprintf(stderr,
                     "SSI-263 formant/noise paths are not distinct (vowel_hf=%g fricative_hf=%g)\n",
                     vowel_hf, fricative_hf);
        return 1;
    }

    // SSI-263 uses its pseudo-random source directly for an unvoiced phone;
    // changing pitch must not chop S at the glottal period. Some residual
    // difference is expected because the SC-01-compatible control core still
    // commits interpolated filter values against its pitch counter.
    SSI263 s_low_pitch;
    SSI263 s_high_pitch;
    configureSpeech(s_low_pitch, 0x30, 0x80, 0xA0, 0x7F, 0xE8);
    configureSpeech(s_high_pitch, 0x30, 0x80, 0xA8, 0x7F, 0xE8);
    std::vector<float> s_low_audio(8192 * 2, 0.0f);
    std::vector<float> s_high_audio(8192 * 2, 0.0f);
    s_low_pitch.mixSamples(s_low_audio, 8192);
    s_high_pitch.mixSamples(s_high_audio, 8192);
    const float s_low_energy = sampleEnergy(s_low_audio);
    const float s_high_energy = sampleEnergy(s_high_audio);
    const float s_pitch_difference =
        waveformDifference(s_low_audio, s_high_audio);
    if (s_low_energy <= 0.0f || s_high_energy <= 0.0f ||
        s_pitch_difference > std::max(s_low_energy, s_high_energy) * 0.30f) {
        std::fprintf(stderr,
            "SSI-263 unvoiced S is modulated by pitch "
            "(low=%g high=%g difference=%g)\n",
            s_low_energy, s_high_energy, s_pitch_difference);
        return 1;
    }

    auto render_consonant = [](uint8_t phone) {
        SSI263 probe;
        configureSpeech(probe, phone, 0xC0, 0xA8, 0x7F);
        std::vector<float> audio(8192 * 2, 0.0f);
        probe.mixSamples(audio, 8192);
        return audio;
    };
    const std::vector<float> s_audio = render_consonant(0x30);
    const std::vector<float> th_audio = render_consonant(0x36);
    const std::vector<float> t_audio = render_consonant(0x28);
    const std::vector<float> p_audio = render_consonant(0x27);
    const std::vector<float> k_audio = render_consonant(0x29);
    const float s_hf = highFrequencyRatio(s_audio);
    const float th_hf = highFrequencyRatio(th_audio);
    const float t_hf = highFrequencyRatio(t_audio);
    const float p_hf = highFrequencyRatio(p_audio);
    const float k_hf = highFrequencyRatio(k_audio);
    const float stop_hf = std::max(p_hf, k_hf);
    const float s_energy = sampleEnergy(s_audio);
    const float th_energy = sampleEnergy(th_audio);
    const float t_energy = sampleEnergy(t_audio);
    const float k_energy = sampleEnergy(k_audio);
    const float s_peak = peakSample(s_audio);
    const float th_peak = peakSample(th_audio);

    // The SSI targets must not collapse S, TH, and T onto a single noise
    // path. S is the dominant sibilant: the brightest spectrum and, as a
    // sustained fricative, more total energy than a K burst. TH is a soft,
    // diffuse dental fricative -- brighter than the stop bursts but well
    // below S in both brightness and level. T's alveolar release is
    // brighter than the low labial P pop, and nothing may reach the
    // clipped/railed range.
    if (!hasAudibleSamples(s_audio) || !hasAudibleSamples(th_audio) ||
        !hasAudibleSamples(t_audio) || !hasAudibleSamples(p_audio) ||
        !hasAudibleSamples(k_audio) || !allSamplesInRange(s_audio) ||
        !allSamplesInRange(th_audio) || !allSamplesInRange(t_audio) ||
        s_hf <= th_hf || s_hf <= stop_hf * 2.0f ||
        th_hf <= stop_hf || t_hf <= p_hf ||
        s_energy <= th_energy * 1.5f ||
        s_peak <= th_peak || s_peak >= 0.80f || th_peak >= 0.45f) {
        std::fprintf(stderr,
            "SSI-263 consonant spectra collapsed/distorted "
            "(hf S=%g TH=%g T=%g P=%g K=%g; "
            "level S=%g/%g TH=%g/%g)\n",
            s_hf, th_hf, t_hf, p_hf, k_hf,
            s_energy, s_peak, th_energy, th_peak);
        return 1;
    }

    // Every SSI-263 phoneme/allophone that was absent from Appletini's lossy
    // inverse SC-01 table must have an explicit tract target. None may silently
    // fall through to SC-01 STOP. Stateful hold codes are covered below by the
    // captured M/HN sequence.
    constexpr uint8_t formerly_unmapped_phones[] = {
        0x04, 0x06, 0x12, 0x15, 0x17, 0x1E, 0x1F,
        0x21, 0x22, 0x31, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    };
    for (uint8_t phone : formerly_unmapped_phones) {
        SSI263 allophone_speech;
        configureSpeech(allophone_speech,
                        static_cast<uint8_t>(0xC0 | phone),
                        0x7A, 0xA8, 0x77, 0xE8);
        std::vector<float> allophone_audio(8192 * 2, 0.0f);
        allophone_speech.mixSamples(allophone_audio, 8192);
        if (!hasAudibleSamples(allophone_audio) ||
            !allSamplesInRange(allophone_audio)) {
            std::fprintf(stderr,
                "SSI-263 phoneme $%02X has no valid formant target\n", phone);
            return 1;
        }
    }

    // CTTRAMP amplitude is a live linear control around the same synthesized
    // tract output, and normal speech should not spend material time clipped.
    SSI263 quiet_speech;
    SSI263 loud_speech;
    configureSpeech(quiet_speech, 0x08, 0xC0, 0xA8, 0x71);
    configureSpeech(loud_speech, 0x08, 0xC0, 0xA8, 0x7F);
    std::vector<float> quiet_audio(8192 * 2, 0.0f);
    std::vector<float> loud_audio(8192 * 2, 0.0f);
    quiet_speech.mixSamples(quiet_audio, 8192);
    loud_speech.mixSamples(loud_audio, 8192);
    const float quiet_energy = sampleEnergy(quiet_audio);
    const float loud_energy = sampleEnergy(loud_audio);
    if (quiet_energy <= 0.0f || loud_energy < quiet_energy * 10.0f ||
        peakSample(loud_audio) <= 0.0001f ||
        clippedSamples(loud_audio) > loud_audio.size() / 100) {
        std::fprintf(stderr,
                     "SSI-263 amplitude/clipping failed (quiet=%g loud=%g peak=%g clipped=%zu)\n",
                     quiet_energy, loud_energy, peakSample(loud_audio),
                     clippedSamples(loud_audio));
        return 1;
    }

    // Long speech streams retain oscillator and filter state between phones.
    // Repeated articulation must not collapse into silence or clipped beeps.
    constexpr uint8_t phrase[] = {
        0x08, 0x30, 0x0A, 0x25, 0x16, 0x34, 0x01, 0x37,
    };
    SSI263 phrase_speech;
    configureSpeech(phrase_speech, static_cast<uint8_t>(0xC0 | phrase[0]),
                    0xC0, 0xA8, 0x7F);
    double first_cycle_energy = 0.0;
    double last_cycle_energy = 0.0;
    float phrase_peak = 0.0f;
    for (unsigned repetition = 0; repetition < 12; ++repetition) {
        double cycle_energy = 0.0;
        for (unsigned phone_index = 0; phone_index < std::size(phrase);
             ++phone_index) {
            if (repetition != 0 || phone_index != 0) {
                phrase_speech.write(0, static_cast<uint8_t>(
                    0xC0 | phrase[phone_index]));
            }
            const PhoneAudioMetrics metrics = renderPhone(phrase_speech);
            if (!phrase_speech.ready() || metrics.samples == 0 ||
                !std::isfinite(metrics.energy)) {
                std::fprintf(stderr,
                             "SSI-263 chained phoneme did not complete cleanly\n");
                return 1;
            }
            cycle_energy += metrics.energy;
            phrase_peak = std::max(phrase_peak, metrics.peak);
        }
        if (repetition == 0) {
            first_cycle_energy = cycle_energy;
        }
        if (repetition == 11) {
            last_cycle_energy = cycle_energy;
        }
    }
    if (first_cycle_energy <= 0.0 ||
        last_cycle_energy < first_cycle_energy * 0.35 ||
        phrase_peak >= 0.9999f) {
        std::fprintf(stderr,
                     "SSI-263 long speech degraded (first=%g last=%g peak=%g)\n",
                     first_cycle_energy, last_cycle_energy, phrase_peak);
        return 1;
    }

    // The exact $51A1-$51C8 region that exposed the "MY" fault. In particular,
    // M ($37) is followed by HN ($2E): the SSI-263 instruction to hold the
    // current nasal tract. HN must preserve M rather than map to SC-01 STOP.
    // The final synthetic M+AY pair also covers the ordinary front-vowel
    // transition after the nasal hold.
    constexpr uint8_t my_frames[][5] = {
        {0xE8, 0x6A, 0xA8, 0x43, 0x77}, // M
        {0xE8, 0x6A, 0xA8, 0x43, 0x47},
        {0xE8, 0x7A, 0xA8, 0x4B, 0x2F},
        {0xE8, 0x7A, 0xA8, 0x63, 0x77}, // M
        {0xE8, 0x7A, 0xA8, 0x63, 0xEE}, // HN (hold nasal)
        {0xE8, 0x6A, 0xA8, 0x6B, 0x0E},
        {0xE8, 0x59, 0xA8, 0x73, 0x69},
        {0xE8, 0x48, 0xA8, 0x6B, 0xEC},
        {0xE6, 0x7A, 0xC8, 0x77, 0x37}, // M
        {0xE5, 0x7F, 0xC8, 0x77, 0x05}, // AY (final y/ee)
    };
    SSI263 my_speech;
    configureSpeech(my_speech, 0xC1, 0x7A, 0xC8, 0x77, 0xE6);
    std::vector<float> my_warmup(4096 * 2, 0.0f);
    my_speech.mixSamples(my_warmup, 4096);
    double my_first_energy = 0.0;
    double my_last_energy = 0.0;
    float my_peak = 0.0f;
    for (unsigned repetition = 0; repetition < 64; ++repetition) {
        double repetition_energy = 0.0;
        for (const auto &frame : my_frames) {
            writeCapturedFrame(my_speech, frame);
            const PhoneAudioMetrics metrics = renderPhone(my_speech);
            if (!my_speech.ready() || metrics.samples == 0 ||
                !std::isfinite(metrics.energy)) {
                std::fprintf(stderr,
                    "SSI-263 M/AY transition produced invalid audio\n");
                return 1;
            }
            repetition_energy += metrics.energy;
            my_peak = std::max(my_peak, metrics.peak);
        }
        if (repetition == 0) {
            my_first_energy = repetition_energy;
        }
        if (repetition == 63) {
            my_last_energy = repetition_energy;
        }
    }
    if (my_first_energy <= 0.0 ||
        my_last_energy < my_first_energy * 0.35 ||
        my_peak >= 0.9999f) {
        std::fprintf(stderr,
            "SSI-263 M/AY transition degraded "
            "(first=%g last=%g peak=%g)\n",
            my_first_energy, my_last_energy, my_peak);
        return 1;
    }

    // Exact $5352-$539D frames spelling "SWEET MICRO" in the supplied MESS
    // capture. The word boundary is M ($37) -> HN ($2E) -> AH ($0E). HN must
    // freeze the instantaneous nasal articulation reached by the short M
    // frame; continuing to interpolate toward M's ROM target eventually
    // overdrives the resonators and turns the rest of the phrase into beeps.
    constexpr uint8_t sweet_micro_frames[][5] = {
        {0xE8, 0x6B, 0xA8, 0x53, 0x30}, // S
        {0xE8, 0x7B, 0xA8, 0x5B, 0x63}, // W
        {0xE8, 0x7B, 0xA8, 0x63, 0x01}, // E
        {0xE8, 0x3B, 0xA8, 0x69, 0x28}, // T
        {0xE8, 0x30, 0xA8, 0x69, 0xEC}, // HF
        {0xE8, 0x30, 0xA8, 0x6B, 0xC0}, // PA
        {0xE8, 0x30, 0xA8, 0x6B, 0xC0}, // PA
        {0xE8, 0x5B, 0xA8, 0x5B, 0x77}, // M
        {0xE8, 0x5B, 0xA8, 0x5B, 0xEE}, // HN
        {0xE8, 0x6B, 0xA8, 0x63, 0x4E}, // AH
        {0xE8, 0x7B, 0xA8, 0x6B, 0x84}, // Y1
        {0xE8, 0x7B, 0xA8, 0x63, 0x69}, // K
        {0xE8, 0x7B, 0xA8, 0x63, 0xEC}, // HF
        {0xE8, 0x6B, 0xA8, 0x5B, 0x5D}, // R
        {0xE8, 0x5B, 0xA8, 0x53, 0x11}, // O
        {0xE8, 0x5B, 0xA8, 0x4B, 0x63}, // W
    };
    SSI263 sweet_micro_speech;
    double sweet_micro_first_energy = 0.0;
    double sweet_micro_last_energy = 0.0;
    float sweet_micro_peak = 0.0f;
    uint32_t sweet_micro_railed_samples = 0;
    for (unsigned repetition = 0; repetition < 64; ++repetition) {
        double repetition_energy = 0.0;
        for (const auto &frame : sweet_micro_frames) {
            writeCapturedFrame(sweet_micro_speech, frame);
            const PhoneAudioMetrics metrics = renderPhone(sweet_micro_speech);
            if (!sweet_micro_speech.ready() || metrics.samples == 0 ||
                !std::isfinite(metrics.energy)) {
                std::fprintf(stderr,
                    "SSI-263 SWEET MICRO sequence produced invalid audio\n");
                return 1;
            }
            repetition_energy += metrics.energy;
            sweet_micro_peak = std::max(sweet_micro_peak, metrics.peak);
            sweet_micro_railed_samples += metrics.railed_samples;
        }
        if (repetition == 0) {
            sweet_micro_first_energy = repetition_energy;
        }
        if (repetition == 63) {
            sweet_micro_last_energy = repetition_energy;
        }
    }
    if (sweet_micro_first_energy <= 0.0 ||
        sweet_micro_last_energy < sweet_micro_first_energy * 0.35 ||
        sweet_micro_peak >= 0.9999f || sweet_micro_railed_samples != 0) {
        std::fprintf(stderr,
            "SSI-263 SWEET MICRO sequence degraded "
            "(first=%g last=%g peak=%g railed=%u)\n",
            sweet_micro_first_energy, sweet_micro_last_energy,
            sweet_micro_peak, sweet_micro_railed_samples);
        return 1;
    }

    // Exact five-register frames from the supplied MESS capture around a
    // long CTTRAMP power-down run. Replay them in their stored programming
    // order at a fixed interval and make sure repeated playback neither leaks
    // powered-down audio nor collapses into clipped transients.
    constexpr uint8_t captured_frames[][5] = {
        {0xE6, 0x7B, 0xF8, 0x47, 0x40},
        {0xE6, 0x7B, 0xF8, 0x47, 0x40},
        {0xE6, 0x7B, 0xF8, 0x47, 0x40},
        {0xE6, 0x7B, 0xB8, 0x87, 0xA5},
        {0xE5, 0x7B, 0xF8, 0x87, 0xAB},
        {0xE4, 0x7B, 0xD8, 0x87, 0x47},
        {0xE6, 0x7B, 0xB8, 0x87, 0x47},
        {0xE6, 0x7B, 0xB8, 0x87, 0x65},
        {0xE6, 0x7B, 0xF8, 0x87, 0xC0},
        {0xE6, 0x7B, 0xB8, 0x87, 0x04},
        {0xE4, 0x7B, 0xB8, 0x87, 0x16},
        {0xE5, 0x7A, 0x98, 0x77, 0x0C},
    };
    SSI263 captured_speech;
    float captured_peak = 0.0f;
    for (unsigned repetition = 0; repetition < 32; ++repetition) {
        for (const auto &frame : captured_frames) {
            writeCapturedFrame(captured_speech, frame);
            std::vector<float> frame_audio(735 * 2, 0.0f);
            captured_speech.mixSamples(frame_audio, 735);
            if (!allSamplesInRange(frame_audio) ||
                ((frame[3] & 0x80) != 0 && !allSamplesSilent(frame_audio))) {
                std::fprintf(stderr,
                    "SSI-263 captured composite power-down playback failed\n");
                return 1;
            }
            captured_peak = std::max(captured_peak, peakSample(frame_audio));
        }
    }
    if (captured_peak >= 0.9999f) {
        std::fprintf(stderr,
            "SSI-263 captured composite stream clipped (peak=%g)\n",
            captured_peak);
        return 1;
    }

    // Completion from the primary SSI socket feeds CA1 of the $Cx80 VIA.
    // IFR must latch without IER, IER then gates IRQ, and PCR rising-edge mode
    // must suppress a falling completion edge.
    NClock clock;
    InterruptController irq_controller;
    N6522 via("SSI completion VIA", &clock, &irq_controller, 4, 0);
    via.write(MB_6522_PCR, 0x00);
    via.signal_ca1_falling_edge();
    if ((via.read(MB_6522_IFR) & 0x02) == 0 ||
        irq_controller.get_irq(IRQ_SLOT_0)) {
        std::fprintf(stderr, "6522 CA1 did not latch independently of IER\n");
        return 1;
    }
    via.write(MB_6522_IFR, 0x02);
    via.write(MB_6522_IER, 0x82);

    SSI263 irq_speech;
    configureSpeech(irq_speech, 0xC1, 0x40, 0xA8, 0x5A);
    if (!finishPhoneme(irq_speech, &via) ||
        (via.read(MB_6522_IFR) & 0x82) != 0x82 ||
        !irq_controller.get_irq(IRQ_SLOT_0)) {
        std::fprintf(stderr, "SSI-263 completion did not assert VIA CA1 IRQ\n");
        return 1;
    }

    (void)via.read(MB_6522_ORA_NH);
    via.write(MB_6522_ORA_NH, 0x5A);
    if ((via.read(MB_6522_IFR) & 0x82) != 0x82 ||
        !irq_controller.get_irq(IRQ_SLOT_0)) {
        std::fprintf(stderr, "6522 no-handshake Port A access cleared CA1 IRQ\n");
        return 1;
    }

    (void)via.read(MB_6522_ORA);
    if ((via.read(MB_6522_IFR) & 0x02) != 0 ||
        irq_controller.get_irq(IRQ_SLOT_0)) {
        std::fprintf(stderr, "6522 Port A access did not clear CA1 IRQ\n");
        return 1;
    }
    irq_speech.write(1, 0x41); // ready clear repeats the completed phoneme
    if (!finishPhoneme(irq_speech, &via) ||
        (via.read(MB_6522_IFR) & 0x82) != 0x82) {
        std::fprintf(stderr, "SSI-263 repeated completion did not reassert CA1\n");
        return 1;
    }

    via.write(MB_6522_IFR, 0x02);
    via.write(MB_6522_PCR, 0x01);
    irq_speech.write(2, 0xA8);
    if (!finishPhoneme(irq_speech, &via) ||
        (via.read(MB_6522_IFR) & 0x02) != 0 ||
        irq_controller.get_irq(IRQ_SLOT_0)) {
        std::fprintf(stderr, "6522 PCR did not reject SSI falling-edge IRQ\n");
        return 1;
    }

    std::puts("PASS: SSI-263 decode, controls, audio, D7, and CA1 IRQ checks passed");
    return 0;
}
