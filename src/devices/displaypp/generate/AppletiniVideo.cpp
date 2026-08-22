#include "AppletiniVideo.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr uint16_t SHR_IMAGE = 0x2000;
constexpr uint16_t SHR_SCB = 0x9D00;
constexpr uint16_t SHR_CTRL = 0x9DF8;
constexpr uint16_t SHR_MAGIC = 0x9DFC;
constexpr uint16_t SHR_PALETTE = 0x9E00;
constexpr uint32_t SHR_WIDTH = 640;
constexpr uint32_t SHR_HEIGHT = 400;

constexpr uint8_t SHR4_MAGIC[4] = {0xD3, 0xC8, 0xD2, 0xB4};
constexpr uint8_t SHR3200_MAGIC[4] = {0xB3, 0xB2, 0xB0, 0xB0};
constexpr uint8_t A2LI_MAGIC[4] = {0xC1, 0xB2, 0xCC, 0xE9};

constexpr int8_t RGGB_G[13] = {
    -2, 0, 4, 0, -2, 4, 8, 4, -2, 0, 4, 0, -2,
};
constexpr int8_t RGGB_XG[13] = {
    1, -2, 0, -2, -2, 8, 10, 8, -2, -2, 0, -2, 1,
};
constexpr int8_t RGGB_XGX[13] = {
    -2, -2, 8, -2, 1, 0, 10, 0, 1, -2, 8, -2, -2,
};
constexpr int8_t RGGB_RB[13] = {
    -3, 4, 0, 4, -3, 0, 12, 0, -3, 4, 0, 4, -3,
};

bool has_magic(const uint8_t *bank, const uint8_t (&magic)[4]) {
    return bank != nullptr && std::memcmp(bank + SHR_MAGIC, magic, 4) == 0;
}

bool has_shr4(const uint8_t *bank) {
    return has_magic(bank, SHR4_MAGIC);
}

bool has_shr3200(const uint8_t *bank) {
    return has_magic(bank, SHR3200_MAGIC);
}

RGBA_t apply_monochrome(RGBA_t color, bool enabled) {
    if (!enabled) return color;
    const uint8_t y = static_cast<uint8_t>(
        (static_cast<uint32_t>(color.r) * 77u +
         static_cast<uint32_t>(color.g) * 150u +
         static_cast<uint32_t>(color.b) * 29u) >> 8);
    return RGBA_t::make(y, y, y, 0xFF);
}

RGBA_t rgb444(uint8_t r, uint8_t g, uint8_t b, bool monochrome) {
    return apply_monochrome(
        RGBA_t::make(static_cast<uint8_t>(r * 16u),
                     static_cast<uint8_t>(g * 16u),
                     static_cast<uint8_t>(b * 16u), 0xFF),
        monochrome);
}

RGBA_t packed_color(uint16_t color, bool monochrome) {
    return rgb444(static_cast<uint8_t>((color >> 8) & 0x0F),
                  static_cast<uint8_t>((color >> 4) & 0x0F),
                  static_cast<uint8_t>(color & 0x0F), monochrome);
}

RGBA_t palette_color(const uint8_t *bank, uint16_t base, uint8_t index,
                     bool monochrome) {
    const uint16_t address = static_cast<uint16_t>(base + index * 2u);
    const uint16_t color = static_cast<uint16_t>(bank[address]) |
                           static_cast<uint16_t>(bank[address + 1] << 8);
    return packed_color(color, monochrome);
}

RGBA_t average(RGBA_t a, RGBA_t b) {
    return RGBA_t::make(
        static_cast<uint8_t>((static_cast<uint16_t>(a.r) + b.r) >> 1),
        static_cast<uint8_t>((static_cast<uint16_t>(a.g) + b.g) >> 1),
        static_cast<uint8_t>((static_cast<uint16_t>(a.b) + b.b) >> 1),
        0xFF);
}

uint8_t sanitize_page_mode(const uint8_t *aux) {
    if (!(has_shr4(aux) || has_shr3200(aux))) return 0;
    return aux[SHR_CTRL] == 1 || aux[SHR_CTRL] == 2 ? aux[SHR_CTRL] : 0;
}

bool field_uses_selector(const uint8_t *bank, uint8_t selector) {
    if (!has_shr4(bank)) return false;
    for (uint16_t address = SHR_PALETTE + 1; address != 0; address += 2) {
        if ((bank[address] >> 4) == selector) return true;
        if (address == 0x9FFF) break;
    }
    return false;
}

struct RenderContext {
    const uint8_t *main = nullptr;
    const uint8_t *aux = nullptr;
    bool interlaced = false;
    bool monochrome = false;
    AppletiniSHRRenderInfo *info = nullptr;
};

int32_t rggb_sample(const RenderContext &ctx, const uint8_t *field,
                    int32_t x, int32_t row, bool mode640) {
    const int32_t columns = mode640 ? 640 : 320;
    const int32_t rows = ctx.interlaced ? 400 : 200;
    if (x < 0 || x >= columns || row < 0 || row >= rows) return 0;

    const uint8_t *bank = field;
    int32_t source_row = row;
    if (ctx.interlaced) {
        bank = (row & 1) ? ctx.main : ctx.aux;
        source_row >>= 1;
    }

    if (mode640) {
        const uint16_t address = static_cast<uint16_t>(
            SHR_IMAGE + source_row * 160 + (x >> 2));
        return (bank[address] >> (6 - 2 * (x & 3))) & 0x03;
    }

    const uint16_t address = static_cast<uint16_t>(
        SHR_IMAGE + source_row * 160 + (x >> 1));
    return (x & 1) ? (bank[address] & 0x0F) : (bank[address] >> 4);
}

uint8_t rggb_filter(const int32_t (&samples)[13], const int8_t (&weights)[13],
                    int32_t maximum) {
    int32_t value = 0;
    for (size_t i = 0; i < 13; ++i) value += samples[i] * weights[i];
    value = (value * 255) / (maximum * 16);
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

RGBA_t rggb_pixel(const RenderContext &ctx, const uint8_t *field,
                  int32_t x, int32_t row, bool mode640) {
    const int32_t maximum = mode640 ? 3 : 15;
    int32_t samples[13];
    samples[0] = rggb_sample(ctx, field, x, row - 2, mode640);
    samples[1] = rggb_sample(ctx, field, x - 1, row - 1, mode640);
    samples[2] = rggb_sample(ctx, field, x, row - 1, mode640);
    samples[3] = rggb_sample(ctx, field, x + 1, row - 1, mode640);
    samples[4] = rggb_sample(ctx, field, x - 2, row, mode640);
    samples[5] = rggb_sample(ctx, field, x - 1, row, mode640);
    samples[6] = rggb_sample(ctx, field, x, row, mode640);
    samples[7] = rggb_sample(ctx, field, x + 1, row, mode640);
    samples[8] = rggb_sample(ctx, field, x + 2, row, mode640);
    samples[9] = rggb_sample(ctx, field, x - 1, row + 1, mode640);
    samples[10] = rggb_sample(ctx, field, x, row + 1, mode640);
    samples[11] = rggb_sample(ctx, field, x + 1, row + 1, mode640);
    samples[12] = rggb_sample(ctx, field, x, row + 2, mode640);

    const uint8_t own = static_cast<uint8_t>((samples[6] * 255) / maximum);
    uint8_t r;
    uint8_t g;
    uint8_t b;
    if ((x & 1) == 0 && (row & 1) == 0) {
        r = own;
        g = rggb_filter(samples, RGGB_G, maximum);
        b = rggb_filter(samples, RGGB_RB, maximum);
    } else if ((x & 1) != 0 && (row & 1) == 0) {
        r = rggb_filter(samples, RGGB_XG, maximum);
        g = own;
        b = rggb_filter(samples, RGGB_XGX, maximum);
    } else if ((x & 1) == 0) {
        r = rggb_filter(samples, RGGB_XGX, maximum);
        g = own;
        b = rggb_filter(samples, RGGB_XG, maximum);
    } else {
        r = rggb_filter(samples, RGGB_RB, maximum);
        g = rggb_filter(samples, RGGB_G, maximum);
        b = own;
    }
    return apply_monochrome(RGBA_t::make(r, g, b, 0xFF), ctx.monochrome);
}

RGBA_t r4g4b4_pixel(const uint8_t *bank, uint16_t row, uint16_t x,
                    bool monochrome) {
    const uint16_t group = static_cast<uint16_t>(x / 6);
    const uint16_t column = static_cast<uint16_t>(group * 3);
    const uint16_t base = static_cast<uint16_t>(SHR_IMAGE + row * 160 + column);
    const uint8_t b0 = bank[base];
    const uint8_t b1 = column + 1 < 160 ? bank[base + 1] : 0;
    const uint8_t b2 = column + 2 < 160 ? bank[base + 2] : 0;
    if ((x % 6) < 3) {
        return rgb444(b0 >> 4, b0 & 0x0F, b1 >> 4, monochrome);
    }
    return rgb444(b1 & 0x0F, b2 >> 4, b2 & 0x0F, monochrome);
}

bool shr3200_palette(const RenderContext &ctx, const uint8_t *field,
                     uint16_t row, uint16_t &base, const uint8_t *&palette_bank) {
    if (!has_shr3200(field)) return false;
    base = static_cast<uint16_t>(field[SHR_CTRL + 2]) |
           static_cast<uint16_t>(field[SHR_CTRL + 3] << 8);
    if (base > static_cast<uint16_t>(0xFFFFu - 200u * 32u)) return false;
    palette_bank = field[SHR_CTRL + 1] == 1 ? ctx.aux : ctx.main;
    base = static_cast<uint16_t>(base + row * 32u);
    return true;
}

void render_shr_line(const RenderContext &ctx, const uint8_t *bank,
                     uint16_t source_row, uint16_t sample_row, RGBA_t *output) {
    const uint8_t control = bank[SHR_SCB + source_row];
    const uint16_t palette_base = static_cast<uint16_t>(
        SHR_PALETTE + (control & 0x0F) * 32u);
    const bool mode640 = (control & 0x80) != 0;
    const bool shr4 = has_shr4(bank);
    const bool shr3200 = has_shr3200(bank);
    std::array<uint8_t, SHR_WIDTH> indices = {};

    if (mode640) {
        constexpr uint8_t QUADRANT[4] = {8, 12, 0, 4};
        for (uint16_t x = 0; x < SHR_WIDTH; ++x) {
            const uint8_t byte = bank[SHR_IMAGE + source_row * 160 + x / 4];
            const uint8_t value = static_cast<uint8_t>((byte >> (6 - 2 * (x & 3))) & 3);
            indices[x] = static_cast<uint8_t>(QUADRANT[x & 3] + value);
        }
    } else {
        static constexpr uint8_t FILL_SEED[32] = {
            2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        };
        uint8_t last = FILL_SEED[source_row & 0x1F];
        for (uint16_t x = 0; x < 320; ++x) {
            const uint8_t byte = bank[SHR_IMAGE + source_row * 160 + x / 2];
            uint8_t index = (x & 1) ? (byte & 0x0F) : (byte >> 4);
            if (!shr4 && (control & 0x20) != 0) {
                if (index == 0) index = last;
                else last = index;
            }
            indices[x * 2] = index;
            indices[x * 2 + 1] = index;
        }
    }

    uint16_t line_palette_base = 0;
    const uint8_t *line_palette_bank = nullptr;
    const bool use_3200 = !mode640 &&
        shr3200_palette(ctx, bank, source_row, line_palette_base, line_palette_bank);

    for (uint16_t x = 0; x < SHR_WIDTH; ++x) {
        const uint8_t index = indices[x];
        RGBA_t color;
        if (use_3200) {
            color = palette_color(line_palette_bank, line_palette_base,
                                  static_cast<uint8_t>(15 - index), ctx.monochrome);
        } else if (shr4) {
            const uint8_t selector = bank[palette_base + index * 2u + 1u] >> 4;
            if (selector <= 3) ctx.info->selector_mask |= static_cast<uint8_t>(1u << selector);
            else ctx.info->selector_mask = 0x0F;

            if (selector == 1) {
                if (!mode640 && (x & 1)) color = output[x - 1];
                else color = rggb_pixel(ctx, bank, mode640 ? x : x / 2,
                                        sample_row, mode640);
            } else if (selector == 3 && !mode640) {
                color = r4g4b4_pixel(bank, source_row, x / 2, ctx.monochrome);
            } else {
                color = palette_color(bank, palette_base, index, ctx.monochrome);
            }
        } else {
            color = palette_color(bank, palette_base, index, ctx.monochrome);
        }
        output[x] = color;
    }
}

RGBA_t pal256_pixel(const uint8_t *bank, uint16_t row, uint16_t x,
                    bool monochrome) {
    const uint8_t index = bank[SHR_IMAGE + row * 320u + x];
    return palette_color(bank, SHR_PALETTE, index, monochrome);
}

} // namespace

void AppletiniVideo7State::reset() {
    flags = 0;
    mode = 0;
    sequence = 0;
}

void AppletiniVideo7State::access(uint16_t address, bool mixed, bool col80) {
    const uint8_t low = static_cast<uint8_t>(address);
    if ((low & 0xFE) != 0x5E) return;
    if (mixed) {
        sequence = 0;
        return;
    }

    if (low == 0x5E) {
        if (sequence == 0) {
            flags = 0;
            sequence = 1;
        } else if ((sequence & 1) == 0) {
            ++sequence;
            if (sequence == 5) {
                sequence = 0;
                mode = flags;
            }
        }
    } else if ((sequence & 1) != 0) {
        ++sequence;
        if (sequence == 2) {
            flags = col80 ? 0 : 2;
        } else {
            flags = static_cast<uint8_t>((flags & 2) | (col80 ? 0 : 1));
        }
    }
}

uint8_t appletini_legacy_paged_mode(const uint8_t *main_bank,
                                    bool graphics, bool hires) {
    if (main_bank == nullptr || !graphics) return 0;
    const uint16_t base = hires ? 0x4078 : 0x0878;
    if (std::memcmp(main_bank + base, A2LI_MAGIC, 4) != 0) return 0;
    const uint8_t mode = main_bank[base + 4];
    return mode == 1 || mode == 2 ? mode : 0;
}

AppletiniSHRRenderInfo appletini_render_shr(const uint8_t *main_bank,
                                            const uint8_t *aux_bank,
                                            RGBA_t *output,
                                            size_t output_stride,
                                            bool force_monochrome) {
    AppletiniSHRRenderInfo info;
    if (main_bank == nullptr || aux_bank == nullptr || output == nullptr ||
        output_stride < SHR_WIDTH) {
        return info;
    }

    if (has_shr4(aux_bank)) info.family = appletini_shr_family_t::SHR4;
    else if (has_shr3200(aux_bank)) info.family = appletini_shr_family_t::SHR_3200;
    info.page_mode = sanitize_page_mode(aux_bank);
    info.pal256 = field_uses_selector(aux_bank, 2);

    RenderContext ctx;
    ctx.main = main_bank;
    ctx.aux = aux_bank;
    ctx.interlaced = info.page_mode == 1;
    ctx.monochrome = force_monochrome;
    ctx.info = &info;

    if (info.pal256) {
        info.selector_mask |= 1u << 2;
        for (uint16_t y = 0; y < SHR_HEIGHT; ++y) {
            RGBA_t *row = output + static_cast<size_t>(y) * output_stride;
            const uint8_t *bank = aux_bank;
            uint16_t source_row;
            if (info.page_mode == 1) {
                const uint16_t combined_row = y / 2;
                bank = combined_row < 100 ? aux_bank : main_bank;
                source_row = static_cast<uint16_t>(combined_row % 100);
            } else {
                source_row = static_cast<uint16_t>(y / 4);
            }
            for (uint16_t x = 0; x < SHR_WIDTH; ++x) {
                const uint16_t source_x = x / 2;
                RGBA_t color = pal256_pixel(bank, source_row, source_x,
                                            force_monochrome);
                if (info.page_mode == 2) {
                    color = average(
                        pal256_pixel(aux_bank, source_row, source_x, force_monochrome),
                        pal256_pixel(main_bank, source_row, source_x, force_monochrome));
                }
                row[x] = color;
            }
        }
        return info;
    }

    std::array<RGBA_t, SHR_WIDTH> first = {};
    std::array<RGBA_t, SHR_WIDTH> second = {};
    for (uint16_t y = 0; y < SHR_HEIGHT; ++y) {
        RGBA_t *row = output + static_cast<size_t>(y) * output_stride;
        const uint16_t source_row = y / 2;
        if (info.page_mode == 1) {
            const uint8_t *bank = (y & 1) ? main_bank : aux_bank;
            render_shr_line(ctx, bank, source_row, y, row);
        } else if (info.page_mode == 2) {
            render_shr_line(ctx, aux_bank, source_row, source_row, first.data());
            render_shr_line(ctx, main_bank, source_row, source_row, second.data());
            for (uint16_t x = 0; x < SHR_WIDTH; ++x) {
                row[x] = average(first[x], second[x]);
            }
        } else {
            render_shr_line(ctx, aux_bank, source_row, source_row, row);
        }
    }
    return info;
}
