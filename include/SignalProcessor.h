#pragma once

#include "Common.h"
#include <cmath>

namespace ct0405 {

// Low-pass filter component for 1-Euro Filter
class LowPassFilter {
public:
    LowPassFilter() : m_has_prev(false), m_prev_val(0.0) {}

    double Filter(double value, double alpha) {
        if (!m_has_prev) {
            m_prev_val = value;
            m_has_prev = true;
            return value;
        }
        double filtered = alpha * value + (1.0 - alpha) * m_prev_val;
        m_prev_val = filtered;
        return filtered;
    }

    void Reset() {
        m_has_prev = false;
        m_prev_val = 0.0;
    }

    double GetValue() const { return m_prev_val; }

private:
    bool m_has_prev;
    double m_prev_val;
};

// 1-Euro Filter implementation for 1D signal
class OneEuroFilter {
public:
    OneEuroFilter(double min_cutoff = 1.2, double beta = 0.005, double d_cutoff = 1.0);

    void SetParameters(double min_cutoff, double beta, double d_cutoff);
    double Filter(double value, double timestamp_sec);
    void Reset();

private:
    static double ComputeAlpha(double cutoff, double dt);

    double m_min_cutoff;
    double m_beta;
    double m_d_cutoff;

    LowPassFilter m_x_filt;
    LowPassFilter m_dx_filt;
    double m_prev_time;
    bool m_initialized;
};

// Master Signal Processor handling 2D smoothing, jitter suppression, and pressure mapping
class SignalProcessor {
public:
    SignalProcessor();

    void UpdateConfig(const DriverConfig& config);
    void SetTabletCapabilities(const TabletCapabilities& caps);

    // Process raw tablet state into smoothed, calibrated state
    TabletProcessedState Process(const TabletRawState& raw);

    void Reset();

private:
    double ApplyPressureCurve(double normalized_pressure) const;
    static double EvaluateCubicBezier(double t, double p1, double p2);

    DriverConfig m_config;
    TabletCapabilities m_caps;

    OneEuroFilter m_filter_x;
    OneEuroFilter m_filter_y;
    OneEuroFilter m_filter_pressure;

    bool m_was_in_proximity = false;
};

} // namespace ct0405
