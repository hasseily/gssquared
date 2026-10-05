#!/usr/bin/env python3
"""Write the synthetic cases of the suite into suite/ (closed-loop scripts
for common/driver.hpp). Each case is a register program with a 6502's
timing: 6-cycle writes, 7-cycle D7 polls, 9-cycle IFR polls, IRQ waits with
the 7-cycle entry. Re-run after editing; it rewrites suite/syn_*.txt only.
"""
from pathlib import Path

H = Path(__file__).resolve().parent
OUT = H / 'suite'

HELLO = [0x6C, 0x4B, 0x60, 0x11, 0x00]          # HF EH1 L O PA ("hello")
WORDS = [0x2A, 0x4B, 0x34, 0x00, 0x3D, 0x1F, 0x3A, 0x00]   # varied phones


class S:
    def __init__(self, title):
        self.l = [f'# {title}']

    def add(self, *lines):
        self.l.extend(lines)
        return self

    def ssi(self, reg, val, sock='P'):
        return self.add(f'ssi {sock} {reg} {val:02X}')

    def setup(self, sock='P', dr=3, infl=0x52, rate=0xA8, ff=0xE6, art=7, amp=0xF,
              first=0x00, mode='phasor'):
        if mode:
            self.add(f'mode {mode}')
        self.ssi(3, 0x80, sock)
        self.ssi(1, infl, sock)
        self.ssi(2, rate, sock)
        self.ssi(4, ff, sock)
        self.ssi(0, (dr << 6) | first, sock)
        self.ssi(3, (art << 4) | amp, sock)
        return self

    def phone(self, ph, d=None, sock='P', wait='d7', dr_bits=None):
        if wait == 'd7':
            self.add(f'waitd7 {sock}')
        elif wait == 'ifr':
            self.add(f'waitifr {sock}')
        elif wait == 'irq':
            self.add('waitirq')
        elif isinstance(wait, int):
            self.add(f'samples {wait}')
        top = (d if d is not None else 3) << 6
        return self.ssi(0, top | ph, sock)

    def stop(self, sock='P', wait='d7', tail=1200):
        if wait == 'd7':
            self.add(f'waitd7 {sock}')
        elif wait == 'ifr':
            self.add(f'waitifr {sock}')
        elif wait == 'irq':
            self.add('waitirq')
        elif isinstance(wait, int):
            self.add(f'samples {wait}')
        self.ssi(3, 0x80, sock)
        return self.add(f'samples {tail}', 'end')

    def write(self, name):
        (OUT / f'syn_{name}.txt').write_text('\n'.join(self.l) + '\n')


def main():
    OUT.mkdir(exist_ok=True)
    for p in OUT.glob('syn_*.txt'):
        p.unlink()

    # 1. Every phoneme: PA, the phone at D=0 then D=2, PA, stop (mode 3).
    for ph in range(64):
        s = S(f'phoneme {ph:02X}: PA, {ph:02X} D=0, {ph:02X} D=2, PA; DR=11, R=A, I=52/8')
        s.setup(dr=3, first=0x00)
        s.phone(ph, d=0).phone(ph, d=2).phone(0x00, d=3)
        s.stop()
        s.write(f'phone_{ph:02X}')

    # 2. Duration and rate.
    for d in range(4):
        for r in (0x0, 0x8, 0xF):
            s = S(f'"hello" at D={d} R={r:X} (DR=11)')
            s.setup(dr=3, rate=(r << 4) | 0x8)
            for ph in HELLO:
                s.phone(ph, d=d)
            s.stop()
            s.write(f'dur{d}_rate{r:X}')

    # 3. Duration/response modes, polled in native mode.
    for dr in (1, 2, 3):
        s = S(f'mode DR={dr}: words, polled D7')
        s.setup(dr=dr)
        for ph in WORDS:
            s.phone(ph, d=1)
        s.stop()
        s.write(f'mode_DR{dr}')
    s = S('mode DR=00 (A/R masked): phones on a sample clock, D7 never polled')
    s.setup(dr=0, first=0x00)
    for ph in WORDS:
        s.phone(ph, d=1, wait=2000)
    s.stop(wait=2000)
    s.write('mode_DR0')
    # DR=01 responds every frame: a new phone each response.
    s = S('mode DR=01: a phone on every frame response')
    s.setup(dr=1, rate=0x88)
    for ph in WORDS * 2:
        s.phone(ph, d=0)
    s.stop()
    s.write('mode_DR1_frames')

    # 4. Inflection.
    for infl, rate in ((0x00, 0xA0), (0x40, 0xA0), (0x80, 0xA0), (0xA8, 0xA8), (0xFF, 0xAF), (0x00, 0xA8)):
        s = S(f'inflection INFLECT={infl:02X} RATE={rate:02X} (DR=10)')
        s.setup(dr=2, infl=infl, rate=rate)
        for ph in (0x0A, 0x11, 0x20):
            s.phone(ph, d=0)
        s.stop()
        s.write(f'infl_{infl:02X}_{rate:02X}')
    for slope in (0, 3, 7):
        s = S(f'transitioned inflection (DR=11), slope {slope}: target moves each phone')
        s.setup(dr=3, infl=(0x04 << 3) | slope, rate=0xA0)
        for i, ph in enumerate((0x0A, 0x0A, 0x11, 0x11, 0x20, 0x20)):
            s.add('waitd7 P')
            # I[10:6] = INFLECT[7:3] is the glide target, I[5:3] =
            # INFLECT[2:0] the slope.
            s.ssi(1, (((0x04 + 5 * i) & 0x1F) << 3) | slope)
            s.ssi(2, 0xA0 | (i & 7))
            s.ssi(0, ph)
        s.stop()
        s.write(f'infl_glide_s{slope}')
    s = S('inflection changes mid-phone (DR=10 and DR=01)')
    s.setup(dr=2, infl=0x40, rate=0xA0)
    s.phone(0x0A, d=0)
    for v in (0x60, 0x80, 0xA0, 0xC0, 0x30):
        s.add('samples 300').ssi(1, v)
    s.stop()
    s.write('infl_midphone')

    # 5. Filter frequency (no audio effect expected on either side).
    for ff in (0x00, 0x80, 0xE6, 0xFF):
        s = S(f'filter frequency {ff:02X}')
        s.setup(ff=ff)
        for ph in HELLO:
            s.phone(ph, d=2)
        s.add('samples 300').ssi(4, ff ^ 0x55)
        s.stop()
        s.write(f'filfreq_{ff:02X}')

    # 6. Amplitude, including live changes.
    for amp in (0x0, 0x1, 0x4, 0x8, 0xC, 0xF):
        s = S(f'amplitude {amp:X}')
        s.setup(amp=amp)
        for ph in HELLO:
            s.phone(ph, d=2)
        s.stop()
        s.write(f'amp_{amp:X}')
    s = S('amplitude stepped during a vowel, then muted with A=0 and restored')
    s.setup(amp=0xF)
    s.phone(0x0A, d=0)
    for a in (0xC, 0x8, 0x4, 0x1, 0x0, 0x0, 0xF):
        s.add('samples 250').ssi(3, 0x70 | a)
    s.stop()
    s.write('amp_live')

    # 7. Articulation.
    for art in range(8):
        s = S(f'articulation {art}')
        s.setup(art=art)
        for ph in WORDS:
            s.phone(ph, d=2)
        s.stop()
        s.write(f'art_{art}')

    # 8. The control bit.
    s = S('CTL raised mid-phone, then lowered: the phone restarts')
    s.setup()
    s.phone(0x0A, d=0)
    s.add('samples 600').ssi(3, 0xF0 | 0xF).add('samples 600').ssi(3, 0x7F)
    s.phone(0x11, d=1)
    s.stop()
    s.write('ctl_toggle')
    s = S('phone written while CTL=1, started by lowering CTL; DR changed at the edge')
    s.setup(dr=3)
    s.phone(0x0A, d=1)
    s.add('waitd7 P').ssi(3, 0x80).add('samples 400')
    s.ssi(0, 0x80 | 0x11)            # DR=10 in the top bits, phone O
    s.add('samples 400').ssi(3, 0x7F)
    s.phone(0x20, d=2)
    s.stop()
    s.write('ctl_phone_while_high')
    s = S('long CTL=1 pause between words (formant state while powered down)')
    s.setup()
    for ph in (0x2A, 0x4B, 0x34):
        s.phone(ph, d=2)
    s.add('waitd7 P').ssi(3, 0x80).add('samples 4000')
    s.ssi(0, 0xC0 | 0x3D).ssi(3, 0x7F)
    for ph in (0x1F, 0x3A):
        s.phone(ph, d=2)
    s.stop()
    s.write('ctl_long_pause')
    s = S('CTL=1 with DR=00 bits then lowered: interrupts disabled, mode kept')
    s.setup(dr=3)
    s.phone(0x0A, d=2)
    s.add('waitd7 P').ssi(3, 0x80).ssi(0, 0x11).ssi(3, 0x7F)
    s.add('samples 3000')
    s.ssi(0, 0x40 | 0x20)
    s.add('samples 3000')
    s.stop(wait=None)
    s.write('ctl_dr00')

    # 9. Power-down (Apple RESET) of the AP part, then resume by lowering CTL.
    s = S('Apple RESET mid-phone: CTL forced high, registers kept; resume with CTL low')
    s.setup()
    s.phone(0x0A, d=0)
    s.add('samples 500', 'reset 20', 'samples 800', 'mode phasor')
    s.ssi(3, 0x7F)
    s.phone(0x11, d=2)
    s.stop()
    s.write('powerdown_reset')

    # 10. Mockingboard mode: IRQ through VIA-B CA1, the ISR acknowledges by
    # writing the next phone, clears IFR and returns.
    s = S('Mockingboard mode, IRQ on VIA-B CA1 ($C48E=82), ISR writes next phone')
    s.add('mode mb', 'w C48C 00', 'w C48E 82')
    s.setup(mode=None, dr=3)
    for ph in HELLO:
        s.add('waitirq', 'r C48D').ssi(0, 0xC0 | ph).add('w C48D 02')
    s.add('waitirq', 'r C48D').ssi(3, 0x80).add('w C48D 02', 'samples 1200', 'end')
    s.write('mb_irq_hello')
    s = S('Mockingboard mode, polled IFR CA1, PCR=00')
    s.add('mode mb', 'w C48C 00')
    s.setup(mode=None, dr=3)
    for ph in WORDS:
        s.phone(ph, d=1, wait='ifr')
        s.add('w C48D 02')
    s.stop(wait='ifr')
    s.write('mb_poll_ifr')
    s = S('Mockingboard mode, PCR bit 0 = 1 (positive CA1 edge): no IFR from A/R')
    s.add('mode mb', 'w C48C 01', 'w C48E 82')
    s.setup(mode=None, dr=3)
    for ph in (0x0A, 0x11):
        s.phone(ph, d=2, wait=3000)
        s.add('r C48D')
    s.stop(wait=3000)
    s.write('mb_pcr_pos')
    s = S('Mockingboard mode, secondary socket ($C420) on VIA-A CA1')
    s.add('mode mb', 'w C40C 00', 'w C40E 82')
    s.setup(mode=None, sock='S', dr=3)
    for ph in HELLO:
        s.add('waitirq', 'r C40D').ssi(0, 0xC0 | ph, 'S').add('w C40D 02')
    s.add('waitirq').ssi(3, 0x80, 'S').add('w C40D 02', 'samples 1200', 'end')
    s.write('mb_irq_secondary')

    # 11. Native direct IRQ.
    s = S('Phasor native mode, direct IRQ, ISR writes the next phone (acknowledge)')
    s.setup(dr=3)
    for ph in HELLO + WORDS:
        s.phone(ph, d=2, wait='irq')
    s.stop(wait='irq')
    s.write('native_irq')

    # 12. Both sockets.
    s = S('both sockets: secondary and primary speak interleaved, polled')
    s.setup(sock='P', dr=3).setup(sock='S', dr=3, infl=0x80, mode=None)
    for a, b in zip(HELLO, WORDS):
        s.phone(a, d=2, sock='P').phone(b, d=2, sock='S')
    s.add('waitd7 P').ssi(3, 0x80, 'P').add('waitd7 S').ssi(3, 0x80, 'S')
    s.add('samples 1200', 'end')
    s.write('both_sockets')
    s = S('broadcast writes ($C460-$C467 select both sockets)')
    s.add('mode phasor', 'w C463 80', 'w C461 52', 'w C462 A8', 'w C460 C0', 'w C463 7F')
    for ph in HELLO:
        s.add('waitd7 P', f'w C460 {0x80 | ph:02X}')
    s.add('waitd7 P', 'w C463 80', 'samples 1200', 'end')
    s.write('broadcast')

    # 13. Live rate changes.
    s = S('RATE changed mid-phone (applies at the next slot)')
    s.setup(rate=0xA8)
    s.phone(0x0A, d=0)
    for r in (0x48, 0xF8, 0x08, 0xA8):
        s.add('samples 700').ssi(2, r)
    s.phone(0x11, d=1)
    s.stop()
    s.write('rate_live')

    # 14. Mode switch with a pending request.
    s = S('A/R pending across mode switches (native -> MB -> native)')
    s.add('mode mb', 'w C48C 00', 'w C48E 82', 'mode phasor')
    s.setup(mode=None, dr=3)
    s.phone(0x0A, d=3)
    s.add('samples 2000', 'mode mb', 'wait 50', 'r C48D', 'mode phasor', 'wait 50',
          'r C440', 'ssi P 0 C0', 'waitd7 P', 'ssi P 3 80', 'samples 600', 'end')
    s.write('mode_switch_pending')

    # 15. Mockingboard-mode SSI writes alias VIA-A registers ($C440-$C447).
    s = S('Mockingboard mode "hello" with D7 polled via IFR (VIA-A aliasing)')
    s.add('mode mb', 'w C48C 00')
    s.setup(mode=None, dr=3)
    for ph in HELLO:
        s.phone(ph, d=2, wait='ifr')
        s.add('w C48D 02')
    s.stop(wait='ifr')
    s.write('mb_hello')

    print(f'wrote {len(list(OUT.glob("syn_*.txt")))} cases into {OUT}')


if __name__ == '__main__':
    main()
