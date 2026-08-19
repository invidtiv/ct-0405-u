#include "PacketDecoder.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace ct0405 {

PacketDecoder::PacketDecoder() {
    Reset();
}

PacketDecoder::PacketDecoder(DeviceModel model) : m_model(model) {
    SetDeviceModel(model);
    Reset();
}

void PacketDecoder::SetDeviceModel(DeviceModel model) {
    m_model = model;
    switch (model) {
        case DeviceModel::PenPartner_CT0405U:
            m_caps.max_x = 5040;
            m_caps.max_y = 3780;
            m_caps.max_pressure = 255;
            m_caps.resolution_lpi = 1016;
            m_caps.has_eraser = true;
            m_caps.has_barrel_switch = true;
            m_caps.has_second_barrel_switch = false;
            break;
        case DeviceModel::Graphire1:
        case DeviceModel::Graphire2:
            m_caps.max_x = 10206;
            m_caps.max_y = 7422;
            m_caps.max_pressure = 511;
            m_caps.resolution_lpi = 1016;
            m_caps.has_eraser = true;
            m_caps.has_barrel_switch = true;
            m_caps.has_second_barrel_switch = true;
            break;
        case DeviceModel::Graphire3:
        case DeviceModel::Graphire4:
            m_caps.max_x = 13918;
            m_caps.max_y = 10206;
            m_caps.max_pressure = 511;
            m_caps.resolution_lpi = 2032;
            m_caps.has_eraser = true;
            m_caps.has_barrel_switch = true;
            m_caps.has_second_barrel_switch = true;
            break;
        case DeviceModel::Volito:
        case DeviceModel::Volito2:
            m_caps.max_x = 5104;
            m_caps.max_y = 3712;
            m_caps.max_pressure = 511;
            m_caps.resolution_lpi = 1016;
            m_caps.has_eraser = false;
            m_caps.has_barrel_switch = true;
            m_caps.has_second_barrel_switch = true;
            break;
        default:
            m_caps.max_x = 5040;
            m_caps.max_y = 3780;
            m_caps.max_pressure = 255;
            m_caps.resolution_lpi = 1016;
            m_caps.has_eraser = true;
            m_caps.has_barrel_switch = true;
            m_caps.has_second_barrel_switch = true;
            break;
    }
    m_observed_max_x = m_caps.max_x;
    m_observed_max_y = m_caps.max_y;
    m_observed_max_pressure = m_caps.max_pressure;
}

void PacketDecoder::SetCapabilities(const TabletCapabilities& caps) {
    m_caps = caps;
    m_observed_max_x = std::max(m_observed_max_x, m_caps.max_x);
    m_observed_max_y = std::max(m_observed_max_y, m_caps.max_y);
    m_observed_max_pressure = std::max(m_observed_max_pressure, static_cast<int32_t>(m_caps.max_pressure));
}

void PacketDecoder::SetCustomBounds(uint32_t max_x, uint32_t max_y, uint32_t max_pressure, bool auto_detect) {
    if (max_x > 0) m_caps.max_x = max_x;
    if (max_y > 0) m_caps.max_y = max_y;
    if (max_pressure > 0) m_caps.max_pressure = max_pressure;
    m_auto_detect_bounds = auto_detect;
    m_observed_max_x = std::max(m_observed_max_x, m_caps.max_x);
    m_observed_max_y = std::max(m_observed_max_y, m_caps.max_y);
}

void PacketDecoder::Reset() {
    m_last_state = TabletRawState{};
    m_last_state.timestamp_us = GetCurrentTimestampUs();
}

std::string PacketDecoder::FormatHex(const uint8_t* data, size_t length) {
    if (!data || length == 0) return "";
    std::ostringstream hex_ss;
    for (size_t i = 0; i < length; ++i) {
        hex_ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
    }
    return hex_ss.str();
}

bool PacketDecoder::DecodePacket(const uint8_t* data, size_t length, TabletRawState& out_state) {
    if (!data || length < 6) {
        return false;
    }

    out_state.timestamp_us = GetCurrentTimestampUs();
    out_state.raw_hex = FormatHex(data, length);

    // Handle leading 0x00 Report ID offset if Windows prepended it
    const uint8_t* pData = data;
    size_t dataLen = length;
    if (data[0] == 0x00 && length > 7 && (data[1] == 0x01 || data[1] == 0x02)) {
        pData = data + 1;
        dataLen = length - 1;
    }

    // Route based on device model
    if (m_model == DeviceModel::Graphire1 || m_model == DeviceModel::Graphire2 || 
        m_model == DeviceModel::Graphire3 || m_model == DeviceModel::Graphire4 || 
        m_model == DeviceModel::Volito || m_model == DeviceModel::Volito2) {
        if (DecodeGraphireReport(pData, dataLen, out_state)) {
            m_last_state = out_state;
            return true;
        }
    }

    // Try PenPartner protocol
    if (DecodePenPartnerReport(pData, dataLen, out_state)) {
        m_last_state = out_state;
        return true;
    }

    // Try Graphire protocol
    if (DecodeGraphireReport(pData, dataLen, out_state)) {
        m_last_state = out_state;
        return true;
    }

    // Try Status-First protocol (Flags, X_lo, X_hi, Y_lo, Y_hi, Pressure)
    if ((pData[0] & 0x80) != 0 || (pData[0] & 0x40) != 0) {
        bool prox = (pData[0] & 0x80) != 0 || (pData[0] & 0x40) != 0;
        out_state.in_proximity = prox;
        if (prox) {
            out_state.raw_x = static_cast<uint32_t>(pData[1]) | (static_cast<uint32_t>(pData[2]) << 8);
            out_state.raw_y = static_cast<uint32_t>(pData[3]) | (static_cast<uint32_t>(pData[4]) << 8);
            out_state.raw_pressure = (dataLen > 5) ? static_cast<int32_t>(pData[5]) : 0;
            out_state.tip_switch = (out_state.raw_pressure > 5) || (pData[0] & 0x01) != 0;
            out_state.barrel_switch_1 = (pData[0] & 0x02) != 0 || (pData[0] & 0x04) != 0;
            out_state.eraser_switch = (pData[0] & 0x20) != 0;
            out_state.tool = out_state.eraser_switch ? ToolType::Eraser : ToolType::Pen;
        } else {
            out_state.tool = ToolType::None;
            out_state.raw_pressure = 0;
        }
        m_last_state = out_state;
        return true;
    }

    // Try Generic coordinate extraction
    if (DecodeGenericReport(pData, dataLen, out_state)) {
        m_last_state = out_state;
        return true;
    }

    return false;
}

bool PacketDecoder::DecodePenPartnerReport(const uint8_t* data, size_t length, TabletRawState& out_state) {
    uint8_t report_id = data[0];

    if (report_id == 0x01 && length >= 7) {
        bool prox = (data[5] & 0x80) != 0;
        out_state.in_proximity = prox;

        if (prox) {
            bool is_eraser = (data[5] & 0x20) != 0;
            out_state.tool = is_eraser ? ToolType::Eraser : ToolType::Pen;
            out_state.eraser_switch = is_eraser;

            out_state.raw_x = static_cast<uint32_t>(data[1]) | (static_cast<uint32_t>(data[2]) << 8);
            out_state.raw_y = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8);

            int8_t raw_p = static_cast<int8_t>(data[6]);
            int32_t pressure = static_cast<int32_t>(raw_p) + 127;
            if (pressure < 0) pressure = 0;
            if (pressure > 255) pressure = 255;
            out_state.raw_pressure = pressure;

            out_state.tip_switch = (raw_p > -127) || (pressure > 5);
            out_state.barrel_switch_1 = (data[5] & 0x40) != 0;
            out_state.barrel_switch_2 = (data[5] & 0x10) != 0;
        } else {
            out_state.tool = ToolType::None;
            out_state.raw_pressure = 0;
            out_state.tip_switch = false;
            out_state.barrel_switch_1 = false;
            out_state.barrel_switch_2 = false;
            out_state.eraser_switch = false;
            out_state.raw_x = m_last_state.raw_x;
            out_state.raw_y = m_last_state.raw_y;
        }

        if (m_auto_detect_bounds && out_state.in_proximity) {
            if (out_state.raw_x > m_observed_max_x) m_observed_max_x = out_state.raw_x;
            if (out_state.raw_y > m_observed_max_y) m_observed_max_y = out_state.raw_y;
        }
        return true;
    } 
    else if (report_id == 0x02 && length >= 7) {
        out_state.in_proximity = true;
        out_state.tool = ToolType::Pen;
        out_state.raw_x = static_cast<uint32_t>(data[1]) | (static_cast<uint32_t>(data[2]) << 8);
        out_state.raw_y = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8);

        int8_t raw_p = static_cast<int8_t>(data[6]);
        int32_t pressure = static_cast<int32_t>(raw_p) + 127;
        if (pressure < 0) pressure = 0;
        if (pressure > 255) pressure = 255;
        out_state.raw_pressure = pressure;

        out_state.tip_switch = (raw_p > -80) && !(data[5] & 0x20);
        out_state.barrel_switch_1 = (data[5] & 0x40) != 0;
        out_state.barrel_switch_2 = false;
        out_state.eraser_switch = false;

        if (m_auto_detect_bounds) {
            if (out_state.raw_x > m_observed_max_x) m_observed_max_x = out_state.raw_x;
            if (out_state.raw_y > m_observed_max_y) m_observed_max_y = out_state.raw_y;
        }
        return true;
    }

    return false;
}

bool PacketDecoder::DecodeGraphireReport(const uint8_t* data, size_t length, TabletRawState& out_state) {
    if (data[0] != 0x02 || length < 7) {
        return false;
    }

    bool prox = (data[6] & 0x80) != 0 || (data[6] & 0x40) != 0;
    out_state.in_proximity = prox;

    if (prox) {
        out_state.raw_x = static_cast<uint32_t>(data[1]) | (static_cast<uint32_t>(data[2]) << 8);
        out_state.raw_y = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8);

        bool is_eraser = (data[6] & 0x10) != 0 || (data[5] & 0x20) != 0;
        out_state.tool = is_eraser ? ToolType::Eraser : ToolType::Pen;
        out_state.eraser_switch = is_eraser;

        int32_t pressure = static_cast<int32_t>(data[5]);
        if (length > 7) {
            pressure |= (static_cast<int32_t>(data[7] & 0x01) << 8);
        } else if (data[6] & 0x01) {
            pressure |= 0x100;
        }

        out_state.raw_pressure = pressure;
        out_state.tip_switch = (data[6] & 0x01) != 0 || (pressure > 10);
        out_state.barrel_switch_1 = (data[6] & 0x02) != 0 || (data[6] & 0x04) != 0;
        out_state.barrel_switch_2 = (data[6] & 0x08) != 0;

        if (m_auto_detect_bounds) {
            if (out_state.raw_x > m_observed_max_x) m_observed_max_x = out_state.raw_x;
            if (out_state.raw_y > m_observed_max_y) m_observed_max_y = out_state.raw_y;
            if (out_state.raw_pressure > m_observed_max_pressure) m_observed_max_pressure = out_state.raw_pressure;
        }
    } else {
        out_state.tool = ToolType::None;
        out_state.raw_pressure = 0;
        out_state.tip_switch = false;
        out_state.barrel_switch_1 = false;
        out_state.barrel_switch_2 = false;
        out_state.eraser_switch = false;
        out_state.raw_x = m_last_state.raw_x;
        out_state.raw_y = m_last_state.raw_y;
    }

    return true;
}

bool PacketDecoder::DecodeGenericReport(const uint8_t* data, size_t length, TabletRawState& out_state) {
    if (length < 6) return false;

    // Direct byte parsing assuming [X_low, X_high, Y_low, Y_high, Flags, Pressure]
    out_state.raw_x = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8);
    out_state.raw_y = static_cast<uint32_t>(data[2]) | (static_cast<uint32_t>(data[3]) << 8);
    out_state.raw_pressure = (length > 5) ? static_cast<int32_t>(data[5]) : 0;
    out_state.in_proximity = (out_state.raw_x > 0 || out_state.raw_y > 0 || out_state.raw_pressure > 0);
    out_state.tool = ToolType::Pen;
    out_state.tip_switch = (out_state.raw_pressure > 0);
    out_state.barrel_switch_1 = (length > 4) && ((data[4] & 0x40) != 0);
    out_state.barrel_switch_2 = false;
    return true;
}

} // namespace ct0405
