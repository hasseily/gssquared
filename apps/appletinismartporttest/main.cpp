#include "devices/pdblock3/AppletiniSmartPort.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
using SP = AppletiniSmartPort;
void expect(bool condition, const char* text) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
}
std::vector<uint8_t> transact(SP& sp, uint8_t family, std::vector<uint8_t> bytes, size_t count) {
    for (auto byte : bytes) sp.data_write(byte);
    sp.execute(family);
    expect(sp.control_read() == 0x80, "CTRL ready without private DMA bits");
    std::vector<uint8_t> result;
    for (size_t i = 0; i < count; ++i) {
        const auto b = sp.data_read();
        expect(sp.data_read() == b, "DATA read does not consume FIFO");
        result.push_back(b); sp.pop();
    }
    return result;
}
std::vector<uint8_t> request(uint8_t cmd, uint8_t unit, uint32_t block = 0) {
    return {cmd, 3, unit, 0, 0, static_cast<uint8_t>(block),
        static_cast<uint8_t>(block >> 8), static_cast<uint8_t>(block >> 16), 0, 0};
}
}

int main() {
    std::array<SP::Unit, 8> units{};
    std::array<std::array<uint8_t, 512>, 8> disks{};
    SP sp(7, [&](uint8_t u) { return units[u]; },
        [&](uint8_t u, uint32_t, uint8_t* dst) {
            std::memcpy(dst, disks[u].data(), 512); return SP::ok;
        }, [&](uint8_t u, uint32_t, const uint8_t* src) {
            std::memcpy(disks[u].data(), src, 512); return SP::ok;
        });

    auto dib = transact(sp, 2, request(0, 0, 3), 32);
    expect(dib[0] == 0 && dib[1] == 29 && dib[3] == 0, "empty controller GETDIB");
    expect(dib[11] == 12 && std::memcmp(dib.data() + 12, "Appletini SP", 12) == 0,
        "supported Appletini detection string");
    expect(dib[30] == 1 && dib[31] == 0, "F1.0.8 DIB version is 1,0");
    units[7] = {true, 1, false, true}; disks[7].fill(0xA8);
    expect(transact(sp, 2, request(0, 0), 11)[3] == 1,
        "sparse controller count is present devices, not highest unit");
    expect(transact(sp, 2, request(1, 1), 1)[0] == SP::no_device,
        "configured unit holes stay absent");
    expect(transact(sp, 2, request(1, 8), 513)[1] == 0xA8,
        "eighth unit remains addressable with earlier holes");
    for (uint8_t u = 0; u < 8; ++u) {
        units[u] = {true, 0x10001, u == 7, true}; disks[u].fill(u + 1);
    }
    expect(transact(sp, 2, request(0, 0), 11)[3] == 8, "all eight units enumerated");
    for (uint8_t u = 1; u <= 8; ++u) {
        const auto data = transact(sp, 2, request(1, u, 65536), 513);
        expect(data[0] == 0 && data[1] == u && data[512] == u, "24-bit block read on each unit");
    }
    expect(transact(sp, 1, {0, 0x70, 0, 0, 0, 0}, 3) == std::vector<uint8_t>({0,255,255}),
        "ProDOS block count saturates at 65535");
    expect(transact(sp, 1, {0, 0x60, 0, 0, 0, 0}, 3) ==
        std::vector<uint8_t>({SP::no_device, 0, 0}), "wrong ProDOS slot is disconnected");
    expect(transact(sp, 2, request(1, 0), 1)[0] == SP::no_device, "unit zero cannot read data");
    expect(transact(sp, 2, request(1, 9), 1)[0] == SP::no_device, "unit nine rejected");
    expect(transact(sp, 2, request(1, 1, 65537), 1)[0] == SP::io_error, "block boundary");
    auto wr = request(2, 8); wr.resize(522, 0xAB);
    expect(transact(sp, 2, wr, 1)[0] == SP::no_write && disks[7][0] == 8, "protected write unchanged");
    wr[2] = 2;
    expect(transact(sp, 2, wr, 1)[0] == 0 && disks[1][511] == 0xAB, "512-byte write");
    expect(transact(sp, 2, request(2, 1), 1)[0] == SP::io_error, "truncated write rejected");
    expect(transact(sp, 2, request(3, 1), 1)[0] == SP::no_write, "firmware FORMAT response");
    expect(transact(sp, 0x40, {}, 1)[0] == SP::badctl, "unused config family unsupported");
    expect(transact(sp, 2, {0}, 1)[0] == SP::badctl, "malformed command completes with error");
    sp.data_write(0xAB); sp.reset();
    expect(sp.control_read() == 0 && sp.data_read() == 0, "reset clears command/response/ready");
    expect(transact(sp, 2, request(0, 0), 11)[0] == 0, "new command after interrupted reset");

    sp.enable_ramdisk(true);
    expect(sp.ramdisk_unit() == -1, "RAM32 never displaces eight file images");
    units[7] = {false, 0, false, true};
    expect(sp.ramdisk_unit() == -1, "failed configured image is not replaced by RAM32");
    units[7] = {};
    expect(sp.ramdisk_unit() == 7, "RAM32 uses first genuinely free unit");
    auto header = transact(sp, 2, request(1, 8, 2), 513);
    expect(header[5] == 0xF5 && std::memcmp(header.data() + 6, "RAM32", 5) == 0,
        "RAM32 ProDOS volume header");
    expect(header[5 + 0x25] == 255 && header[5 + 0x26] == 255, "RAM32 total blocks");
    auto bitmap = transact(sp, 2, request(1, 8, 6), 513);
    expect(bitmap[1] == 0 && bitmap[2] == 0 && bitmap[3] == 3 && bitmap[4] == 255,
        "RAM32 boot, directory and bitmap blocks allocated");
    bitmap = transact(sp, 2, request(1, 8, 21), 513);
    expect(bitmap.back() == 254, "nonexistent block 65535 excluded from free bitmap");
    wr = request(2, 8, 65534); wr.resize(522, 0x9B);
    expect(transact(sp, 2, wr, 1)[0] == 0, "last RAM32 block writable");
    sp.reset();
    expect(transact(sp, 2, request(1, 8, 65534), 513)[512] == 0x9B, "Apple reset retains RAM32");
    expect(transact(sp, 2, request(1, 8, 65535), 1)[0] == SP::io_error, "RAM32 end bound");
    units[7] = {true, 1, false, true};
    units[6] = {};
    expect(sp.ramdisk_unit() == 6, "host mount moves RAM32 to next free unit");
    expect(transact(sp, 2, request(1, 7, 65534), 513)[1] == 0,
        "RAM32 relocation reformats as firmware does");
    units[7] = {};
    units[6] = {true, 1, false, true};
    expect(sp.ramdisk_unit() == 7, "RAM32 returns to available eighth unit");
    sp.enable_ramdisk(false); sp.enable_ramdisk(true);
    expect(transact(sp, 2, request(1, 8, 65534), 513)[1] == 0, "disable discards volatile RAM32");
    std::puts("All Appletini SmartPort protocol and RAM32 tests passed");
}
