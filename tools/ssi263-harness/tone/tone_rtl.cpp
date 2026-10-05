// The Phasor's tone stage itself (obj/tone_stage.sv, cut verbatim from
// mockingboard.sv by tone/extract_tone.py) under Verilator, clocked on the
// same schedule as bin/card_rtl (tone/schedule.hpp).
//
//   tone_rtl IN.pcm OUT.pcm [--clock card|fixed] [--d D] [--ac WORD]
//       IN: stereo int16, one speech sample per 48 kHz tick (left = A5
//       secondary, right = A6 primary); OUT: the stage's output per tick.
#include "Vtone_stage.h"
#include "verilated.h"
#include "../tone/schedule.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tone_rtl IN.pcm OUT.pcm [--clock card|fixed] [--d D] [--ac WORD]\n");
        return 2;
    }
    bool card = true;
    int d = 151;
    uint32_t ac = (8u << 15) | (1u << 25);
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--clock") card = std::string(argv[++i]) == "card";
        else if (a == "--d") d = std::atoi(argv[++i]);
        else if (a == "--ac") ac = std::strtoul(argv[++i], nullptr, 0);
    }
    std::vector<int16_t> in;
    {
        FILE *f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        int16_t b[4096];
        size_t n;
        while ((n = std::fread(b, 2, 4096, f)) > 0) in.insert(in.end(), b, b + n);
        std::fclose(f);
    }
    const size_t ns = in.size() / 2;
    if (!ns) return 0;
    auto ctx = std::make_unique<VerilatedContext>();
    ctx->commandArgs(1, argv);
    auto top = std::make_unique<Vtone_stage>(ctx.get());
    auto clk = [&]() { top->clk = 1; top->eval(); top->clk = 0; top->eval(); };
    top->audio_control = ac;
    top->in_l = 0; top->in_r = 0;
    top->rstn = 0;
    for (int i = 0; i < 8; ++i) clk();
    top->rstn = 1;
    tone::Schedule s = tone::makeSchedule(ns, card, d);
    std::vector<int16_t> out(2 * ns);
    size_t k_in = 0, k_out = 0;
    top->in_l = in[0]; top->in_r = in[1];
    for (int64_t e = 0; e <= s.out[ns - 1]; ++e) {
        while (k_in + 1 < ns && s.sw[k_in + 1] <= e) {
            ++k_in;
            top->in_l = in[2 * k_in]; top->in_r = in[2 * k_in + 1];
        }
        clk();
        while (k_out < ns && s.out[k_out] == e) {
            out[2 * k_out] = (int16_t)top->audio_l;
            out[2 * k_out + 1] = (int16_t)top->audio_r;
            ++k_out;
        }
    }
    top->final();
    FILE *f = std::fopen(argv[2], "wb");
    if (!f) { std::perror(argv[2]); return 2; }
    std::fwrite(out.data(), 2, out.size(), f);
    std::fclose(f);
    return 0;
}
