// Code under test: GSSquared's Phasor speech path, compiled from the
// worktree's own sources (SSI263.cpp, W6522.hpp, PhasorLogic.hpp,
// PhasorAudio.hpp). mb2.cpp itself needs SDL, NClock and the whole machine,
// so its Mockingboard class is mirrored here call for call for everything
// the speech path touches (lines cited). The AY chips take part in the bus
// protocol (register latch, write, read-back through VIA port A, as mb-audit
// and the Phasor programs probe them) but render no audio: card.pcm has no
// AY, and the SDL stream is left out.
#include <cstdint>
#include <memory>

#include "devices/mockingboard/PhasorAudio.hpp"
#include "devices/mockingboard/PhasorLogic.hpp"
#include "devices/mockingboard/SSI263.hpp"
#include "devices/mockingboard/W6522.hpp"
#include "devices/mockingboard/AY8910-2.hpp"
#include "util/InterruptController.hpp"

#include "../common/driver.hpp"

uint64_t debug_level = 0;

class GssCard : public Card {
public:
    GssCard() {
        // mb2.cpp:350-357
        local_irq_ = std::make_unique<InterruptController>();
        local_irq_->register_irq_receiver([this](bool) { updateCardIrq(); });
        via_[0] = std::make_unique<N6522>("MB_6522 1 0x80", nullptr, local_irq_.get(), 4, 0);
        via_[1] = std::make_unique<N6522>("MB_6522 2 0x00", nullptr, local_irq_.get(), 4, 1);
        via_[0]->set_ira(0xFF);
        via_[1]->set_ira(0xFF);
        ay_primary_ = std::make_unique<AY8910s>(&ay_buf_[0], nullptr, nullptr);
        ay_secondary_ = std::make_unique<AY8910s>(&ay_buf_[1], nullptr, nullptr);
    }

    ~GssCard() override {
        // mb2.cpp's destructor sets shutting_down first; detach the receiver
        // so the VIAs' teardown does not call back into a dying card.
        local_irq_->register_irq_receiver(nullptr);
        via_[0].reset();
        via_[1].reset();
    }

    uint8_t cycle(const Access *acc, bool due, bool res_low) override {
        // RES acts from its first cycle (the RTL resets on the clock edges
        // before the serve point), so apply it before taking the view.
        if (res_low) {
            if (!in_reset_) reset(false);  // the machine's reset handler, once per RES
            in_reset_ = true;
        } else {
            in_reset_ = false;
        }
        view_.irq = card_irq_;
        view_.d7[0] = ssi_secondary_.ready();
        view_.d7[1] = ssi_primary_.ready();
        view_.dirq[0] = phasorNative() && ssi_secondary_.ready() && ssi_secondary_.interruptsEnabled();
        view_.dirq[1] = phasorNative() && ssi_primary_.ready() && ssi_primary_.interruptsEnabled();
        uint8_t got = 0x00;
        if (acc && !res_low) {
            if ((acc->addr & 0xFFF0) == 0xC0C0) {
                modeSwitch(acc->addr);           // mb_read_C0nx / mb_write_C0nx
            } else if ((acc->addr & 0xFF00) == 0xC400) {
                if (acc->rw) got = read(acc->addr, 0x00);
                else write(acc->addr, acc->data);
            }
        }
        clockDevices(due);
        return got;
    }
    View view() override { return view_; }
    Sample sample() override { return s_; }
    const char *name() override { return "gss"; }

private:
    bool mockingboardMode() const { return PhasorLogic::isMockingboard(mode_); }
    bool phasorNative() const { return PhasorLogic::isPhasorNative(mode_); }

    // mb2.cpp:205-209
    bool directSpeechIrq() const {
        if (!phasorNative()) return false;
        return (ssi_primary_.ready() && ssi_primary_.interruptsEnabled()) ||
               (ssi_secondary_.ready() && ssi_secondary_.interruptsEnabled());
    }
    // mb2.cpp:211-217
    void updateCardIrq() {
        card_irq_ = (local_irq_ && local_irq_->any_irq_asserted()) || directSpeechIrq();
    }
    // mb2.cpp:219-224
    void routeSpeechCompletion(SSI263 &ssi, uint8_t via) {
        if (ssi.takeCompletion() && mockingboardMode() && ssi.interruptsEnabled()) {
            via_[via]->signal_ca1_falling_edge();
        }
    }
    // mb2.cpp:226-267 (AY generation omitted; the phase is the driver's)
    void clockDevices(bool due) {
        via_[PhasorLogic::kViaHigh]->incr_cycle();
        via_[PhasorLogic::kViaLow]->incr_cycle();
        if (due) {
#ifdef GSS_DBG_PRE
            GSS_DBG_PRE(ssi_secondary_, ssi_primary_);
#endif
            const float secondary = ssi_secondary_.renderSample();
            const float primary = ssi_primary_.renderSample();
#ifdef GSS_DBG_POST
            GSS_DBG_POST(ssi_secondary_, ssi_primary_);
#endif
            s_.sec = toPcm(secondary);
            s_.pri = toPcm(primary);
            // mb2.cpp:507-523 with both AY banks silent.
#ifdef EXP_MIX
            // Analysis only: the RTL's routing (mockingboard.sv:940-962,
            // mix_speech = sat_add16(PSG, speech)): the A5 secondary socket
            // into the left channel only, the A6 primary into the right
            // only, at full level. Then GSSquared's own warmth stage.
            const PhasorLogic::StereoSample mixed{
                PhasorLogic::limitAudioSample(secondary),
                PhasorLogic::limitAudioSample(primary)};
#else
            const PhasorLogic::StereoSample mixed =
                PhasorLogic::mixAudioSample(0.0f, 0.0f, 0.0f, 0.0f, secondary, primary);
#endif
            const PhasorAudio::StereoSample shaped = warmth_.process(mixed.left, mixed.right);
            s_.l = toPcm(shaped.left);
            s_.r = toPcm(shaped.right);
        }
        ssi_primary_.clockXck();
        ssi_secondary_.clockXck();
        routeSpeechCompletion(ssi_primary_, PhasorLogic::kViaHigh);
        routeSpeechCompletion(ssi_secondary_, PhasorLogic::kViaLow);
        updateCardIrq();
    }
    static int16_t toPcm(float f) {
        float v = f * 32768.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        return static_cast<int16_t>(v);
    }
    // mb2.cpp:281-302 (AY clock rate and selections omitted)
    void modeSwitch(uint32_t addr) {
        const uint8_t next = PhasorLogic::updateModeLatch(mode_, static_cast<uint16_t>(addr));
        if (next == mode_) return;
        mode_ = next;
        setAyClockRate();
        if (!PhasorLogic::isExtended(mode_)) ay_selected_[0] = ay_selected_[1] = {false, false};
        if (mockingboardMode()) {
            if (ssi_primary_.ready() && ssi_primary_.interruptsEnabled())
                via_[PhasorLogic::kViaHigh]->signal_ca1_falling_edge();
            if (ssi_secondary_.ready() && ssi_secondary_.interruptsEnabled())
                via_[PhasorLogic::kViaLow]->signal_ca1_falling_edge();
        }
        updateCardIrq();
    }
    // mb2.cpp:425-446 (ayBusCycle omitted: it only feeds the AY and IRA)
    void write(uint32_t addr, uint8_t data) {
        const uint8_t offset = addr & 0xFF;
        const uint8_t reg = offset & 0x0F;
        const PhasorLogic::ViaHits via_hits = PhasorLogic::decodeViaHits(mode_, offset);
        for (uint8_t via = 0; via < 2; ++via) {
            if (!PhasorLogic::viaHit(via_hits, via)) continue;
            via_[via]->write(reg, data);
            if (reg == MB_6522_ORB) ayBusCycle(via);
        }
        const PhasorLogic::SsiSelects sel = PhasorLogic::decodeSsiWrites(mode_, offset);
        const uint8_t ssi_reg = SSI263::registerForOffset(offset);
        if (sel.primary) ssi_primary_.write(ssi_reg, data);
        if (sel.secondary) ssi_secondary_.write(ssi_reg, data);
        updateCardIrq();
    }
    // mb2.cpp:269-279, 304-343 (the bus protocol only)
    void setAyClockRate() {
        const uint8_t multiplier = phasorNative() ? 2 : 1;
        ay_primary_->setClockMultiplier(multiplier);
        ay_secondary_->setClockMultiplier(multiplier);
    }
    void ayBusCycle(uint8_t via) {
        const uint8_t ay_chip = PhasorLogic::ayChipForVia(via);
        const uint8_t ddra = via_[via]->get_ddra();
        const uint8_t pa = static_cast<uint8_t>((via_[via]->get_ora() & ddra) | static_cast<uint8_t>(~ddra));
        const uint8_t ddrb = via_[via]->get_ddrb();
        const uint8_t pb = static_cast<uint8_t>((via_[via]->get_orb() & ddrb) | static_cast<uint8_t>(~ddrb));
        const double t = 0.0;
        const PhasorLogic::AyRoute route = PhasorLogic::decodeAyRoute(mode_, via, pb, ay_selected_[via]);
        if (route.reset) {
            ay_primary_->busCycle(ay_chip, pa, pb, t);
            ay_secondary_->busCycle(ay_chip, pa, pb, t);
            ay_selected_[via] = route.next_selection;
            via_[via]->set_ira(0xFF);
            return;
        }
        AyBusResult primary_result{false, 0};
        AyBusResult secondary_result{false, 0};
        if (route.drive_primary) primary_result = ay_primary_->busCycle(ay_chip, pa, pb, t);
        if (route.drive_secondary) secondary_result = ay_secondary_->busCycle(ay_chip, pa, pb, t);
        via_[via]->set_ira(PhasorLogic::combineAyRead(primary_result.drove_data, primary_result.data,
                                                     secondary_result.drove_data, secondary_result.data));
        ay_selected_[via] = route.next_selection;
    }

    // mb2.cpp:448-474
    uint8_t read(uint32_t addr, uint8_t floating_bus) {
        const uint8_t offset = addr & 0xFF;
        const PhasorLogic::SsiSocket status_socket = PhasorLogic::nativeStatusSocket(mode_, offset);
        if (status_socket != PhasorLogic::SsiSocket::None) {
            const bool d7 = status_socket == PhasorLogic::SsiSocket::Secondary
                                ? ssi_secondary_.ready() : ssi_primary_.ready();
            return PhasorLogic::nativeStatusValue(floating_bus, d7);
        }
        const PhasorLogic::ViaHits via_hits = PhasorLogic::decodeViaHits(mode_, offset);
        uint8_t result = 0;
        bool any_hit = false;
        for (uint8_t via = 0; via < 2; ++via) {
            if (!PhasorLogic::viaHit(via_hits, via)) continue;
#ifdef EXP_VIAREAD
            // RTL order (mockingboard.sv:1083-1093, via6522.v timer_read_extra_tick):
            // the value is served at serve_en, the extra native-mode tick
            // lands with the read strobe at data_en, after it.
            result |= via_[via]->read(offset & 0x0F);
            if (PhasorLogic::nativeTimerReadNeedsExtraTick(mode_, offset)) via_[via]->incr_cycle();
#else
            result |= PhasorLogic::readVia(mode_, offset, *via_[via]);
#endif
            any_hit = true;
        }
        return any_hit ? result : floating_bus;
    }
    // mb2.cpp:551-572 (warm reset path; the cold one is the constructor)
    void reset(bool cold_start) {
        mode_ = PhasorLogic::kModeMockingboard;
        ay_selected_[0] = ay_selected_[1] = {false, false};
        setAyClockRate();
        via_[0]->reset();
        via_[1]->reset();
        ay_primary_->reset();
        ay_secondary_->reset();
        ssi_primary_.reset(cold_start);
        ssi_secondary_.reset(cold_start);
        if (cold_start) warmth_.reset();
        via_[0]->set_ira(0xFF);
        via_[1]->set_ira(0xFF);
        updateCardIrq();
    }

    std::unique_ptr<InterruptController> local_irq_;
    std::unique_ptr<N6522> via_[2];
    std::vector<float> ay_buf_[2];
    std::unique_ptr<AY8910s> ay_primary_, ay_secondary_;
    PhasorLogic::AySelection ay_selected_[2] = {};
    SSI263 ssi_primary_;
    SSI263 ssi_secondary_;
    PhasorAudio::WarmthFilter warmth_;
    uint8_t mode_ = PhasorLogic::kModeMockingboard;
    bool card_irq_ = false;
    bool in_reset_ = false;
    View view_{false, {false, false}, {false, false}};
    Sample s_{0, 0, 0, 0};
};

#ifndef GSS_NO_MAIN
int main(int argc, char **argv) {
    GssCard card;
    return driverMain(card, argc, argv);
}
#endif
