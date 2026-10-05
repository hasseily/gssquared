// Tone-stage-only check: the Phasor's tone stage (per-clock C++ model of
// mockingboard.sv's final_audio_mix, tone_model.hpp) and GSSquared's
// PhasorAudio::WarmthChannel (compiled from the worktree) driven with the
// same speech input, compared with each other and with a reference.
//
//   tone_check DIR [--clock card|fixed] [--d D] [--scan D0 D1] [--quiet]
//       DIR holds an RTL render (sec.pcm, pri.pcm, card.pcm from
//       bin/card_rtl): sec drives the left channel, pri the right, as the
//       RTL routes them. Reports, per channel, the model against the RTL's
//       card.pcm (must be 0 differences: the model is the RTL) and
//       WarmthChannel against it (the tone-stage residual GSSquared has
//       with the RTL's own speech as input). --scan finds the voice latency
//       D for which the model is exact.
//   tone_check --in IN.pcm --out OUT.pcm [--clock ...] [--d D]
//       stereo int16 input through the model; writes the model's output
//       and prints WarmthChannel's residual against it (synthetic inputs).
//   --model-out FILE / --gss-out FILE write those outputs (stereo int16).
#include "schedule.hpp"
#include "devices/mockingboard/PhasorAudio.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::vector<int16_t> load(const std::string &p) {
    std::vector<int16_t> v;
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) { std::perror(p.c_str()); std::exit(2); }
    int16_t b[4096];
    size_t n;
    while ((n = std::fread(b, 2, 4096, f)) > 0) v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}
static void save(const std::string &p, const std::vector<int16_t> &v) {
    FILE *f = std::fopen(p.c_str(), "wb");
    if (!f) { std::perror(p.c_str()); std::exit(2); }
    std::fwrite(v.data(), 2, v.size(), f);
    std::fclose(f);
}

// Model output for stereo input (interleaved), one sample per tick.
static std::vector<int16_t> runModel(const std::vector<int16_t> &in, size_t ns, bool card, int d,
                                     uint32_t ac) {
    tone::Schedule s = tone::makeSchedule(ns, card, d);
    std::vector<int16_t> out(2 * ns);
    tone::Channel L, R;
    size_t k_in = 0;             // input sample in force
    int32_t xl = in[0], xr = in[1];
    size_t k_out = 0;
    const int64_t last = s.out[ns - 1];
    for (int64_t e = 0; e <= last; ++e) {
        while (k_in + 1 < ns && s.sw[k_in + 1] <= e) {
            ++k_in;
            xl = in[2 * k_in]; xr = in[2 * k_in + 1];
        }
        L.step(xl, ac);
        R.step(xr, ac);
        while (k_out < ns && s.out[k_out] == e) {
            out[2 * k_out] = static_cast<int16_t>(L.audio);
            out[2 * k_out + 1] = static_cast<int16_t>(R.audio);
            ++k_out;
        }
    }
    return out;
}

static std::vector<int16_t> runGss(const std::vector<int16_t> &in, size_t ns) {
    PhasorAudio::WarmthChannel L, R;
    std::vector<int16_t> out(2 * ns);
    for (size_t k = 0; k < ns; ++k) {
        out[2 * k] = L.processPcm(in[2 * k]);
        out[2 * k + 1] = R.processPcm(in[2 * k + 1]);
    }
    return out;
}

struct Diff { size_t n = 0; int max = 0; size_t at = 0; double win = 0; size_t win_at = 0; double snr = 0; };
static Diff diff(const std::vector<int16_t> &a, const std::vector<int16_t> &b, int ch, size_t ns) {
    Diff r;
    double pa = 0, pd = 0, w = 0;
    size_t wn = 0, wi = 0;
    for (size_t k = 0; k < ns; ++k) {
        const int e = int(a[2 * k + ch]) - int(b[2 * k + ch]);
        pa += double(a[2 * k + ch]) * a[2 * k + ch];
        pd += double(e) * e;
        if (e) ++r.n;
        if (std::abs(e) > r.max) { r.max = std::abs(e); r.at = k; }
        w += double(e) * e;
        if (++wn == 480) {
            const double rms = std::sqrt(w / 480);
            if (rms > r.win) { r.win = rms; r.win_at = wi * 480; }
            w = 0; wn = 0; ++wi;
        }
    }
    r.snr = pd == 0 ? INFINITY : (pa == 0 ? -INFINITY : 10 * std::log10(pa / pd));
    return r;
}

int main(int argc, char **argv) {
    std::string dir, in_path, out_path, model_out, gss_out;
    bool card = true, quiet = false;
    int d = -100000, d0 = -100000, d1 = -100000, dstep = 1;
    uint32_t ac = (8u << 15) | (1u << 25);   // warmth +8, rtl_card.hpp's default
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--clock") card = std::string(argv[++i]) == "card";
        else if (a == "--d") d = std::atoi(argv[++i]);
        else if (a == "--scan") { d0 = std::atoi(argv[++i]); d1 = std::atoi(argv[++i]); if (i + 1 < argc && argv[i + 1][0] != '-') dstep = std::atoi(argv[++i]); }
        else if (a == "--in") in_path = argv[++i];
        else if (a == "--out") out_path = argv[++i];
        else if (a == "--model-out") model_out = argv[++i];
        else if (a == "--gss-out") gss_out = argv[++i];
        else if (a == "--ac") ac = std::strtoul(argv[++i], nullptr, 0);
        else if (a == "--quiet") quiet = true;
        else dir = a;
    }
    std::vector<int16_t> in, ref;
    if (!dir.empty()) {
        std::vector<int16_t> sec = load(dir + "/sec.pcm"), pri = load(dir + "/pri.pcm");
        ref = load(dir + "/card.pcm");
        in.resize(2 * sec.size());
        for (size_t k = 0; k < sec.size(); ++k) { in[2 * k] = sec[k]; in[2 * k + 1] = pri[k]; }
        if (ref.size() != in.size()) { std::fprintf(stderr, "card.pcm length differs\n"); return 2; }
    } else if (!in_path.empty()) {
        in = load(in_path);
    } else {
        std::fprintf(stderr, "usage: tone_check DIR | --in IN.pcm [--out OUT.pcm] [--clock card|fixed] [--d D] [--scan D0 D1]\n");
        return 2;
    }
    const size_t ns = in.size() / 2;
    if (!ns) { std::printf("empty\n"); return 0; }
    if (d0 > -100000) {
        for (int dd = d0; dd <= d1; dd += dstep) {
            auto m = runModel(in, ns, card, dd, ac);
            Diff l = diff(ref, m, 0, ns), r = diff(ref, m, 1, ns);
            std::printf("d=%d model-vs-rtl L %zu diff (max %d) R %zu diff (max %d)\n", dd, l.n, l.max, r.n, r.max);
        }
        return 0;
    }
    if (d == -100000) d = 151;
    auto m = runModel(in, ns, card, d, ac);
    auto g = runGss(in, ns);
    if (!model_out.empty()) save(model_out, m);
    if (!gss_out.empty()) save(gss_out, g);
    if (!out_path.empty()) save(out_path, m);
    int rc = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const char *C = ch ? "R" : "L";
        if (!ref.empty()) {
            Diff mr = diff(ref, m, ch, ns), gr = diff(ref, g, ch, ns);
            std::printf("%s model-vs-rtl %zu/%zu diff max %d | gss-vs-rtl %zu diff max %d @%zu win-rms %.1f @%zu snr %.1f dB\n",
                        C, mr.n, ns, mr.max, gr.n, gr.max, gr.at, gr.win, gr.win_at, gr.snr);
            if (mr.n) rc = 1;
        } else {
            Diff gm = diff(m, g, ch, ns);
            std::printf("%s gss-vs-model %zu/%zu diff max %d @%zu win-rms %.1f @%zu snr %.1f dB\n",
                        C, gm.n, ns, gm.max, gm.at, gm.win, gm.win_at, gm.snr);
        }
    }
    (void)quiet;
    return rc;
}
