// Debug build of card_gss: per-sample internal state of one GSSquared
// SSI263 (env SSI_DBG=file, SSI_DBG_SEL=0 secondary / 1 primary), in the
// same format as card_rtl's SSI_DBG dump. Reaches into SSI263::Impl by
// compiling SSI263.cpp into this translation unit.
#define private public
#include "exp/SSI263_exp.cpp"
#undef private
#include <cstdio>
static FILE *g_dbg = nullptr;
static int g_sel = 1;
static unsigned long long g_n = 0;
static void dbgOpen() {
    static bool done = false;
    if (done) return;
    done = true;
    const char *p = std::getenv("SSI_DBG");
    if (p) g_dbg = std::fopen(p, "w");
    const char *s = std::getenv("SSI_DBG_SEL");
    g_sel = s ? std::atoi(s) : 1;
}
static unsigned packFilt(const FormantCore &c) {
    return (unsigned)(c.filt_va & 15) << 28 | (unsigned)(c.filt_fa & 15) << 24 | (unsigned)(c.filt_fc & 15) << 20 |
           (unsigned)(c.filt_f1 & 15) << 16 | (unsigned)(c.filt_f2 & 31) << 11 | (unsigned)(c.filt_f2q & 15) << 7 |
           (unsigned)(c.filt_f3 & 15) << 3;
}
static void dbgPre(SSI263 &sec, SSI263 &pri) {
    dbgOpen();
    if (!g_dbg) return;
    SSI263 &v = g_sel ? pri : sec;
    const FormantCore &c = v.impl_->core;
    unsigned cur = (unsigned)c.cur_va << 24 | (unsigned)c.cur_fa << 16 | (unsigned)c.cur_f1 << 8 | c.cur_f3;
    std::fprintf(g_dbg, "%llu P pitch=%u ticks=%u clos=%u gate=%u nz=%u upd=%u filt=%08X cur=%08X",
                 g_n, c.pitch, c.ticks, c.closure_age, c.pitch_noise_gate, c.noise_bit, c.update_counter,
                 packFilt(c), cur);
}
static void dbgPost(SSI263 &sec, SSI263 &pri) {
    if (!g_dbg) return;
    SSI263 &v = g_sel ? pri : sec;
    const FormantCore &c = v.impl_->core;
    std::fprintf(g_dbg, " out=%d\n%llu F filt=%08X\n", (int)v.impl_->synth.visible_output_, g_n, packFilt(c));
    ++g_n;
}
#define GSS_DBG_PRE(s, p) dbgPre(s, p)
#define GSS_DBG_POST(s, p) dbgPost(s, p)
#include "card_gss.cpp"
