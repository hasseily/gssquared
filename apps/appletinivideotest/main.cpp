#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "devices/displaypp/generate/AppletiniVideo.hpp"

namespace {

constexpr size_t WIDTH = 640;
constexpr size_t HEIGHT = 400;
constexpr uint16_t SCB = 0x9D00;
constexpr uint16_t CTRL = 0x9DF8;
constexpr uint16_t MAGIC = 0x9DFC;
constexpr uint16_t PALETTE = 0x9E00;

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

void set_magic(std::vector<uint8_t> &bank, bool shr4) {
    const uint8_t value[4] = {
        static_cast<uint8_t>(shr4 ? 0xD3 : 0xB3),
        static_cast<uint8_t>(shr4 ? 0xC8 : 0xB2),
        static_cast<uint8_t>(shr4 ? 0xD2 : 0xB0),
        static_cast<uint8_t>(shr4 ? 0xB4 : 0xB0),
    };
    std::memcpy(bank.data() + MAGIC, value, sizeof(value));
}

void set_palette(std::vector<uint8_t> &bank, uint16_t index,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t selector = 0) {
    const uint16_t address = static_cast<uint16_t>(PALETTE + index * 2);
    bank[address] = static_cast<uint8_t>((g << 4) | b);
    bank[address + 1] = static_cast<uint8_t>((selector << 4) | r);
}

bool color_is(RGBA_t color, uint8_t r, uint8_t g, uint8_t b) {
    return color.r == r && color.g == g && color.b == b && color.a == 0xFF;
}

void reset(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
           std::vector<RGBA_t> &output) {
    std::fill(main.begin(), main.end(), 0);
    std::fill(aux.begin(), aux.end(), 0);
    std::fill(output.begin(), output.end(), RGBA_t::make(0, 0, 0));
}

void test_base_shr(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                   std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_palette(aux, 0, 15, 0, 0);
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.family == appletini_shr_family_t::SHR, "base SHR family");
    expect(color_is(output[0], 240, 0, 0), "base SHR 320 pixel");
    expect(output[0] == output[1] && output[0] == output[WIDTH],
           "base SHR doubles rows and 320 samples");

    reset(main, aux, output);
    aux[SCB] = 0x80;
    aux[0x2000] = 0x1B;
    set_palette(aux, 8, 15, 0, 0);
    set_palette(aux, 13, 0, 15, 0);
    set_palette(aux, 2, 0, 0, 15);
    set_palette(aux, 7, 15, 15, 15);
    appletini_render_shr(main.data(), aux.data(), output.data(), WIDTH, false);
    expect(color_is(output[0], 240, 0, 0) &&
           color_is(output[1], 0, 240, 0) &&
           color_is(output[2], 0, 0, 240) &&
           color_is(output[3], 240, 240, 240), "base SHR 640 palette map");
}

void test_shr4_r4g4b4(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                      std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_magic(aux, true);
    for (uint16_t i = 0; i < 16; ++i) set_palette(aux, i, 0, 0, 0, 3);
    aux[0x2000] = 0xC3;
    aux[0x2001] = 0xD4;
    aux[0x2002] = 0xE5;
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.family == appletini_shr_family_t::SHR4 &&
           (info.selector_mask & (1u << 3)) != 0, "SHR4 R4G4B4 selection");
    expect(color_is(output[0], 192, 48, 208) &&
           color_is(output[5], 192, 48, 208), "R4G4B4 first pixel width");
    expect(color_is(output[6], 64, 224, 80), "R4G4B4 second pixel");
}

void test_shr4_rggb(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                    std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_magic(aux, true);
    std::fill(aux.begin() + 0x2000, aux.begin() + 0x9D00, 0xFF);
    for (uint16_t i = 0; i < 16; ++i) set_palette(aux, i, i, i, i, 1);
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect((info.selector_mask & (1u << 1)) != 0, "SHR4 RGGB selection");
    expect(color_is(output[100 * WIDTH + 200], 255, 255, 255),
           "SHR4 RGGB uniform-field reconstruction");
}

void test_shr4_pal256(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                      std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_magic(aux, true);
    set_palette(aux, 0, 0, 0, 0, 2);
    set_palette(aux, 1, 15, 0, 0, 2);
    set_palette(aux, 165, 0, 15, 0, 2);
    aux[0x2000] = 1;
    aux[0x20A0] = 165;
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.pal256, "SHR4 PAL256 field detection");
    expect(color_is(output[0], 240, 0, 0) &&
           color_is(output[320], 0, 240, 0), "PAL256 packed 320-byte row");
    expect(output[0] == output[3 * WIDTH], "PAL256 progressive row quadrupling");

    reset(main, aux, output);
    set_magic(aux, true);
    set_magic(main, true);
    aux[CTRL] = 1;
    main[CTRL] = 1;
    set_palette(aux, 0, 0, 0, 0, 2);
    set_palette(aux, 1, 15, 0, 0, 2);
    set_palette(main, 0, 0, 0, 0, 2);
    set_palette(main, 2, 0, 0, 15, 2);
    std::fill(aux.begin() + 0x2000, aux.begin() + 0x2000 + 32000, 1);
    std::fill(main.begin() + 0x2000, main.begin() + 0x2000 + 32000, 2);
    info = appletini_render_shr(main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.page_mode == 1 && info.pal256, "PAL256i control");
    expect(color_is(output[199 * WIDTH], 240, 0, 0) &&
           color_is(output[200 * WIDTH], 0, 0, 240),
           "PAL256i uses aux top and main bottom");
}

void test_shr_interlace(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                        std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_magic(aux, true);
    set_magic(main, true);
    aux[CTRL] = 1;
    main[CTRL] = 1;
    set_palette(aux, 0, 15, 0, 0);
    set_palette(main, 0, 0, 0, 15);
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.page_mode == 1, "SHR interlace control");
    expect(color_is(output[0], 240, 0, 0) &&
           color_is(output[WIDTH], 0, 0, 240) &&
           color_is(output[2 * WIDTH], 240, 0, 0),
           "SHR interlace alternates aux and main rows");
}

void test_shr3200(std::vector<uint8_t> &main, std::vector<uint8_t> &aux,
                  std::vector<RGBA_t> &output) {
    reset(main, aux, output);
    set_magic(aux, false);
    aux[CTRL + 1] = 1;
    aux[CTRL + 2] = 0x00;
    aux[CTRL + 3] = 0xA0;
    set_palette(aux, 0, 0, 0, 0);
    const uint16_t reversed_zero = 0xA000 + 15 * 2;
    aux[reversed_zero] = 0x00;
    aux[reversed_zero + 1] = 0x0F;
    AppletiniSHRRenderInfo info = appletini_render_shr(
        main.data(), aux.data(), output.data(), WIDTH, false);
    expect(info.family == appletini_shr_family_t::SHR_3200,
           "SHR-3200 family detection");
    expect(color_is(output[0], 240, 0, 0), "SHR-3200 reversed palette index");
}

void test_legacy_and_video7(std::vector<uint8_t> &main) {
    std::fill(main.begin(), main.end(), 0);
    const uint8_t signature[4] = {0xC1, 0xB2, 0xCC, 0xE9};
    std::memcpy(main.data() + 0x4078, signature, sizeof(signature));
    main[0x407C] = 1;
    expect(appletini_legacy_paged_mode(main.data(), true, true) == 1,
           "HGR A2Li interlace signal");
    expect(appletini_legacy_paged_mode(main.data(), false, true) == 0,
           "A2Li is graphics-gated");

    main[0x407C] = 0xFF;
    expect(appletini_legacy_load_hold_requested(main.data()),
           "signed hires A2Li $FF requests load hold");
    expect(appletini_legacy_paged_mode(main.data(), true, true) == 0,
           "A2Li $FF is not a committed paged mode");

    std::memset(main.data() + 0x4078, 0, sizeof(signature));
    expect(!appletini_legacy_load_hold_requested(main.data()),
           "unsigned $FF does not request load hold");

    std::memcpy(main.data() + 0x0878, signature, sizeof(signature));
    main[0x087C] = 0xFF;
    expect(appletini_legacy_load_hold_requested(main.data()),
           "signed lores A2Li $FF holds across family changes");
    main[0x087C] = 0;
    expect(!appletini_legacy_load_hold_requested(main.data()) &&
           appletini_legacy_paged_mode(main.data(), true, false) == 0,
           "A2Li mode zero selects the base legacy renderer");

    main[0x087C] = 0xFF;
    std::memcpy(main.data() + 0x4078, signature, sizeof(signature));
    main[0x407C] = 2;
    appletini_legacy_reset_load_hold(main.data());
    expect(main[0x087C] == 0 && main[0x407C] == 2,
           "reset aborts $FF while preserving committed A2Li mode");

    AppletiniVideo7State video7;
    video7.access(0xC05E, false, false);
    video7.access(0xC05F, false, false);
    video7.access(0xC05E, false, false);
    video7.access(0xC05F, false, false);
    video7.access(0xC05E, false, false);
    expect(video7.mode == 3 && video7.mono560(), "Video-7 MONO560 sequence");
    video7.access(0xC05E, true, false);
    expect(video7.sequence == 0, "Video-7 mixed-mode abort");
}

int render_file(const char *input_path, const char *output_path) {
    std::ifstream input(input_path, std::ios::binary);
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (data.size() != 32768 && data.size() != 65536) {
        std::fprintf(stderr, "render input must contain one or two 32 KiB SHR banks\n");
        return 2;
    }

    std::vector<uint8_t> main(65536);
    std::vector<uint8_t> aux(65536);
    std::memcpy(aux.data() + 0x2000, data.data(), 32768);
    if (data.size() == 65536) {
        std::memcpy(main.data() + 0x2000, data.data() + 32768, 32768);
    }
    std::vector<RGBA_t> output(WIDTH * HEIGHT);
    appletini_render_shr(main.data(), aux.data(), output.data(), WIDTH, false);

    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "could not create %s\n", output_path);
        return 2;
    }
    file << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    for (const RGBA_t pixel : output) {
        file.put(static_cast<char>(pixel.r));
        file.put(static_cast<char>(pixel.g));
        file.put(static_cast<char>(pixel.b));
    }
    return file ? 0 : 2;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--render") {
        return render_file(argv[2], argv[3]);
    }

    std::vector<uint8_t> main(65536);
    std::vector<uint8_t> aux(65536);
    std::vector<RGBA_t> output(WIDTH * HEIGHT);

    test_base_shr(main, aux, output);
    test_shr4_r4g4b4(main, aux, output);
    test_shr4_rggb(main, aux, output);
    test_shr4_pal256(main, aux, output);
    test_shr_interlace(main, aux, output);
    test_shr3200(main, aux, output);
    test_legacy_and_video7(main);

    if (failures != 0) {
        std::fprintf(stderr, "%d Appletini video tests failed\n", failures);
        return 1;
    }
    std::puts("All Appletini video tests passed");
    return 0;
}
