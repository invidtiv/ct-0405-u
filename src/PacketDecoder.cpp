#include "PacketDecoder.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace ct0405 {

namespace {
// A coordinate beyond this cannot come from this hardware and signals that we
// are decoding a report we do not actually understand.
constexpr uint32_t MAX_CREDIBLE_COORD = 65535;
constexpr uint32_t COORD_SLACK_FACTOR = 4;   // generous headroom over nominal bounds
constexpr int32_t  MAX_CREDIBLE_PRESSURE = 8192;

constexpr uint8_t REPORT_ID_STATUS = 0x01;
constexpr uint8_t REPORT_ID_MOTION = 0x02;

constexpr uint8_t STATUS_IN_RANGE = 0x80;
constexpr uint8_t STATUS_BARREL_1 = 0x40;
constexpr uint8_t STATUS_ERASER   = 0x20;
constexpr uint8_t STATUS_BARREL_2 = 0x10;
} // namespace

PacketDecoder::PacketDecoder() {
    Reset();
}

void PacketDecoder::SetCapabilities(const TabletCapabilities& caps) {
    // Deliberately does not touch the observed extents: those record what the
    // hardware reported, not what we expected it to report.
    m_caps = caps;
}

void PacketDecoder::ResetObservedExtents() {
    m_observed_max_x.store(0, std::memory_order_relaxed);
    m_observed_max_y.store(0, std::memory_order_relaxed);
    m_observed_max_pressure.store(0, std::memory_order_relaxed);
}

void PacketDecoder::Reset() {
    m_last_state = TabletRawState{};
    m_last_state.timestamp_us = GetCurrentTimestampUs();
    ResetObservedExtents();
}

std::string PacketDecoder::FormatHex(const uint8_t* data, size_t length) {
    if (!data || length == 0) return "";
    std::ostringstream hex_ss;
    for (size_t i = 0; i < length; ++i) {
        hex_ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<int>(data[i]) << " ";
    }
    return hex_ss.str();
}

void PacketDecoder::NoteObservedExtents(const TabletRawState& state) {
    if (!state.in_proximity) return;

    auto raise = [](std::atomic<uint32_t>& slot, uint32_t value, uint32_t ceiling) {
        if (value > ceiling) return;
        uint32_t current = slot.load(std::memory_order_relaxed);
        while (value > current && !slot.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
        }
    };

    raise(m_observed_max_x, state.raw_x, MAX_CREDIBLE_COORD);
    raise(m_observed_max_y, state.raw_y, MAX_CREDIBLE_COORD);

    if (state.raw_pressure > 0 && state.raw_pressure <= MAX_CREDIBLE_PRESSURE) {
        int32_t current = m_observed_max_pressure.load(std::memory_order_relaxed);
        while (state.raw_pressure > current &&
               !m_observed_max_pressure.compare_exchange_weak(current, state.raw_pressure,
                                                              std::memory_order_relaxed)) {
        }
    }
}

bool PacketDecoder::IsPlausible(const TabletRawState& state) const {
    if (!state.in_proximity) return true;
    const uint32_t x_limit = std::min<uint32_t>(MAX_CREDIBLE_COORD, m_caps.max_x * COORD_SLACK_FACTOR);
    const uint32_t y_limit = std::min<uint32_t>(MAX_CREDIBLE_COORD, m_caps.max_y * COORD_SLACK_FACTOR);
    if (state.raw_x > x_limit || state.raw_y > y_limit) return false;
    if (state.raw_pressure < 0 || state.raw_pressure > MAX_CREDIBLE_PRESSURE) return false;
    return true;
}

bool PacketDecoder::DecodePacket(const uint8_t* data, size_t length, TabletRawState& out_state) {
    if (!data || length < 7) {
        return false;
    }

    out_state.timestamp_us = GetCurrentTimestampUs();
    out_state.raw_hex = FormatHex(data, length);

    // Windows sometimes prepends a zero report-id byte; skip it when the real
    // report id follows.
    const uint8_t* pData = data;
    size_t dataLen = length;
    if (data[0] == 0x00 && length > 7 &&
        (data[1] == REPORT_ID_STATUS || data[1] == REPORT_ID_MOTION)) {
        pData = data + 1;
        dataLen = length - 1;
    }

    if (!DecodePenPartnerReport(pData, dataLen, out_state)) {
        return false;
    }
    if (!IsPlausible(out_state)) {
        return false;
    }

    NoteObservedExtents(out_state);
    m_last_state = out_state;
    return true;
}

bool PacketDecoder::DecodePenPartnerReport(const uint8_t* data, size_t length, TabletRawState& out_state) {
    if (length < 7) return false;

    const uint8_t report_id = data[0];

    if (report_id == REPORT_ID_STATUS) {
        const uint8_t status = data[5];
        const bool prox = (status & STATUS_IN_RANGE) != 0;
        out_state.in_proximity = prox;

        if (prox) {
            const bool is_eraser = (status & STATUS_ERASER) != 0;
            out_state.tool = is_eraser ? ToolType::Eraser : ToolType::Pen;
            out_state.eraser_switch = is_eraser;

            out_state.raw_x = static_cast<uint32_t>(data[1]) | (static_cast<uint32_t>(data[2]) << 8);
            out_state.raw_y = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8);

            const int8_t raw_p = static_cast<int8_t>(data[6]);
            out_state.raw_pressure = std::clamp(static_cast<int32_t>(raw_p) + 127, 0, 255);

            out_state.tip_switch = (raw_p > -127);
            out_state.barrel_switch_1 = (status & STATUS_BARREL_1) != 0;
            out_state.barrel_switch_2 = (status & STATUS_BARREL_2) != 0;
        } else {
            out_state.tool = ToolType::None;
            out_state.raw_pressure = 0;
            out_state.tip_switch = false;
            out_state.barrel_switch_1 = false;
            out_state.barrel_switch_2 = false;
            out_state.eraser_switch = false;
            // Hold the last known position so the cursor does not jump to the
            // origin when the pen leaves the surface.
            out_state.raw_x = m_last_state.raw_x;
            out_state.raw_y = m_last_state.raw_y;
        }
        return true;
    }

    if (report_id == REPORT_ID_MOTION) {
        // Report 0x02 is only emitted while the tool is in range.
        out_state.in_proximity = true;
        out_state.tool = ToolType::Pen;
        out_state.raw_x = static_cast<uint32_t>(data[1]) | (static_cast<uint32_t>(data[2]) << 8);
        out_state.raw_y = static_cast<uint32_t>(data[3]) | (static_cast<uint32_t>(data[4]) << 8);

        const int8_t raw_p = static_cast<int8_t>(data[6]);
        out_state.raw_pressure = std::clamp(static_cast<int32_t>(raw_p) + 127, 0, 255);

        out_state.tip_switch = (raw_p > -80) && !(data[5] & STATUS_ERASER);
        out_state.barrel_switch_1 = (data[5] & STATUS_BARREL_1) != 0;
        out_state.barrel_switch_2 = false;
        out_state.eraser_switch = false;
        return true;
    }

    return false;
}

} // namespace ct0405
