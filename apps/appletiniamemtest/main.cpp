#include <cstdio>
#include <cstring>
#include <vector>

#include "devices/pdblock3/AppletiniMemoryApi.hpp"

namespace {

using Api = AppletiniMemoryApi;

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

struct Machine {
    std::vector<uint8_t> main = std::vector<uint8_t>(Api::BANK_SIZE);
    std::vector<uint8_t> aux = std::vector<uint8_t>(Api::BANK_SIZE);
    std::vector<uint8_t> ramworks = std::vector<uint8_t>(127 * Api::BANK_SIZE);
    bool with_ramworks = true;

    Machine() {
        for (size_t i = 0; i < main.size(); ++i) main[i] = static_cast<uint8_t>(i * 7 + 1);
        for (size_t i = 0; i < aux.size(); ++i) aux[i] = static_cast<uint8_t>(i * 5 + 3);
        for (size_t i = 0; i < ramworks.size(); ++i) {
            ramworks[i] = static_cast<uint8_t>((i >> 16) ^ (i * 3));
        }
    }

    Api::Memory memory() {
        Api::Memory m;
        m.main = main.data();
        m.aux = aux.data();
        if (with_ramworks) {
            m.ramworks = ramworks.data();
            m.ramworks_size = ramworks.size();
        }
        return m;
    }

    /* $C073's bank n (1-127) */
    uint8_t *bank(unsigned n) { return ramworks.data() + (n - 1) * Api::BANK_SIZE; }
};

struct List {
    std::vector<uint8_t> bytes;

    explicit List(uint8_t count) {
        const uint8_t head[] = {'A', 'M', 'E', 'M', 1, count, 0, 0};
        bytes.assign(head, head + sizeof(head));
    }

    List &copy(uint8_t sspace, uint8_t sbank, uint16_t saddr,
               uint8_t dspace, uint8_t dbank, uint16_t daddr,
               uint16_t length, uint8_t flags) {
        const uint8_t d[16] = {
            Api::OP_COPY, flags, sspace, sbank,
            static_cast<uint8_t>(saddr), static_cast<uint8_t>(saddr >> 8),
            dspace, dbank,
            static_cast<uint8_t>(daddr), static_cast<uint8_t>(daddr >> 8),
            static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
            0, 0, 0, 0};
        bytes.insert(bytes.end(), d, d + 16);
        return *this;
    }

    List &fill(uint8_t dspace, uint8_t dbank, uint16_t daddr,
               uint16_t length, uint8_t value, uint8_t flags) {
        const uint8_t d[16] = {
            Api::OP_FILL, flags, 0, 0, 0, 0,
            dspace, dbank,
            static_cast<uint8_t>(daddr), static_cast<uint8_t>(daddr >> 8),
            static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
            value, 0, 0, 0};
        bytes.insert(bytes.end(), d, d + 16);
        return *this;
    }

    /* The SmartPort CONTROL frame DOOM pushes into $CFF0. */
    std::vector<uint8_t> frame() const {
        std::vector<uint8_t> f = {4, 3, 0, 0x00, 0x00, 0x80, 0, 0, 0, 0,
                                  static_cast<uint8_t>(bytes.size()),
                                  static_cast<uint8_t>(bytes.size() >> 8)};
        f.insert(f.end(), bytes.begin(), bytes.end());
        return f;
    }
};

uint8_t run(Api &api, Machine &m, const List &list) {
    const std::vector<uint8_t> f = list.frame();
    return api.control_frame(f.data(), f.size(), m.memory());
}

void test_status() {
    Api api;
    Machine m;
    uint8_t s[Api::STATUS_SIZE];
    api.status(s, m.memory());
    /* What DOOM's probe_amem checks, and the rest of the block. */
    const uint8_t head[16] = {'A', 'M', 'E', 'M', 1, 0, 16, 16,
                              7, 0, 0x00, 0x02, 0x00, 0xC0, 126, 1};
    expect(std::memcmp(s, head, 16) == 0, "STATUS capability bytes 0-15");
    expect(s[20] == 0x00 && s[21] == 0x02, "STATUS legacy chunk 512");
    expect(s[22] == 0 && s[23] == 0 && s[28] == 0, "STATUS results start clear");

    Api::Memory none;
    api.status(s, none);
    expect(s[15] == 0, "STATUS unavailable without the IIe memory");
}

void test_copy_and_fill() {
    Api api;
    Machine m;
    Machine before = m;

    /* DOOM's first request: MAIN $0200- into bank 122, then a private fill
       and a restore, in one list. */
    List l(3);
    l.copy(0, 0, 0x0200, 1, 122, 0x0200, 0xBE00, 0)
     .fill(0, 0, 0x0200, 0x0100, 0xA5, Api::FLAG_PRIVATE)
     .copy(1, 122, 0x0300, 0, 0, 0x0300, 0x0010, Api::FLAG_PRIVATE);
    expect(run(api, m, l) == Api::OK, "copy/fill/copy list succeeds");
    expect(std::memcmp(m.bank(122) + 0x200, before.main.data() + 0x200, 0xBE00) == 0,
           "MAIN $0200-$BFFF lands in bank 122");
    expect(m.bank(122)[0x1FF] == before.bank(122)[0x1FF] &&
           m.bank(122)[0xC000] == before.bank(122)[0xC000],
           "bank 122 outside the copy is untouched");
    expect(m.main[0x1FF] == before.main[0x1FF] && m.main[0x200] == 0xA5 &&
           m.main[0x2FF] == 0xA5 && m.main[0x300] == before.main[0x300],
           "the fill covers exactly $0200-$02FF");
    expect(std::memcmp(m.aux.data(), before.aux.data(), Api::BANK_SIZE) == 0,
           "base AUX untouched by MAIN/bank requests");
    expect(api.last_completed_descriptors() == 3 &&
           api.last_completed_bytes() == 0xBE00 + 0x100 + 0x10,
           "results count descriptors and bytes");

    uint8_t s[Api::STATUS_SIZE];
    api.status(s, m.memory());
    expect(s[22] == 0 && s[23] == 3 && s[28] == 0x10 && s[29] == 0xBF,
           "STATUS reports the last CONTROL");
}

void test_ordered_and_banks() {
    Api api;
    Machine m;
    Machine before = m;

    /* Later descriptors read earlier destinations; unaligned endpoints;
       MAIN and AUX 0 at the same address are different banks. */
    List l(4);
    l.fill(1, 5, 0x1001, 3, 0x11, 0)
     .copy(1, 5, 0x1000, 1, 6, 0x2003, 5, 0)
     .copy(1, 6, 0x2003, 1, 0, 0x2003, 5, Api::FLAG_PRIVATE)
     .copy(1, 0, 0x2003, 0, 0, 0x2003, 5, Api::FLAG_PRIVATE);
    expect(run(api, m, l) == Api::OK, "ordered list succeeds");
    const uint8_t want[5] = {before.bank(5)[0x1000], 0x11, 0x11, 0x11,
                             before.bank(5)[0x1004]};
    expect(std::memcmp(m.bank(6) + 0x2003, want, 5) == 0, "second reads the first's fill");
    expect(std::memcmp(m.aux.data() + 0x2003, want, 5) == 0, "AUX 0 is base aux");
    expect(std::memcmp(m.main.data() + 0x2003, want, 5) == 0, "MAIN reads AUX 0");
    expect(m.bank(6)[0x2002] == before.bank(6)[0x2002] &&
           m.bank(6)[0x2008] == before.bank(6)[0x2008],
           "bytes around an unaligned endpoint are preserved");
    expect(m.bank(5)[0x1000] == before.bank(5)[0x1000] &&
           m.bank(5)[0x1004] == before.bank(5)[0x1004], "fill bounds");

    /* Bank 126 is the highest, $BFFF the last byte. */
    List top(1);
    top.fill(1, 126, 0xBFFF, 1, 0x5A, 0);
    expect(run(api, m, top) == Api::OK && m.bank(126)[0xBFFF] == 0x5A &&
           m.bank(127)[0xBFFF] == before.bank(127)[0xBFFF], "bank 126 $BFFF");
}

void test_refusals() {
    struct Case { const char *name; List list; uint8_t want; };
    std::vector<Case> cases;

    auto add = [&](const char *name, List l, uint8_t want) {
        cases.push_back({name, l, want});
    };

    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[4] = 2;
      add("version 2", l, Api::BAD_HEADER); }
    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[6] = 1;
      add("header flags", l, Api::BAD_HEADER); }
    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[7] = 1;
      add("header reserved", l, Api::BAD_HEADER); }
    { List l(2); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0);
      add("count above the descriptors sent", l, Api::BAD_HEADER); }
    { List l(0); add("count 0", l, Api::BAD_HEADER); }
    { List l(17);
      for (int i = 0; i < 17; ++i) l.fill(1, 3, 0x2000, 1, 0, 0);
      add("17 descriptors", l, Api::BAD_HEADER); }
    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[0] = 'B';
      add("signature", l, Api::BAD_HEADER); }

    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[8] = 3;
      add("operation 3", l, Api::BAD_DESCRIPTOR); }
    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 2);
      add("flag bit 1", l, Api::BAD_DESCRIPTOR); }
    { List l(1); l.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0).bytes[8 + 12] = 1;
      add("COPY with a fill value", l, Api::BAD_DESCRIPTOR); }
    { List l(1); l.fill(1, 3, 0x2000, 0x10, 0, 0).bytes[8 + 4] = 1;
      add("FILL with a source", l, Api::BAD_DESCRIPTOR); }
    { List l(1); l.fill(1, 3, 0x2000, 0x10, 0, 0).bytes[8 + 15] = 1;
      add("reserved byte", l, Api::BAD_DESCRIPTOR); }

    { List l(1); l.fill(1, 3, 0x01FF, 1, 0, 0); add("below $0200", l, Api::RANGE); }
    { List l(1); l.fill(1, 3, 0xBFFF, 2, 0, 0); add("past $C000", l, Api::RANGE); }
    { List l(1); l.fill(1, 3, 0xFFFF, 2, 0, 0); add("wrapping", l, Api::RANGE); }
    { List l(1); l.fill(1, 3, 0x2000, 0, 0, 0); add("zero count", l, Api::RANGE); }
    { List l(1); l.fill(1, 127, 0x2000, 1, 0, 0); add("bank 127", l, Api::RANGE); }
    { List l(1); l.fill(0, 1, 0x2000, 1, 0, 1); add("MAIN bank 1", l, Api::RANGE); }
    { List l(1); l.fill(2, 0, 0x2000, 1, 0, 1); add("space 2", l, Api::RANGE); }
    { List l(1); l.copy(0, 0, 0x01FF, 1, 3, 0x2000, 1, 0);
      add("source below $0200", l, Api::RANGE); }

    { List l(1); l.copy(1, 3, 0x2000, 1, 3, 0x2000, 0x10, 0);
      add("identical interval", l, Api::OVERLAP); }
    { List l(1); l.copy(1, 3, 0x2000, 1, 3, 0x200F, 0x10, 0);
      add("one byte of overlap", l, Api::OVERLAP); }
    { List l(1); l.copy(0, 0, 0x3000, 0, 0, 0x2FF1, 0x10, 1);
      add("MAIN overlap", l, Api::OVERLAP); }

    { List l(1); l.fill(0, 0, 0x2000, 1, 0, 0);
      add("MAIN destination without PRIVATE", l, Api::PRIVATE_REQUIRED); }
    { List l(1); l.copy(1, 4, 0x2000, 1, 0, 0x2000, 1, 0);
      add("AUX 0 destination without PRIVATE", l, Api::PRIVATE_REQUIRED); }

    /* The whole list validates before the first write. */
    { List l(2); l.fill(1, 3, 0x2000, 0x10, 0xEE, 0).fill(1, 3, 0x2000, 0, 0, 0);
      add("good then bad range", l, Api::RANGE); }
    { List l(2); l.fill(1, 3, 0x2000, 0x10, 0xEE, 0).fill(0, 0, 0x2000, 1, 0, 0);
      add("good then missing PRIVATE", l, Api::PRIVATE_REQUIRED); }

    for (const Case &c : cases) {
        Api api;
        Machine m;
        Machine before = m;
        const uint8_t got = run(api, m, c.list);
        if (got != c.want) {
            std::fprintf(stderr, "FAIL: %s: $%02X, wanted $%02X\n", c.name, got, c.want);
            ++failures;
        }
        if (m.main != before.main || m.aux != before.aux || m.ramworks != before.ramworks) {
            std::fprintf(stderr, "FAIL: %s: memory changed\n", c.name);
            ++failures;
        }
        expect(api.last_completed_descriptors() == 0 && api.last_completed_bytes() == 0,
               "a refusal completes nothing");
    }

    /* Different banks at the same address do not overlap. */
    Api api;
    Machine m;
    List l(1);
    l.copy(1, 3, 0x2000, 1, 4, 0x2000, 0x10, 0);
    expect(run(api, m, l) == Api::OK, "same address, different banks");
}

void test_frames_and_ramworks() {
    Api api;
    Machine m;
    List l(1);
    l.fill(1, 3, 0x2000, 1, 0, 0);

    std::vector<uint8_t> f = l.frame();
    f[1] = 2;
    expect(api.control_frame(f.data(), f.size(), m.memory()) == Api::BAD_HEADER,
           "parameter count other than 3");
    f = l.frame();
    f.push_back(0);
    expect(api.control_frame(f.data(), f.size(), m.memory()) == Api::BAD_HEADER,
           "length word disagrees with the bytes sent");
    f = l.frame();
    f[6] = f[7] = f[8] = f[9] = 0xFF;
    expect(api.control_frame(f.data(), f.size(), m.memory()) == Api::OK,
           "ROM padding bytes are ignored");

    m.with_ramworks = false;
    Machine before = m;
    expect(run(api, m, l) == Api::UNAVAILABLE, "RamWorks off: extended AUX unavailable");
    List l2(1);
    l2.copy(1, 0, 0x4000, 0, 0, 0x4000, 0x100, Api::FLAG_PRIVATE);
    expect(run(api, m, l2) == Api::OK &&
           std::memcmp(m.main.data() + 0x4000, before.aux.data() + 0x4000, 0x100) == 0,
           "RamWorks off: MAIN/AUX 0 still work");
    List l3(1);
    l3.copy(0, 0, 0x2000, 1, 3, 0x2000, 0x10, 0);
    l3.fill(0, 0, 0x2000, 1, 0, 0);   // (appended past the count: header error first)
    expect(run(api, m, l3) == Api::BAD_HEADER, "validation precedes availability");
}

void test_sixteen() {
    Api api;
    Machine m;
    List l(16);
    for (uint8_t i = 0; i < 16; ++i) {
        l.fill(1, static_cast<uint8_t>(10 + i), 0x0200, 0xBE00, i, 0);
    }
    expect(l.frame().size() == 12 + 8 + 256, "maximum request is 276 bytes");
    expect(run(api, m, l) == Api::OK, "16 full-bank fills");
    bool all = true;
    for (uint8_t i = 0; i < 16; ++i) {
        const uint8_t *b = m.bank(10 + i);
        all = all && b[0x0200] == i && b[0xBFFF] == i;
    }
    expect(all, "each of the 16 fills reached its bank");
    expect(api.last_completed_descriptors() == 16 &&
           api.last_completed_bytes() == 16u * 0xBE00, "16 descriptors counted");
}

}  // namespace

int main() {
    test_status();
    test_copy_and_fill();
    test_ordered_and_banks();
    test_refusals();
    test_frames_and_ramworks();
    test_sixteen();
    if (failures == 0) std::printf("appletiniamemtest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
