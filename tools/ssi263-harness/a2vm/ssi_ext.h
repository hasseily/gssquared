/*
 * SSI-263 capture extensions of this scratch copy of a2vm (not in the
 * appletini-software tree):
 *
 *   --blockdev FILE      a ProDOS block device in slot 7 holding FILE (a
 *                        ProDOS-order image, read into memory; writes stay
 *                        in memory), with a boot ROM, so the //e ROM's
 *                        autostart boots it (--boot)
 *   --boot               start from the 65C02 RESET vector (the //e ROM)
 *   --phasor-rtl F       the Phasor in slot 4 is appletini-one's RTL
 *                        (hdl/apple/mockingboard.sv under Verilator, the
 *                        harness's tb_card), F fabric clocks an Apple cycle;
 *                        every Apple cycle runs it, its IRQ reaches the CPU
 *   --phasor-log FILE    every access to the card ($C0C0-$C0CF,
 *                        $C400-$C4FF) as "CYCLE R|W ADDR VALUE", the
 *                        replay format of the harness (common/driver.hpp)
 *   --screen-log FILE    the text screen whenever it changes (polled every
 *                        20,000 cycles)
 *   --expect FILE        screen-driven typing: lines "PATTERN<TAB>KEYS";
 *                        wait until PATTERN is on the text screen, then type
 *                        KEYS (\r return, \e escape, \xNN a byte); a line
 *                        "@N<TAB>KEYS" waits N cycles; "PATTERN<TAB>@stop"
 *                        ends the run; "#" starts a comment line
 */
#ifndef SSI_EXT_H
#define SSI_EXT_H

#include <stdint.h>
#include <stdio.h>

struct a2vm;

typedef struct ssi_ext {
    /* block device */
    uint8_t *bd_image;
    uint32_t bd_blocks;
    uint64_t bd_reads, bd_writes, bd_status;
    /* RTL card */
    void *rtl;
    uint64_t rtl_next;              /* the next Apple cycle the card runs */
    FILE *phasor_log;
    uint64_t phasor_logged;
    /* screen */
    FILE *screen_log;
    uint64_t next_screen;
    char last_screen[24 * 81 + 8];
    /* expect */
    char (*expect_pat)[128];
    char (*expect_keys)[256];
    unsigned expect_count, expect_at;
    uint64_t expect_since;
    int stop;
} ssi_ext;

void ssi_ext_blockdev(struct a2vm *m, const char *path);
void ssi_ext_phasor_rtl(struct a2vm *m, int fabric);
void ssi_ext_phasor_log(struct a2vm *m, const char *path);
void ssi_ext_screen_log(struct a2vm *m, const char *path);
void ssi_ext_expect(struct a2vm *m, const char *path);
void ssi_ext_finish(struct a2vm *m);

/* hooks a2vm.c calls */
int ssi_ext_slot_read(struct a2vm *m, uint16_t address, uint8_t *value);
int ssi_ext_slot_write(struct a2vm *m, uint16_t address, uint8_t value);
int ssi_ext_c0_access(struct a2vm *m, uint16_t address, int rw, uint8_t value);
int ssi_ext_step(struct a2vm *m);   /* 1: the step was a trap, done */
int ssi_ext_irq(struct a2vm *m);
void ssi_ext_poll(struct a2vm *m);  /* after each step */
void ssi_ext_text(struct a2vm *m, char out[24 * 81 + 8]);

#endif
