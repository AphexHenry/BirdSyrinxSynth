#pragma once

#include <algorithm>
#include <cmath>

/// Output safety clipper. Unity (bit-identical) below −1 dBFS; tanh knee above;
/// never exceeds 0 dBFS. WP-C1.
class SafetyLimiter {
public:
    static constexpr float kThreshold = 0.8912509381337456f; // 10^(-1/20)

    void reset() noexcept {}

    [[nodiscard]] static float processSample(float x) noexcept {
        if (!std::isfinite(x)) {
            return 0.0f;
        }
        const float ax = std::abs(x);
        if (ax <= kThreshold) {
            return x;
        }
        const float headroom = 1.0f - kThreshold;
        const float y = kThreshold + headroom * std::tanh((ax - kThreshold) / headroom);
        return std::copysign(std::min(y, 1.0f), x);
    }

    void process(float* x, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            x[i] = processSample(x[i]);
        }
    }
};
