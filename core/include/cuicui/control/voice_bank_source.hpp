#pragma once

#include "cuicui/control/gesture_source.hpp"

namespace cuicui::control {

/// Stub for WP-F3: a learned voice-bank `GestureSource`. Declared, not implemented.
class VoiceBankSource : public GestureSource {
public:
    void noteOn(double, int, std::uint32_t) noexcept override {}
    void noteOff() noexcept override {}
    GestureSample next(double) noexcept override { return {}; }
};

} // namespace cuicui::control
