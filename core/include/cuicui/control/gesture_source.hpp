#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace cuicui::control {

/// One control-rate sample from a gesture generator (C4/C5/F3). Hint fields use NaN = unset.
/// `side` is also NaN = unset (VoiceController then uses the jump switcher).
struct GestureSample {
    double pitchCents = 0.0;
    double ampDb = 0.0;
    double tone = 0.5;
    double side = std::numeric_limits<double>::quiet_NaN();
    bool active = false;
    double oecRatio = std::numeric_limits<double>::quiet_NaN();
    double gape = std::numeric_limits<double>::quiet_NaN();
    double secondVoiceCents = std::numeric_limits<double>::quiet_NaN();
};

class GestureSource {
public:
    virtual ~GestureSource() = default;
    virtual void noteOn(double anchorCents, int velocity, std::uint32_t seed) noexcept = 0;
    virtual void noteOff() noexcept = 0;
    virtual GestureSample next(double dtSeconds) noexcept = 0;
};

/// Default source: inactive, 0 dB, no hints. Used when Leash = 0 (C3).
class NullGestureSource final : public GestureSource {
public:
    void noteOn(double, int, std::uint32_t) noexcept override {}
    void noteOff() noexcept override {}
    GestureSample next(double) noexcept override { return {}; }
};

[[nodiscard]] inline bool hintSet(double x) noexcept { return std::isfinite(x); }

} // namespace cuicui::control
