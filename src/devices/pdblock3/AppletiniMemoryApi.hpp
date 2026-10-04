#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

/* Appletini copy/fill API 1.0 (appletini-one README_MEMORY_API.md).

   A 65C02 program reaches it through the Appletini SmartPort controller,
   unit 0, selector $80: STATUS returns a 32-byte capability and result
   block, CONTROL executes a list of up to 16 COPY/FILL descriptors between
   MAIN, base AUX and RamWorks banks 1-126. Descriptor endpoints are
   physical: they ignore RAMRD, RAMWRT and the selected RamWorks bank.

   The real card holds the vTW CPU while its ARM or FPGA moves the data.
   The emulator runs the whole list before the response becomes READY, so a
   caller polling CTRL bit 7 sees the same sequence: not ready, then ready
   with the result byte. */
class AppletiniMemoryApi {
public:
    static constexpr uint8_t SELECTOR = 0x80;
    static constexpr uint8_t MAJOR = 1;
    static constexpr uint8_t MINOR = 0;
    static constexpr std::size_t STATUS_SIZE = 32;
    static constexpr std::size_t HEADER_SIZE = 8;
    static constexpr std::size_t DESCRIPTOR_SIZE = 16;
    static constexpr uint8_t MAX_DESCRIPTORS = 16;
    static constexpr uint8_t MAX_AUX_BANK = 126;
    static constexpr uint16_t MIN_ADDR = 0x0200;
    static constexpr uint32_t LIMIT_ADDR = 0xC000;
    static constexpr uint16_t DMA_CHUNK = 512;   // legacy STATUS field only

    static constexpr uint8_t OP_COPY = 1;
    static constexpr uint8_t OP_FILL = 2;
    static constexpr uint8_t FLAG_PRIVATE = 1;
    static constexpr uint8_t SPACE_MAIN = 0;
    static constexpr uint8_t SPACE_AUX = 1;
    static constexpr uint16_t FEATURES = 0x0007;  // COPY, FILL, PRIVATE

    static constexpr uint8_t OK = 0x00;
    static constexpr uint8_t BADCTL = 0x21;
    static constexpr uint8_t UNAVAILABLE = 0x60;
    static constexpr uint8_t BAD_HEADER = 0x61;
    static constexpr uint8_t BAD_DESCRIPTOR = 0x62;
    static constexpr uint8_t RANGE = 0x63;
    static constexpr uint8_t OVERLAP = 0x64;
    static constexpr uint8_t PRIVATE_REQUIRED = 0x65;

    static constexpr std::size_t BANK_SIZE = 0x10000;

    /* The emulated storage. main and aux are the IIe's two 64K banks;
       ramworks holds RamWorks banks 1-127 back to back (bank b at
       (b - 1) * 64K), the same banks $C073 selects. ramworks may be null
       when the expansion is off: extended-AUX requests are then
       UNAVAILABLE. A null main or aux makes the service unavailable. */
    struct Memory {
        uint8_t *main = nullptr;
        uint8_t *aux = nullptr;
        uint8_t *ramworks = nullptr;
        std::size_t ramworks_size = 0;
    };

    static bool available(const Memory &memory) {
        return memory.main != nullptr && memory.aux != nullptr;
    }

    void status(uint8_t out[STATUS_SIZE], const Memory &memory) const {
        std::memset(out, 0, STATUS_SIZE);
        std::memcpy(out, "AMEM", 4);
        out[4] = MAJOR;
        out[5] = MINOR;
        out[6] = DESCRIPTOR_SIZE;
        out[7] = MAX_DESCRIPTORS;
        put16(out + 8, FEATURES);
        put16(out + 10, MIN_ADDR);
        put16(out + 12, static_cast<uint16_t>(LIMIT_ADDR));
        out[14] = MAX_AUX_BANK;
        out[15] = available(memory) ? 1 : 0;
        put32(out + 16, micros());
        put16(out + 20, DMA_CHUNK);
        out[22] = last_error_;
        out[23] = last_completed_descriptors_;
        put32(out + 24, last_elapsed_us_);
        put32(out + 28, last_completed_bytes_);
    }

    /* payload starts at "AMEM", after the CONTROL list's length word;
       null for a malformed SmartPort frame (BAD_HEADER, as on the card).
       Every descriptor is checked before any destination is written. */
    uint8_t execute(const uint8_t *payload, std::size_t length,
                    const Memory &memory) {
        const uint32_t started = micros();
        last_error_ = OK;
        last_completed_descriptors_ = 0;
        last_completed_bytes_ = 0;
        last_elapsed_us_ = 0;
        const uint8_t error = run(payload, length, memory);
        last_error_ = error;
        last_elapsed_us_ = micros() - started;
        return error;
    }

    /* A SmartPort CONTROL frame as the FIFO receives it: the command byte
       ($04), nine parameter bytes (count 3, unit 0, the buffer pointer,
       selector $80, four bytes of ROM padding, which are ignored), the
       list's length word, then that many payload bytes. The caller has
       already routed unit 0, selector $80 here. A wrong parameter count or
       a length word that disagrees with the bytes sent is BAD_HEADER, as
       smartport_service.c answers it. */
    uint8_t control_frame(const uint8_t *frame, std::size_t length,
                          const Memory &memory) {
        if (length < 12 || frame[1] != 3 ||
            static_cast<std::size_t>(get16(frame + 10)) + 12 != length) {
            return execute(nullptr, 0, memory);
        }
        return execute(frame + 12, length - 12, memory);
    }

    uint8_t last_error() const { return last_error_; }
    uint8_t last_completed_descriptors() const { return last_completed_descriptors_; }
    uint32_t last_completed_bytes() const { return last_completed_bytes_; }

private:
    struct Endpoint {
        uint8_t space = 0;
        uint8_t bank = 0;   // logical: MAIN 0, AUX 0-126
        uint16_t address = 0;
    };

    struct Descriptor {
        uint8_t operation = 0;
        uint8_t flags = 0;
        uint8_t fill = 0;
        uint16_t length = 0;
        Endpoint source;
        Endpoint destination;
    };

    uint8_t last_error_ = OK;
    uint8_t last_completed_descriptors_ = 0;
    uint32_t last_completed_bytes_ = 0;
    uint32_t last_elapsed_us_ = 0;

    static uint16_t get16(const uint8_t *p) {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    static void put16(uint8_t *p, uint16_t value) {
        p[0] = static_cast<uint8_t>(value);
        p[1] = static_cast<uint8_t>(value >> 8);
    }

    static void put32(uint8_t *p, uint32_t value) {
        put16(p, static_cast<uint16_t>(value));
        put16(p + 2, static_cast<uint16_t>(value >> 16));
    }

    static uint32_t micros() {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /* The physical bank: MAIN 0, base AUX 1, RamWorks bank n is n + 1. */
    static uint8_t physical_bank(const Endpoint &e) {
        return e.space == SPACE_MAIN ? 0 : static_cast<uint8_t>(e.bank + 1);
    }

    static uint8_t endpoint(const uint8_t *p, uint16_t length, Endpoint &e) {
        e.space = p[0];
        e.bank = p[1];
        e.address = get16(p + 2);
        if (e.space > SPACE_AUX ||
            (e.space == SPACE_MAIN && e.bank != 0) ||
            (e.space == SPACE_AUX && e.bank > MAX_AUX_BANK)) {
            return RANGE;
        }
        if (length == 0 || e.address < MIN_ADDR ||
            static_cast<uint32_t>(e.address) + length > LIMIT_ADDR) {
            return RANGE;
        }
        return OK;
    }

    static uint8_t *storage(const Memory &memory, const Endpoint &e) {
        if (e.space == SPACE_MAIN) return memory.main + e.address;
        if (e.bank == 0) return memory.aux + e.address;
        return memory.ramworks + (static_cast<std::size_t>(e.bank) - 1) * BANK_SIZE
               + e.address;
    }

    static bool ramworks_holds(const Memory &memory, const Endpoint &e) {
        return memory.ramworks != nullptr &&
               static_cast<std::size_t>(e.bank) * BANK_SIZE <= memory.ramworks_size;
    }

    uint8_t run(const uint8_t *payload, std::size_t length, const Memory &memory) {
        if (payload == nullptr || length < HEADER_SIZE ||
            std::memcmp(payload, "AMEM", 4) != 0 ||
            payload[4] != MAJOR || payload[6] != 0 || payload[7] != 0 ||
            payload[5] == 0 || payload[5] > MAX_DESCRIPTORS ||
            length != HEADER_SIZE + payload[5] * DESCRIPTOR_SIZE) {
            return BAD_HEADER;
        }

        Descriptor list[MAX_DESCRIPTORS];
        const uint8_t count = payload[5];
        bool needs_ramworks = false;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t *p = payload + HEADER_SIZE + i * DESCRIPTOR_SIZE;
            Descriptor &d = list[i];
            d.operation = p[0];
            d.flags = p[1];
            d.length = get16(p + 10);
            d.fill = p[12];
            if ((d.operation != OP_COPY && d.operation != OP_FILL) ||
                (d.flags & ~FLAG_PRIVATE) != 0 ||
                p[13] != 0 || p[14] != 0 || p[15] != 0 ||
                (d.operation == OP_COPY && d.fill != 0) ||
                (d.operation == OP_FILL &&
                 (p[2] != 0 || p[3] != 0 || p[4] != 0 || p[5] != 0))) {
                return BAD_DESCRIPTOR;
            }
            uint8_t error = endpoint(p + 6, d.length, d.destination);
            if (error != OK) return error;
            if (d.operation == OP_COPY) {
                error = endpoint(p + 2, d.length, d.source);
                if (error != OK) return error;
                if (physical_bank(d.source) == physical_bank(d.destination) &&
                    d.source.address < d.destination.address + d.length &&
                    d.destination.address < d.source.address + d.length) {
                    return OVERLAP;
                }
                if (physical_bank(d.source) >= 2) needs_ramworks = true;
            }
            if (physical_bank(d.destination) >= 2) needs_ramworks = true;
        }

        if (!available(memory)) return UNAVAILABLE;
        if (needs_ramworks) {
            for (uint8_t i = 0; i < count; ++i) {
                const Descriptor &d = list[i];
                if ((physical_bank(d.destination) >= 2 &&
                     !ramworks_holds(memory, d.destination)) ||
                    (d.operation == OP_COPY && physical_bank(d.source) >= 2 &&
                     !ramworks_holds(memory, d.source))) {
                    return UNAVAILABLE;
                }
            }
        }

        /* Every MAIN or base-AUX destination is private working memory:
           the caller must say so, for the whole list, before any write. */
        for (uint8_t i = 0; i < count; ++i) {
            if ((list[i].flags & FLAG_PRIVATE) == 0 &&
                physical_bank(list[i].destination) < 2) {
                return PRIVATE_REQUIRED;
            }
        }

        for (uint8_t i = 0; i < count; ++i) {
            const Descriptor &d = list[i];
            uint8_t *target = storage(memory, d.destination);
            if (d.operation == OP_COPY) {
                /* Same-bank overlap was refused; different banks never
                   alias, so memcpy is exact. */
                std::memcpy(target, storage(memory, d.source), d.length);
            } else {
                std::memset(target, d.fill, d.length);
            }
            last_completed_bytes_ += d.length;
            last_completed_descriptors_++;
        }
        return OK;
    }
};
