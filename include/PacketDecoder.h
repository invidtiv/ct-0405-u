#pragma once

#include "Common.h"
#include <vector>
#include <cstdint>
#include <string>

namespace ct0405 {

class PacketDecoder {
public:
    PacketDecoder();
    explicit PacketDecoder(DeviceModel model);

    void SetDeviceModel(DeviceModel model);
    DeviceModel GetDeviceModel() const { return m_model; }

    void SetCapabilities(const TabletCapabilities& caps);
    const TabletCapabilities& GetCapabilities() const { return m_caps; }

    void SetCustomBounds(uint32_t max_x, uint32_t max_y, uint32_t max_pressure, bool auto_detect);

    // Decode a raw HID report buffer into a TabletRawState
    // Returns true if a valid state change occurred
    bool DecodePacket(const uint8_t* data, size_t length, TabletRawState& out_state);

    // Reset internal state
    void Reset();

    bool IsInProximity() const { return m_last_state.in_proximity; }
    const TabletRawState& GetLastState() const { return m_last_state; }

    static std::string FormatHex(const uint8_t* data, size_t length);

    uint32_t GetObservedMaxX() const { return m_observed_max_x; }
    uint32_t GetObservedMaxY() const { return m_observed_max_y; }
    int32_t GetObservedMaxPressure() const { return m_observed_max_pressure; }

private:
    bool DecodePenPartnerReport(const uint8_t* data, size_t length, TabletRawState& out_state);
    bool DecodeGraphireReport(const uint8_t* data, size_t length, TabletRawState& out_state);
    bool DecodeGenericReport(const uint8_t* data, size_t length, TabletRawState& out_state);

    DeviceModel m_model = DeviceModel::Unknown;
    TabletCapabilities m_caps;
    TabletRawState m_last_state;

    bool m_auto_detect_bounds = true;
    uint32_t m_observed_max_x = 5040;
    uint32_t m_observed_max_y = 3780;
    int32_t m_observed_max_pressure = 255;
};

} // namespace ct0405
