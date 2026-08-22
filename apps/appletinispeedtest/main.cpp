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
        0x01, CLOCK_14_3MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_1_024MHZ,
           "$01 selects 1 MHz");
    expect(speed.slow_locked(), "$01 records the slow lock");

    transition = speed.write(0x01, CLOCK_1_024MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_1_024MHZ,
           "repeated $01 remains at 1 MHz");

    transition = speed.write(0x00, CLOCK_1_024MHZ, 1);
    expect(transition.apply && transition.mode == CLOCK_14_3MHZ,
           "$00 restores the original fixed fast speed");
    expect(!transition.restore_cpu_per_14m,
           "fixed speeds do not restore a ludicrous multiplier");
    expect(!speed.slow_locked(), "$00 clears the slow lock");

    transition = speed.write(0x00, CLOCK_14_3MHZ, 1);
    expect(!transition.apply, "$00 is inert when no slow lock is active");
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

void test_unhandled_value() {
    AppletiniSpeedControl speed;
    const AppletiniSpeedTransition transition = speed.write(
        0x02, CLOCK_7_159MHZ, 1);
    expect(!transition.apply && !speed.slow_locked(),
           "unsupported $C074 values do not change speed");
}

} // namespace

int main() {
    test_fixed_fast_speed();
    test_ludicrous_speed();
    test_unhandled_value();

    if (failures != 0) {
        std::fprintf(stderr, "%d Appletini speed tests failed\n", failures);
        return 1;
    }

    std::puts("All Appletini speed tests passed");
    return 0;
}
