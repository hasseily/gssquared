#include <cstdio>

#include "devices/pdblock3/AppletiniSpeedControl.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

void test_fixed_fast_speed() {
    AppletiniSpeedControl speed;

    AppletiniSpeedTransition transition = speed.write(
        0x01, CLOCK_33_3MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_1_024MHZ,
           "$01 selects 1 MHz");
    expect(speed.slow_locked(), "$01 records the slow lock");

    transition = speed.write(0x01, CLOCK_1_024MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_1_024MHZ,
           "repeated $01 remains at 1 MHz");

    transition = speed.write(0x00, CLOCK_1_024MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_33_3MHZ,
           "$00 restores Appletini MAX speed");
    expect(!transition.restore_cpu_per_14m,
           "fixed speeds do not restore a ludicrous multiplier");
    expect(!speed.slow_locked(), "$00 clears the slow lock");

    transition = speed.write(0x00, CLOCK_33_3MHZ, 1);
    expect(!transition.apply, "$00 is inert when no slow lock is active");
}

void tick(NClock &clock, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) clock.incr_cycles();
}

void test_fixed_33_ntsc_cadence() {
    constexpr uint64_t target_cycles_per_frame = 556272;
    constexpr uint64_t base_ticks_per_frame = 238420;

    NClockII clock(CLOCK_SET_US, CLOCK_33_3MHZ);
    expect(clock.get_hz_rate() == 33'333'333,
           "MAX reports the canonical 33.333333 MHz rate");
    expect(clock.get_cycles_per_frame() == target_cycles_per_frame,
           "MAX reports the rounded NTSC cycles per frame");

    tick(clock, target_cycles_per_frame);
    expect(clock.get_cycles() == target_cycles_per_frame,
           "every MAX tick advances the CPU cycle counter");
    expect(clock.get_c14m() == base_ticks_per_frame,
           "one NTSC frame advances exactly one frame of base 14M ticks");

    // The exact phase is intentionally retained across frames.  Five rounded
    // frames accumulate enough remainder that the sixth needs one fewer CPU
    // cycle while still producing a complete frame of base-clock ticks.
    tick(clock, target_cycles_per_frame * 4);
    expect(clock.get_c14m() == base_ticks_per_frame * 5,
           "MAX cadence remains aligned after five NTSC frames");
    tick(clock, target_cycles_per_frame - 1);
    expect(clock.get_c14m() == base_ticks_per_frame * 6,
           "MAX rational phase avoids rounded-frame drift");

    clock.set_clock_mode(CLOCK_14_3MHZ);
    clock.set_clock_mode(CLOCK_33_3MHZ);
    const uint64_t reset_c14m = clock.get_c14m();
    tick(clock, target_cycles_per_frame - 1);
    expect(clock.get_c14m() == reset_c14m + base_ticks_per_frame - 1,
           "changing modes resets the MAX rational phase");
    tick(clock, 1);
    expect(clock.get_c14m() == reset_c14m + base_ticks_per_frame,
           "a reset MAX phase completes on the rounded frame boundary");
}

void test_fixed_33_pal_cadence() {
    NClockII clock(CLOCK_SET_PAL, CLOCK_33_3MHZ);
    expect(clock.get_cycles_per_frame() == 665579,
           "MAX reports the rounded PAL cycles per frame");
    tick(clock, 665579);
    expect(clock.get_c14m() == 283920,
           "one PAL frame advances exactly one frame of base 14M ticks");
}

void test_iigs_sync_bypasses_fixed_33() {
    NClockIIgs clock(CLOCK_SET_US, CLOCK_33_3MHZ);

    // Two fast CPU cycles are not yet enough to emit a base-clock tick.
    tick(clock, 2);
    expect(clock.get_c14m() == 0,
           "IIgs fast cycles use the MAX rational gate");

    clock.set_next_cycle_type(CYCLE_TYPE_SYNC);
    tick(clock, 1);
    expect(clock.get_c14m() == 14,
           "IIgs SYNC cycles bypass MAX and execute at 1 MHz");

    // SYNC clears the partial fast phase, just like it clears cpu_div in
    // ludicrous mode, so the next fast cycle cannot inherit stale progress.
    clock.set_next_cycle_type(CYCLE_TYPE_FAST);
    tick(clock, 1);
    expect(clock.get_c14m() == 14,
           "IIgs SYNC cycles reset the MAX rational phase");
}

void test_fixed_33_toggle_order() {
    NClockII clock(CLOCK_SET_US, CLOCK_14_3MHZ);
    expect(clock.toggle(1) == CLOCK_33_3MHZ,
           "F9 advances from 14.3 MHz to 33.3 MHz");

    clock.set_clock_mode(CLOCK_33_3MHZ);
    expect(clock.toggle(1) == CLOCK_FREE_RUN,
           "F9 advances from 33.3 MHz to ludicrous speed");

    clock.set_clock_mode(CLOCK_FREE_RUN);
    expect(clock.toggle(-1) == CLOCK_33_3MHZ,
           "Shift-F9 returns from ludicrous speed to 33.3 MHz");
}

void test_ludicrous_speed() {
    AppletiniSpeedControl speed;

    speed.write(0x01, CLOCK_FREE_RUN, 23);
    speed.write(0x01, CLOCK_1_024MHZ, 1);
    const AppletiniSpeedTransition transition = speed.write(
        0x00, CLOCK_1_024MHZ, 1);

    expect(transition.apply && transition.mode == CLOCK_FREE_RUN,
           "$00 restores unlimited speed");
    expect(transition.restore_cpu_per_14m && transition.cpu_per_14m == 23,
           "$00 restores the calibrated unlimited-speed multiplier");
}

void test_firmware_108_control() {
    AppletiniSpeedControl speed;
    auto result = speed.write(0xFE, CLOCK_7_159MHZ, 1);
    expect(result.apply && speed.slow_locked(), "low two bits decode state 2 as native speed");
    result = speed.write(0xFC, CLOCK_1_024MHZ, 1);
    expect(result.apply && result.mode == CLOCK_7_159MHZ, "masked zero restores speed");
    result = speed.write(0xFF, CLOCK_FREE_RUN, 9);
    expect(result.apply && speed.off_until_reset(), "masked state 3 latches off until reset");
    expect(!speed.write(0, CLOCK_1_024MHZ, 1).apply, "zero cannot release state 3");
    result = speed.reset();
    expect(result.apply && result.mode == CLOCK_FREE_RUN && result.cpu_per_14m == 9,
        "Apple reset restores speed and unlimited multiplier");
    expect(!speed.off_until_reset() && !speed.slow_locked(), "reset clears both latches");
    speed.configure(false, false);
    expect(!speed.write(1, CLOCK_7_159MHZ, 1).apply, "disabled accelerator leaves host timing alone");
    speed.configure(true, true);
    expect(!speed.write(3, CLOCK_7_159MHZ, 1).apply, "ignore switch bypasses software override");
    speed.configure(true, false);
    expect(speed.write(1, CLOCK_7_159MHZ, 1).apply, "enabled override can slow again");
    expect(speed.reset().mode == CLOCK_7_159MHZ, "ordinary slow latch clears on reset");
}

} // namespace

int main() {
    test_fixed_fast_speed();
    test_fixed_33_ntsc_cadence();
    test_fixed_33_pal_cadence();
    test_iigs_sync_bypasses_fixed_33();
    test_fixed_33_toggle_order();
    test_ludicrous_speed();
    test_firmware_108_control();

    if (failures != 0) {
        std::fprintf(stderr, "%d Appletini speed tests failed\n", failures);
        return 1;
    }

    std::puts("All Appletini speed tests passed");
    return 0;
}
