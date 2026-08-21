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
    // Guard against a config that would divide by zero or invert the filter.
    m_min_cutoff = (min_cutoff > 0.0001) ? min_cutoff : 0.0001;
    m_beta = beta;
    m_d_cutoff = (d_cutoff > 0.0001) ? d_cutoff : 0.0001;
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

void SignalProcessor::Reset() {
    m_filter_x.Reset();
    m_filter_y.Reset();
    m_was_in_proximity = false;
}

TabletProcessedState SignalProcessor::Process(const TabletRawState& raw,
                                              const DriverConfig& config,
                                              const TabletCapabilities& caps) {
    TabletProcessedState processed;
    processed.in_proximity = raw.in_proximity;
    processed.tool = raw.tool;
    processed.timestamp_us = raw.timestamp_us;
    processed.barrel_button_1 = raw.barrel_switch_1;
    processed.barrel_button_2 = raw.barrel_switch_2;
    processed.eraser_active = (raw.tool == ToolType::Eraser || raw.eraser_switch);

    if (!raw.in_proximity) {
        processed.pressure = 0.0;
        processed.raw_normalized_pressure = 0.0;
        processed.injection_pressure = 0;
        processed.is_contact = false;
        Reset();
        return processed;
    }

    const double timestamp_sec = static_cast<double>(raw.timestamp_us) / 1000000.0;

    // Reset filters on fresh proximity entry to prevent dragging from old location
    if (!m_was_in_proximity) {
        Reset();
        m_was_in_proximity = true;
    }

    m_filter_x.SetParameters(config.filter_min_cutoff, config.filter_beta, config.filter_d_cutoff);
    m_filter_y.SetParameters(config.filter_min_cutoff, config.filter_beta, config.filter_d_cutoff);

    // Effective bounds are resolved by the driver (device detection, user
    // override, or auto-expansion) and arrive here already decided.
    const uint32_t max_x = caps.max_x ? caps.max_x : CT0405U_MAX_X;
    const uint32_t max_y = caps.max_y ? caps.max_y : CT0405U_MAX_Y;
    const uint32_t max_p = caps.max_pressure ? caps.max_pressure : CT0405U_MAX_PRESSURE;

    // Normalize across the measured span, not from an assumed zero origin.
    const uint32_t min_x = (caps.min_x < max_x) ? caps.min_x : 0;
    const uint32_t min_y = (caps.min_y < max_y) ? caps.min_y : 0;
    const double span_x = static_cast<double>(max_x - min_x);
    const double span_y = static_cast<double>(max_y - min_y);

    double norm_x = (static_cast<double>(raw.raw_x) - static_cast<double>(min_x)) / span_x;
    double norm_y = (static_cast<double>(raw.raw_y) - static_cast<double>(min_y)) / span_y;

    norm_x = std::clamp(norm_x, 0.0, 1.0);
    norm_y = std::clamp(norm_y, 0.0, 1.0);

    // Apply smoothing filter if enabled
    if (config.enable_smoothing) {
        norm_x = m_filter_x.Filter(norm_x, timestamp_sec);
        norm_y = m_filter_y.Filter(norm_y, timestamp_sec);
    }

    processed.normalized_x = std::clamp(norm_x, 0.0, 1.0);
    processed.normalized_y = std::clamp(norm_y, 0.0, 1.0);

    // Normalize raw pressure
    double raw_norm_pressure = static_cast<double>(raw.raw_pressure) / static_cast<double>(max_p);
    raw_norm_pressure = std::clamp(raw_norm_pressure, 0.0, 1.0);
    processed.raw_normalized_pressure = raw_norm_pressure;

    // Apply deadzone
    const double min_threshold = std::clamp(config.pressure_min_threshold, 0.0, 1.0);
    const double max_threshold = std::clamp(config.pressure_max_threshold, 0.0, 1.0);
    const bool above_deadzone = (min_threshold <= 0.0) || (raw_norm_pressure > min_threshold);

    double final_pressure = 0.0;
    if (above_deadzone) {
        const double range = max_threshold - min_threshold;
        if (range > 0.01) {
            const double normalized_in_range = (raw_norm_pressure - min_threshold) / range;
            final_pressure = std::clamp(normalized_in_range, 0.0, 1.0);
        } else {
            final_pressure = 1.0;
        }

        final_pressure = ApplyPressureCurve(final_pressure, config);
    }

    processed.pressure = final_pressure;
    // Scale for Windows Ink injection (0..1024 standard resolution)
    processed.injection_pressure = static_cast<uint32_t>(std::lround(final_pressure * 1024.0));

    // The deadzone gates contact, not just the reported pressure value.
    // Previously the hardware tip switch was OR'd in here, and for this family
    // that switch trips at the lightest possible touch - so the deadzone slider
    // attenuated the pressure curve but never suppressed the click, which is
    // the thing users reach for that control to fix.
    processed.is_contact = raw.tip_switch && above_deadzone;

    return processed;
}

double SignalProcessor::ApplyPressureCurve(double p, const DriverConfig& config) {
    p = std::clamp(p, 0.0, 1.0);
    switch (config.curve_type) {
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
            return EvaluateCubicBezier(p, config.custom_bezier_p1, config.custom_bezier_p2);
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
