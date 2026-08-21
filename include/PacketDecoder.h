#pragma once

#include "Common.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace ct0405 {

// Decoder for the Wacom PenPartner report protocol used by the CT-0405-U.
//
// This driver targets one device family and decodes one protocol. There is
// deliberately no fallback path for other Wacom models: a report this decoder
// does not recognise is reported as undecodable rather than reinterpreted by a
// handler written for different hardware, which produced plausible-looking
// garbage coordinates instead of an honest failure.
//
// Layout of a PenPartner report (verified against a CT-0405-U):
//   [0] report id (0x01 or 0x02)
//   [1..2] X, little endian
//   [3..4] Y, little endian
//   [5] status: bit7 in-range, bit6 barrel switch, bit5 eraser, bit4 barrel 2
//   [6] pressure, signed, biased by +127
class PacketDecoder {
public:
    PacketDecoder();

    const TabletCapabilities& GetCapabilities() const { return m_caps; }
    void SetCapabilities(const TabletCapabilities& caps);

    void SetAutoDetectBounds(bool enable) { m_auto_detect_bounds.store(enable, std::memory_order_relaxed); }
    bool GetAutoDetectBounds() const { return m_auto_detect_bounds.load(std::memory_order_relaxed); }

    // Forgets what the hardware has reported so far. Call when the device
    // reconnects or the user re-states the bounds, so stale extents from a
    // previous session cannot hold the mapping open.
    void ResetObservedExtents();

    // Returns false when the report does not match the PenPartner protocol.
    // Callers must not treat an undecoded packet as pen movement.
    bool DecodePacket(const uint8_t* data, size_t length, TabletRawState& out_state);

    void Reset();

    bool IsInProximity() const { return m_last_state.in_proximity; }
    const TabletRawState& GetLastState() const { return m_last_state; }

    static std::string FormatHex(const uint8_t* data, size_t length);

    // Largest coordinates/pressure actually seen from the hardware, or zero if
    // nothing has been seen yet. Consumed by TabletDriver when "auto-expand
    // bounds" is enabled. Atomic because the HID thread writes them while the
    // UI thread can read them through ResolveEffectiveCaps.
    uint32_t GetObservedMaxX() const { return m_observed_max_x.load(std::memory_order_relaxed); }
    uint32_t GetObservedMaxY() const { return m_observed_max_y.load(std::memory_order_relaxed); }
    int32_t GetObservedMaxPressure() const { return m_observed_max_pressure.load(std::memory_order_relaxed); }

private:
    bool DecodePenPartnerReport(const uint8_t* data, size_t length, TabletRawState& out_state);
    void NoteObservedExtents(const TabletRawState& state);
    bool IsPlausible(const TabletRawState& state) const;

    TabletCapabilities m_caps;
    TabletRawState m_last_state;

    std::atomic<bool> m_auto_detect_bounds{ true };

    // Zero means "nothing observed yet". Seeding these with the nominal
    // capabilities made auto-expand a floor rather than a ceiling-raiser.
    std::atomic<uint32_t> m_observed_max_x{ 0 };
    std::atomic<uint32_t> m_observed_max_y{ 0 };
    std::atomic<int32_t> m_observed_max_pressure{ 0 };
};

} // namespace ct0405
