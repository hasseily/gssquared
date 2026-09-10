#include "devices/displaypp/generate/AppletiniTextOverlay.hpp"
#include "devices/displaypp/generate/AppletiniTextOverlayFont.hpp"
#include "mmus/mmu_ii.hpp"
#include "mmus/mmu_iie.hpp"
#include "mmus/mmu_iigs.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

uint64_t debug_level = 0;

namespace {
int failures = 0;
void expect(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

struct Harness {
    AppletiniTextOverlay text;
    uint64_t cycle = 100;
    void set(uint8_t index, uint8_t value) { text.write(0, index, cycle); text.write(1, value, cycle); }
    uint8_t get(uint8_t index) { text.write(0, index, cycle); return text.read(1, cycle); }
    void cmd(uint8_t value) { text.write(3, value, cycle); }
    void ready() { cycle += AppletiniTextOverlay::ARM_CLEAR_CYCLES; text.advance(cycle); }
    void configure(uint16_t base = 0x6000, uint8_t config = 0x00, uint8_t cols = 1, uint8_t rows = 1) {
        set(0, base & 0xFF); set(1, base >> 8); set(2, config); set(3, cols); set(4, rows);
    }
    void store(uint32_t address, uint8_t value) { text.capture_write(address, value, cycle); }
    AppletiniTextOverlay::Frame frame(uint64_t ms = 0, bool shr = false) { return text.frame(shr, cycle, ms); }
};

bool pixel_is(const AppletiniTextOverlay::Frame &frame, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const auto *p = reinterpret_cast<const uint8_t *>(frame.pixels + y * frame.width + x);
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

void test_registers() {
    Harness h;
    expect(h.text.status() == 0, "reset hidden and unarmed");
    expect(h.get(2) == 8 && h.get(3) == 80 && h.get(4) == 24 && h.get(9) == 0x11,
           "firmware register reset defaults");
    expect(h.get(0x1E) == 0x7F, "capabilities");
    const char *magic = "LINTXT";
    h.text.write(0, 0xAA, h.cycle);
    for (int i = 0; i < 6; ++i) expect(h.text.read(8+i, h.cycle) == magic[i], "fixed detection magic");
    expect(h.text.read(14, h.cycle) == 0x4C && h.text.read(15, h.cycle) == 0x10,
           "signature/version do not depend on INDEX");
    h.text.write(0, 0xFF, h.cycle);
    expect(h.text.read(2, h.cycle) == 0 && h.text.read(0, h.cycle) == 0, "DATA_INC read wraps INDEX");
    h.text.write(0, 0xFF, h.cycle); h.text.write(2, 0xAA, h.cycle);
    expect(h.text.read(0, h.cycle) == 0, "DATA_INC write wraps INDEX");
    h.set(0x0B, 0xFF); h.set(0x0C, 0xFF);
    expect(h.get(0x0B) == 0x7F && h.get(0x0C) == 0x0F, "cursor reserved bits masked");
    h.cmd(1);
    expect((h.text.status() & AppletiniTextOverlay::CONFIG_ERROR) != 0, "reset BASE rejects ARM");
    h.text.reset(); h.configure(); h.cmd(1);
    expect(h.text.read(4, h.cycle) == AppletiniTextOverlay::BUSY, "BUSY set on command cycle");
    h.cmd(0); h.cmd(2); h.cmd(3);
    expect(h.text.status() == AppletiniTextOverlay::BUSY, "all commands ignored while BUSY");
    h.ready();
    expect(h.text.status() == AppletiniTextOverlay::ARMED, "ARM becomes ready");
    h.cmd(2);
    expect(h.text.status() == (AppletiniTextOverlay::ARMED | AppletiniTextOverlay::FRAME_PENDING),
           "SHOW status pending before frame edge");
    h.set(0, 0xFF); h.cmd(1); // ARM must not replace a pending SHOW snapshot.
    expect(h.frame().visible && h.get(0x14) == 0 && h.get(0x15) == 0x60, "SHOW atomically uses armed layout");
    h.text.set_canvas(true);
    expect((h.get(0x11) << 8 | h.get(0x10)) == 1280 && (h.get(0x13) << 8 | h.get(0x12)) == 800,
           "SHR canvas readback");
}

void test_capture_and_handoff() {
    Harness h;
    h.configure();
    h.store(0x6000, 0xDB); h.store(0x6001, 0x42);
    h.cmd(1); h.store(0x6001, 0x42); h.ready(); h.cmd(2);
    auto f = h.frame();
    expect(pixel_is(f,0,0,0,0,0), "ARM uses fill cells, not preexisting/pre-ARM/BUSY RAM");
    h.store(0x6001, 0x40);
    f = h.frame(); expect(pixel_is(f,0,0,170,0,0), "active captured writes appear");
    h.store(0x16001, 0x10); h.store(0x26001, 0x10); h.store(0x5FFF, 0x10); h.store(0x6002, 0x10);
    f = h.frame(); expect(pixel_is(f,0,0,170,0,0), "wrong bank/RamWorks/range writes ignored");
    const auto stable = f.generation;
    expect(h.frame().generation == stable, "unchanged frame reuses generation");

    h.configure(0x7001, 0x01); h.cmd(1); h.ready();
    h.store(0x6001, 0x10); h.store(0x17002, 0x20);
    expect(pixel_is(h.frame(),0,0,170,0,0), "old active shadow frozen while next buffer fills");
    h.cmd(2); f = h.frame();
    expect(pixel_is(f,0,0,0,170,0) && h.get(0x14) == 1 && h.get(0x15) == 0x70,
           "double-buffer swap uses aux and unaligned address");
    h.cmd(3); expect(h.frame().visible == false && (h.text.status() & AppletiniTextOverlay::ARMED), "HIDE keeps capture");
    h.store(0x17002, 0x10); h.cmd(2); expect(pixel_is(h.frame(),0,0,0,0,170), "HIDE then SHOW keeps live captured bytes");
    h.cmd(0); h.frame();
    expect(h.text.status() == 0, "OFF hides and disarms");
    h.cmd(2); expect(!h.frame().visible, "SHOW ignored after OFF");
}

void test_errors() {
    Harness h; h.configure(); h.cmd(1); h.ready(); h.cmd(2); h.frame();
    h.configure(0xBFFF); h.cmd(1); h.ready();
    expect(h.text.status() == (AppletiniTextOverlay::VISIBLE | AppletiniTextOverlay::ARMED | AppletiniTextOverlay::CONFIG_ERROR),
           "range overflow preserves active and armed state");
    h.store(0x6001,0x40); expect(pixel_is(h.frame(),0,0,170,0,0), "old armed capture preserved after invalid ARM");
    h.configure(0xBFFE); h.cmd(1); h.ready();
    expect(!(h.text.status() & AppletiniTextOverlay::CONFIG_ERROR), "range ending exactly at C000 accepted");
    h.text.capture_gap();
    expect((h.text.status() & AppletiniTextOverlay::STALE) && !(h.text.status() & AppletiniTextOverlay::ARMED), "capture loss marks stale and disarms");
    expect(!h.frame().visible, "stale hides on frame edge");
    h.cmd(2); expect(!h.frame().visible, "stale cannot SHOW");
    h.configure(); h.cmd(1); h.ready(); expect(h.text.status() == AppletiniTextOverlay::ARMED, "valid ARM clears stale/error");
    for (auto invalid : std::array<std::array<uint8_t,2>,5>{{{{2,0x20}},{{3,0}},{{4,128}},{{9,0x10}},{{9,0x01}}}}) {
        h.configure(); h.set(9,0x11); h.set(invalid[0],invalid[1]); h.cmd(1);
        expect((h.text.status() & AppletiniTextOverlay::CONFIG_ERROR) != 0, "invalid reserved/grid/scale rejected");
    }
    h.text.reset(); expect(h.text.status() == 0 && !h.frame().visible, "reset cancels all active state");
}

void test_glyphs_and_effects() {
    Harness h; h.configure(0x6000, 0x10); h.cmd(1); h.ready();
    h.store(0x6000, 0xA0); h.store(0x6001, 0x04); h.cmd(2);
    auto f = h.frame();
    expect(pixel_is(f,0,0,0,0,0,0) && pixel_is(f,7,12,170,0,0)
        && pixel_is(f,7,13,0,0,0,0), "VT100 underlined space, last row clear and transparent");
    h.store(0x6000, 0); f = h.frame();
    for (int y=0; y<14; ++y) for (int x=0; x<8; ++x) {
        const bool on = linear_text_overlay_dec_font_8x14[0][y] & (0x80 >> x);
        expect(on ? pixel_is(f,x,y,170,0,0) : pixel_is(f,x,y,0,0,0,0), "DEC glyph map matches firmware bitmap");
    }
    h.configure(0x7000,0x06); h.set(9,0x22); h.set(5,3); h.set(7,5); h.cmd(1); h.ready();
    h.store(0x7000,0xDB); h.store(0x7001,0x2C); h.cmd(2); f=h.frame();
    for (int y=0;y<16;++y) for(int x=0;x<8;++x) {
        const bool on=linear_text_overlay_cp437_font_8x16[0xDB][y]&(0x80>>x);
        expect(on ? pixel_is(f,3+x*2,5+y*2,255,85,85) : pixel_is(f,3+x*2,5+y*2,0,170,0), "CP437/font16/scaling/palette");
    }
    h.configure(0x6000,0x08); h.set(9,0x11);h.set(5,0);h.set(7,0);h.cmd(1);h.ready();
    h.store(0x6000,0xA0);h.store(0x6001,0x84);h.cmd(2);
    expect(pixel_is(h.frame(499),0,12,170,0,0), "cell blink visible through 499ms");
    expect(pixel_is(h.frame(500),0,12,0,0,0), "cell blink hidden at 500ms");
    expect(pixel_is(h.frame(1000),0,12,170,0,0), "cell blink visible at 1000ms");
    h.store(0x6000,0x20);h.store(0x6001,0x14); h.set(0x0C,1);
    expect(pixel_is(h.frame(),0,0,170,0,0), "block cursor swaps foreground/background");
    h.set(0x0C,5); expect(pixel_is(h.frame(),7,12,170,0,0) && pixel_is(h.frame(),7,13,0,0,170), "underline cursor one logical row");
    h.set(0x0C,9); expect(pixel_is(h.frame(),0,0,170,0,0) && pixel_is(h.frame(),1,0,0,0,170), "left cursor one logical column");
    h.set(0x0C,11); expect(pixel_is(h.frame(500),0,0,0,0,170), "cursor shares 500ms blink");
    h.set(0x0A,1); expect(pixel_is(h.frame(),0,0,0,0,170), "out-of-grid cursor hidden");
    h.configure(); h.set(5,0xFF);h.set(6,0xFF);h.cmd(1);h.ready();h.cmd(2);
    f=h.frame(); expect(pixel_is(f,0,0,0,0,0,0), "valid off-canvas origin clipped");
}

void test_mapped_writes() {
    std::array<uint8_t, 0x3000> rom{};
    MMU_II mmu(256, 0x20000, rom.data());
    std::vector<std::pair<uint32_t,uint8_t>> seen;
    mmu.set_ram_write_observer({[](void *p,uint32_t a,uint8_t v) {
        static_cast<decltype(seen)*>(p)->emplace_back(a,v);
    }, &seen});
    uint8_t *ram=mmu.get_memory_base();
    mmu.map_page_write(0x60,ram+0x16000,"AUX");
    mmu.write(0x6007,0xAB);
    expect(seen.size()==1 && seen[0].first==0x16007 && seen[0].second==0xAB && ram[0x16007]==0xAB,
           "capture observes resolved auxiliary write target, not read bank");
    mmu.map_page_write(0x60,ram+0x6000,"MAIN"); mmu.write(0x6007,0xCD);
    expect(seen.size()==2 && seen.back().first==0x6007,"bank remap immediately changes observer address");
    std::array<uint8_t,256> expanded{};
    mmu.map_page_write(0x60,expanded.data(),"RAMWORKS");mmu.write(0x6007,0xEF);
    expect(seen.size()==2 && expanded[7]==0xEF,"expanded RamWorks memory not captured as base aux");
    mmu.map_page_write(0x60,nullptr,"ROM");mmu.write(0x6007,0xAA);
    expect(seen.size()==2,"non-RAM writes not captured");
    mmu.set_ram_write_observer({nullptr,nullptr});
}

void test_iigs_bus_writes() {
    std::vector<uint8_t> rom(0x20000);
    MMU_IIe megaii(256, 0x20000, rom.data() + 0x1C000);
    MMU_IIgs gs(256, 0x200000, rom.size(), rom.data(), &megaii);
    gs.init_map();
    std::vector<uint32_t> seen;
    megaii.set_ram_write_observer({[](void *p,uint32_t address,uint8_t) {
        static_cast<std::vector<uint32_t> *>(p)->push_back(address);
    }, &seen});
    gs.write(0xE12000,0x11);
    expect(seen.size()==1 && seen.back()==0x12000,"IIgs direct E1 write captured as auxiliary");
    gs.write(0xE0C029,0xC1); // Linear auxiliary addresses must stay logical on the bus.
    gs.write(0xE120A5,0x22);
    expect(seen.size()==2 && seen.back()==0x120A5,"IIgs linear SHR write captures logical address");
    gs.write(0x0120A5,0x33);
    expect(seen.size()==3 && seen.back()==0x120A5,"IIgs fast RAM shadow to E1 captured once");
    gs.write(0x026007,0x44);
    expect(seen.size()==3,"IIgs unshadowed fast RAM excluded");
    gs.write(0xE0C005,0); // RAMWRT routes E0 writes to aux, independently of RAMRD.
    gs.write(0xE06007,0x55);
    expect(seen.size()==4 && seen.back()==0x16007,"IIgs RAMWRT banking resolved before capture");
    gs.write(0xE0C001,0); // 80STORE + PAGE1 forces text writes to main despite RAMWRT.
    gs.write_c0xx(0xC054,0);
    gs.write(0xE00400,0x66);
    expect(seen.size()==5 && seen.back()==0x00400,"80STORE PAGE1 overrides AUXWRITE for text");
    gs.write_c0xx(0xC055,0);
    gs.write(0xE00400,0x77);
    expect(seen.size()==6 && seen.back()==0x10400,"80STORE PAGE2 selects auxiliary text write");
    megaii.set_ram_write_observer({nullptr,nullptr});
}
}

int main() {
    test_registers(); test_capture_and_handoff(); test_errors(); test_glyphs_and_effects(); test_mapped_writes(); test_iigs_bus_writes();
    if (failures) { std::fprintf(stderr,"%d Appletini text tests failed\n",failures); return 1; }
    std::puts("Appletini text register, capture, handoff, glyph, cursor, blink and MMU tests passed");
    return 0;
}
