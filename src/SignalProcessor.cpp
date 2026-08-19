#include "SignalProcessor.h"
#include <algorithm>

namespace ct0405 {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

OneEuroFilter::OneEuroFilter(double min_cutoff, double beta, double d_cutoff)
    : m_min_cutoff(min_cutoff), m_beta(beta), m_d_cutoff(d_cutoff), m_prev_time(0.0), m_initialized(false) {
}

void OneEuroFilter::SetParameters(double min_cutoff, double beta, double d_cutoff) {
    m_min_cutoff = min_cutoff;
    m_beta = beta;
    m_d_cutoff = d_cutoff;
}

double OneEuroFilter::ComputeAlpha(double cutoff, double dt) {
    double tau = 1.0 / (2.0 * M_PI * cutoff);
    return 1.0 / (1.0 + tau / dt);
}

double OneEuroFilter::Filter(double value, double timestamp_sec) {
    if (!m_initialized) {
        m_prev_time = timestamp_sec;
        m_initialized = true;
        return m_x_filt.Filter(value, 1.0);
    }

    double dt = timestamp_sec - m_prev_time;
    if (dt <= 0.0 || dt > 1.0) {
        dt = 1.0 / 120.0; // Fallback to standard 120Hz report interval
    }
    m_prev_time = timestamp_sec;

    // Estimate derivative
    double dx = (value - m_x_filt.GetValue()) / dt;
    double edx = m_dx_filt.Filter(dx, ComputeAlpha(m_d_cutoff, dt));

    // Dynamic cutoff frequency
    double cutoff = m_min_cutoff + m_beta * std::abs(edx);
    return m_x_filt.Filter(value, ComputeAlpha(cutoff, dt));
}

void OneEuroFilter::Reset() {
    m_initialized = false;
    m_prev_time = 0.0;
    m_x_filt.Reset();
    m_dx_filt.Reset();
}

SignalProcessor::SignalProcessor() {
    Reset();
}

void SignalProcessor::UpdateConfig(const DriverConfig& config) {
    m_config = config;
    m_filter_x.SetParameters(config.filter_min_cutoff, config.filter_beta, config.filter_d_cutoff);
    m_filter_y.SetParameters(config.filter_min_cutoff, config.filter_beta, config.filter_d_cutoff);
    m_filter_pressure.SetParameters(2.0, 0.01, 1.0);
}

void SignalProcessor::SetTabletCapabilities(const TabletCapabilities& caps) {
    m_caps = caps;
}

void SignalProcessor::Reset() {
    m_filter_x.Reset();
    m_filter_y.Reset();
    m_filter_pressure.Reset();
    m_was_in_proximity = false;
}

TabletProcessedState SignalProcessor::Process(const TabletRawState& raw) {
    TabletProcessedState processed;
    processed.in_proximity = raw.in_proximity;
    processed.tool = raw.tool;
    processed.timestamp_us = raw.timestamp_us;
    processed.barrel_button_1 = raw.barrel_switch_1;
    processed.barrel_button_2 = raw.barrel_switch_2;
    processed.eraser_active = (raw.tool == ToolType::Eraser || raw.eraser_switch);

    if (!raw.in_proximity) {
        m_was_in_proximity = false;
        processed.pressure = 0.0;
        processed.injection_pressure = 0;
        processed.is_contact = false;
        Reset();
        return processed;
    }

    double timestamp_sec = static_cast<double>(raw.timestamp_us) / 1000000.0;

    // Reset filters on fresh proximity entry to prevent dragging from old location
    if (!m_was_in_proximity) {
        Reset();
        m_was_in_proximity = true;
    }

    // Normalize coordinates (0.0 to 1.0)
    uint32_t max_x = m_config.tablet_max_x ? m_config.tablet_max_x : (m_caps.max_x ? m_caps.max_x : 5040);
    uint32_t max_y = m_config.tablet_max_y ? m_config.tablet_max_y : (m_caps.max_y ? m_caps.max_y : 3780);
    uint32_t max_p = m_config.tablet_max_pressure ? m_config.tablet_max_pressure : (m_caps.max_pressure ? m_caps.max_pressure : 255);

    double norm_x = static_cast<double>(raw.raw_x) / static_cast<double>(max_x);
    double norm_y = static_cast<double>(raw.raw_y) / static_cast<double>(max_y);

    norm_x = std::clamp(norm_x, 0.0, 1.0);
    norm_y = std::clamp(norm_y, 0.0, 1.0);

    // Apply smoothing filter if enabled
    if (m_config.enable_smoothing) {
        norm_x = m_filter_x.Filter(norm_x, timestamp_sec);
        norm_y = m_filter_y.Filter(norm_y, timestamp_sec);
    }

    processed.normalized_x = std::clamp(norm_x, 0.0, 1.0);
    processed.normalized_y = std::clamp(norm_y, 0.0, 1.0);

    // Normalize raw pressure
    double raw_norm_pressure = static_cast<double>(raw.raw_pressure) / static_cast<double>(max_p);
    raw_norm_pressure = std::clamp(raw_norm_pressure, 0.0, 1.0);

    // Apply Deadzones
    double final_pressure = 0.0;
    if (raw_norm_pressure > m_config.pressure_min_threshold) {
        double range = m_config.pressure_max_threshold - m_config.pressure_min_threshold;
        if (range > 0.01) {
            double normalized_in_range = (raw_norm_pressure - m_config.pressure_min_threshold) / range;
            final_pressure = std::clamp(normalized_in_range, 0.0, 1.0);
        } else {
            final_pressure = 1.0;
        }

        // Apply Pressure Curve
        final_pressure = ApplyPressureCurve(final_pressure);
    }

    processed.pressure = final_pressure;
    // Scale for Windows Ink injection (0..1024 standard resolution)
    processed.injection_pressure = static_cast<uint32_t>(final_pressure * 1024.0);
    processed.is_contact = (final_pressure > 0.001) || raw.tip_switch;

    return processed;
}

double SignalProcessor::ApplyPressureCurve(double p) const {
    p = std::clamp(p, 0.0, 1.0);
    switch (m_config.curve_type) {
        case PressureCurveType::Linear:
            return p;
        case PressureCurveType::Soft:
            return std::pow(p, 0.65);
        case PressureCurveType::VerySoft:
            return std::pow(p, 0.45);
        case PressureCurveType::Firm:
            return std::pow(p, 1.55);
        case PressureCurveType::Hard:
            return std::pow(p, 2.2);
        case PressureCurveType::CustomBezier:
            return EvaluateCubicBezier(p, m_config.custom_bezier_p1, m_config.custom_bezier_p2);
        default:
            return p;
    }
}

double SignalProcessor::EvaluateCubicBezier(double t, double p1, double p2) {
    // 1D cubic Bezier with endpoints at 0 and 1: B(t) = 3(1-t)^2 * t * p1 + 3(1-t) * t^2 * p2 + t^3
    double u = 1.0 - t;
    double tt = t * t;
    double uu = u * u;
    double ttt = tt * t;

    double res = 3.0 * uu * t * p1 + 3.0 * u * tt * p2 + ttt;
    return std::clamp(res, 0.0, 1.0);
}

} // namespace ct0405
