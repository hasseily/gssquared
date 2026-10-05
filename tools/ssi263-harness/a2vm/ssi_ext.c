/* SSI-263 capture extensions of this scratch copy of a2vm (ssi_ext.h). */
#include "a2vm.h"
#include "ssi_ext.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* rtl/rtlcard_shim.cpp of the harness */
void *rtlcard_new(int fabric);
unsigned rtlcard_cycle(void *h, int has_access, unsigned addr, int rw,
                       unsigned data, int res_low);
int rtlcard_irq(void *h);
int rtlcard_d7(void *h, int chip);
void rtlcard_free(void *h);

enum { BD_ENTRY = 0xd0, SCREEN_POLL = 20000, KEY_GAP = 40000 };

static ssi_ext *X(a2vm *m)
{
    if (!m->ext) {
        m->ext = calloc(1, sizeof(ssi_ext));
        if (!m->ext) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    return m->ext;
}

/* ---- the slot-7 block device ---- */

static const uint8_t bd_boot[] = {
    0xa2, 0x20,             /* C700 LDX #$20   ($C701 = $20) */
    0xa0, 0x00,             /* C702 LDY #$00   ($C703 = $00) */
    0xa2, 0x03,             /* C704 LDX #$03   ($C705 = $03) */
    0xa2, 0x3c,             /* C706 LDX #$3C   ($C707 = $3C) */
    0xa9, 0x01, 0x85, 0x42, /* READ */
    0xa9, 0x70, 0x85, 0x43, /* unit: slot 7, drive 1 */
    0xa9, 0x00, 0x85, 0x44, 0x85, 0x46, 0x85, 0x47,
    0xa9, 0x08, 0x85, 0x45, /* buffer $0800, block 0 */
    0x20, BD_ENTRY, 0xc7,   /* JSR the driver */
    0xb0, 0xfe,             /* BCS * */
    0xa2, 0x70,             /* LDX #$70 */
    0x4c, 0x01, 0x08        /* JMP $0801 */
};

static uint8_t bd_rom(unsigned offset)
{
    if (offset < sizeof bd_boot)
        return bd_boot[offset];
    if (offset == BD_ENTRY)
        return 0x60;        /* RTS; the trap runs before it */
    if (offset == 0xfe)
        return 0x07;        /* status, read, write; one volume */
    if (offset == 0xff)
        return BD_ENTRY;
    return 0x00;
}

void ssi_ext_blockdev(a2vm *m, const char *path)
{
    ssi_ext *x = X(m);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n % 512) {
        fprintf(stderr, "%s: not a whole number of blocks\n", path);
        exit(2);
    }
    x->bd_image = malloc((size_t)n);
    if (!x->bd_image || fread(x->bd_image, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    x->bd_blocks = (uint32_t)(n / 512);
}

static int bd_trap(a2vm *m)
{
    ssi_ext *x = m->ext;
    uint8_t cmd = a2vm_read(m, 0x42), unit = a2vm_read(m, 0x43);
    uint16_t buf = (uint16_t)(a2vm_read(m, 0x44) | a2vm_read(m, 0x45) << 8);
    uint32_t blk = (uint32_t)(a2vm_read(m, 0x46) | a2vm_read(m, 0x47) << 8);
    uint8_t err = 0;
    if ((unit & 0x80) || (unit & 0x70) != 0x70)
        err = 0x28;
    else if (cmd == 0) {
        x->bd_status++;
        m->cpu.x = (uint8_t)x->bd_blocks;
        m->cpu.y = (uint8_t)(x->bd_blocks >> 8);
    } else if (cmd == 1 || cmd == 2) {
        if (blk >= x->bd_blocks)
            err = 0x27;
        else if (cmd == 1) {
            x->bd_reads++;
            for (unsigned i = 0; i < 512; i++)
                a2vm_write(m, (uint16_t)(buf + i), x->bd_image[blk * 512 + i]);
        } else {
            x->bd_writes++;
            for (unsigned i = 0; i < 512; i++)
                x->bd_image[blk * 512 + i] = a2vm_read(m, (uint16_t)(buf + i));
        }
    } else if (cmd != 3)
        err = 0x01;
    m->cpu.a = err;
    uint8_t lo = a2vm_read(m, (uint16_t)(0x0100 + (uint8_t)(m->cpu.s + 1)));
    uint8_t hi = a2vm_read(m, (uint16_t)(0x0100 + (uint8_t)(m->cpu.s + 2)));
    m->cpu.s = (uint8_t)(m->cpu.s + 2);
    m->cpu.pc = (uint16_t)((lo | hi << 8) + 1);
    if (err)
        m->cpu.p |= CPU65C02_C;
    else
        m->cpu.p &= (uint8_t)~CPU65C02_C;
    if (err)
        m->cpu.p &= (uint8_t)~CPU65C02_Z;
    else
        m->cpu.p |= CPU65C02_Z;
    m->cpu.cycles += 200;
    return 1;
}

/* ---- the RTL card ---- */

void ssi_ext_phasor_rtl(a2vm *m, int fabric)
{
    ssi_ext *x = X(m);
    x->rtl = rtlcard_new(fabric);
    x->rtl_next = 0;
}

void ssi_ext_phasor_log(a2vm *m, const char *path)
{
    ssi_ext *x = X(m);
    x->phasor_log = fopen(path, "w");
    if (!x->phasor_log) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fprintf(x->phasor_log, "# a2vm phasor-log: CYCLE R|W ADDR VALUE (Apple cycles "
            "from power-on; reads with the value the RTL card returned)\n");
}

static void rtl_sync(a2vm *m, uint64_t upto)
{
    ssi_ext *x = m->ext;
    while (x->rtl_next < upto) {
        rtlcard_cycle(x->rtl, 0, 0, 1, 0, 0);
        x->rtl_next++;
    }
}

static uint8_t rtl_access(a2vm *m, uint16_t address, int rw, uint8_t value)
{
    ssi_ext *x = m->ext;
    /* the core counts the cycle before its callback: this access is cycle
       cycles - 1, counting from 0 */
    uint64_t at = m->cpu.cycles ? m->cpu.cycles - 1 : 0;
    rtl_sync(m, at);
    if (x->rtl_next > at) {
        /* a trap moved the clock back? cannot happen; log it */
        fprintf(stderr, "rtl: access at %" PRIu64 " behind the card (%" PRIu64 ")\n",
                at, x->rtl_next);
        at = x->rtl_next;
    }
    uint8_t r = (uint8_t)rtlcard_cycle(x->rtl, 1, address, rw, value, 0);
    x->rtl_next = at + 1;
    if (x->phasor_log) {
        fprintf(x->phasor_log, "%" PRIu64 " %c %04X %02X\n", at, rw ? 'R' : 'W',
                address, rw ? r : value);
        x->phasor_logged++;
    }
    return r;
}

int ssi_ext_slot_read(a2vm *m, uint16_t address, uint8_t *value)
{
    ssi_ext *x = m->ext;
    if (!x || m->sw[SW_INTCXROM])
        return 0;
    int slot = (address >> 8) & 7;
    if (x->bd_image && slot == 7 && address < 0xc800) {
        *value = bd_rom(address & 0xff);
        return 1;
    }
    if (x->rtl && slot == m->phasor_slot && address < 0xc800) {
        *value = rtl_access(m, address, 1, 0);
        return 1;
    }
    return 0;
}

int ssi_ext_slot_write(a2vm *m, uint16_t address, uint8_t value)
{
    ssi_ext *x = m->ext;
    if (!x || !x->rtl || m->sw[SW_INTCXROM])
        return 0;
    if (((address >> 8) & 7) == m->phasor_slot && address < 0xc800) {
        rtl_access(m, address, 0, value);
        return 1;
    }
    return 0;
}

int ssi_ext_c0_access(a2vm *m, uint16_t address, int rw, uint8_t value)
{
    ssi_ext *x = m->ext;
    if (!x || !x->rtl)
        return 0;
    if ((address & 0xfff0) == (uint16_t)(0xc080 + 16 * m->phasor_slot)) {
        rtl_access(m, address, rw, value);
        return 1;
    }
    return 0;
}

int ssi_ext_step(a2vm *m)
{
    ssi_ext *x = m->ext;
    if (!x)
        return 0;
    {
        /* SSI_TRACE=FROM:COUNT: the instructions from cycle FROM to stderr */
        static int init = 0;
        static uint64_t from = UINT64_MAX, left = 0;
        if (!init) {
            init = 1;
            const char *t = getenv("SSI_TRACE");
            if (t) {
                from = strtoull(t, NULL, 10);
                const char *c = strchr(t, ':');
                left = c ? strtoull(c + 1, NULL, 10) : 20000;
            }
        }
        if (left && m->cpu.cycles >= from) {
            left--;
            fprintf(stderr, "%" PRIu64 " %04X A=%02X X=%02X Y=%02X S=%02X P=%02X %02X %02X %02X\n",
                    m->cpu.cycles, m->cpu.pc, m->cpu.a, m->cpu.x, m->cpu.y, m->cpu.s, m->cpu.p,
                    a2vm_read(m, m->cpu.pc), a2vm_read(m, (uint16_t)(m->cpu.pc + 1)),
                    a2vm_read(m, (uint16_t)(m->cpu.pc + 2)));
        }
    }
    if (x->rtl)
        rtl_sync(m, m->cpu.cycles);
    if (x->bd_image && m->cpu.pc == (0xc700 | BD_ENTRY) && !m->sw[SW_INTCXROM] &&
        m->cpu.state == CPU65C02_RUNNING)
        return bd_trap(m);
    return 0;
}

int ssi_ext_irq(a2vm *m)
{
    ssi_ext *x = m->ext;
    return x && x->rtl && rtlcard_irq(x->rtl);
}

/* ---- the text screen ---- */

static char text_char(uint8_t c)
{
    unsigned ch;
    if (c >= 0xa0)
        ch = c - 0x80u;
    else if (c >= 0x80)
        ch = c - 0x40u;
    else {
        unsigned v = c & 0x3fu;
        ch = v < 0x20 ? v + 0x40u : v;
    }
    if (ch < 0x20 || ch > 0x7e)
        ch = '.';
    /* inverse and flashing ($00-$7F) shown in lowercase, spaces as '_' */
    if (c < 0x80) {
        if (ch >= 'A' && ch <= 'Z')
            ch += 0x20;
        else if (ch == ' ')
            ch = '_';
    }
    return (char)ch;
}

void ssi_ext_text(a2vm *m, char out[24 * 81 + 8])
{
    int col80 = m->sw[SW_COL80];
    unsigned base = (m->sw[SW_PAGE2] && !m->sw[SW_STORE80]) ? 0x800 : 0x400;
    char *p = out;
    for (unsigned row = 0; row < 24; row++) {
        unsigned a = base + (row % 8) * 0x80 + (row / 8) * 0x28;
        for (unsigned col = 0; col < 40; col++) {
            if (col80)
                *p++ = text_char(*a2vm_storage(m, 1, 0, (uint16_t)(a + col)));
            *p++ = text_char(m->main[a + col]);
        }
        *p++ = '\n';
    }
    *p = 0;
}

void ssi_ext_screen_log(a2vm *m, const char *path)
{
    ssi_ext *x = X(m);
    x->screen_log = fopen(path, "w");
    if (!x->screen_log) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
}

static void unescape(const char *in, char *out, size_t size)
{
    size_t n = 0;
    while (*in && n + 1 < size) {
        if (in[0] == '\\' && in[1]) {
            in++;
            if (*in == 'r') out[n++] = '\r';
            else if (*in == 'e') out[n++] = 0x1b;
            else if (*in == 't') out[n++] = '\t';
            else if (*in == 'x' && in[1] && in[2]) {
                char hex[3] = { in[1], in[2], 0 };
                out[n++] = (char)strtoul(hex, NULL, 16);
                in += 2;
            } else out[n++] = *in;
            in++;
        } else
            out[n++] = *in++;
    }
    out[n] = 0;
}

void ssi_ext_expect(a2vm *m, const char *path)
{
    ssi_ext *x = X(m);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    char line[512];
    unsigned cap = 0;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len || line[0] == '#')
            continue;
        char *tab = strchr(line, '\t');
        if (!tab) {
            fprintf(stderr, "%s: no tab in: %s\n", path, line);
            exit(2);
        }
        *tab = 0;
        if (x->expect_count == cap) {
            cap = cap ? cap * 2 : 32;
            x->expect_pat = realloc(x->expect_pat, cap * sizeof *x->expect_pat);
            x->expect_keys = realloc(x->expect_keys, cap * sizeof *x->expect_keys);
        }
        snprintf(x->expect_pat[x->expect_count], 128, "%s", line);
        if (!strcmp(tab + 1, "@stop"))
            snprintf(x->expect_keys[x->expect_count], 256, "%s", "\x01STOP");
        else if (!strcmp(tab + 1, "@reset"))
            snprintf(x->expect_keys[x->expect_count], 256, "%s", "\x01RESET");
        else
            unescape(tab + 1, x->expect_keys[x->expect_count], 256);
        x->expect_count++;
    }
    fclose(f);
}

/* CTRL-RESET: RES low for `n` cycles (the CPU held, the card in reset),
   the //e MMU's switches back to their reset state, then the 65C02's RESET
   sequence through $FFFC. */
static void ctrl_reset(a2vm *m, unsigned n)
{
    ssi_ext *x = m->ext;
    if (x->rtl) {
        rtl_sync(m, m->cpu.cycles);
        for (unsigned i = 0; i < n; i++) {
            rtlcard_cycle(x->rtl, 0, 0, 1, 0, 1);
            x->rtl_next++;
        }
        if (x->phasor_log)
            fprintf(x->phasor_log, "%" PRIu64 " RES %u\n", m->cpu.cycles, n);
    }
    m->cpu.cycles += n;
    for (int i = SW_STORE80; i <= SW_ALTCHAR; i++)
        m->sw[i] = 0;
    m->lc_read = 0;
    m->lc_write = 0;
    m->lc_prewrite = 0;
    m->lc_bank2 = 1;
    a2vm_select_bank(m, 0);
    a2vm_remap(m);
    m->key_latch = 0;
    a2vm_cpu_reset(m);
}

void ssi_ext_poll(a2vm *m)
{
    ssi_ext *x = m->ext;
    if (!x || (!x->screen_log && !x->expect_count))
        return;
    uint64_t now = a2vm_now(m);
    if (now < x->next_screen)
        return;
    x->next_screen = now + SCREEN_POLL;
    char text[24 * 81 + 8];
    ssi_ext_text(m, text);
    if (x->screen_log && strcmp(text, x->last_screen)) {
        fprintf(x->screen_log, "=== cycle %" PRIu64 "\n%s", now, text);
        fflush(x->screen_log);
        memcpy(x->last_screen, text, sizeof text);
    }
    if (x->expect_at < x->expect_count && m->key_count == 0) {
        const char *pat = x->expect_pat[x->expect_at];
        int ready;
        if (pat[0] == '@')
            ready = now >= x->expect_since + strtoull(pat + 1, NULL, 10);
        else
            ready = strstr(text, pat) != NULL;
        if (ready) {
            const char *keys = x->expect_keys[x->expect_at];
            if (!strcmp(keys, "\x01STOP")) {
                x->stop = 1;
                return;
            }
            if (!strcmp(keys, "\x01RESET")) {
                if (x->screen_log)
                    fprintf(x->screen_log, "=== cycle %" PRIu64 " expect %u matched \"%s\", CTRL-RESET\n",
                            now, x->expect_at, pat);
                ctrl_reset(m, 50);
                x->expect_at++;
                x->expect_since = a2vm_now(m);
                return;
            }
            uint64_t t = now;
            for (const char *k = keys; *k; k++) {
                a2vm_press(m, (uint8_t)*k, t);
                t += KEY_GAP;
            }
            if (x->screen_log)
                fprintf(x->screen_log, "=== cycle %" PRIu64 " expect %u matched \"%s\", typing %zu keys\n",
                        now, x->expect_at, pat, strlen(keys));
            x->expect_at++;
            x->expect_since = t;
        }
    }
}

void ssi_ext_finish(a2vm *m)
{
    ssi_ext *x = m->ext;
    if (!x)
        return;
    if (x->phasor_log) {
        fprintf(x->phasor_log, "%" PRIu64 " END\n", m->cpu.cycles);
        fclose(x->phasor_log);
    }
    if (x->screen_log) {
        char text[24 * 81 + 8];
        ssi_ext_text(m, text);
        fprintf(x->screen_log, "=== final cycle %" PRIu64 " (blocks read %" PRIu64
                ", written %" PRIu64 ", card accesses %" PRIu64 ")\n%s",
                m->cpu.cycles, x->bd_reads, x->bd_writes, x->phasor_logged, text);
        fclose(x->screen_log);
    }
    if (x->rtl)
        rtlcard_free(x->rtl);
}
