#pragma once

#include <cmath>

namespace cuicui::control {

/// How a key chooses the bird's pitch (D-020).
enum class KeyMapping {
    OwnHeight = 0, ///< the key's note name, sung at the octave nearest the bird's natural height (default)
    Follow = 1     ///< the key's own pitch three octaves up, so melodies keep their shape
};

/// Target pitch (MIDI cents, 6900 = A4) for a key. Size transposes the whole bird (semitones).
/// Same arithmetic as the PROTO1 prototype (`Placement.target`), including the tie rule (lower octave).
[[nodiscard]] inline double placementTarget(int midiNote, double centreCents, KeyMapping mapping,
                                            double sizeSt) noexcept {
    if (mapping == KeyMapping::Follow) {
        return (static_cast<double>(midiNote) + 36.0 + sizeSt) * 100.0;
    }
    const int pc = ((midiNote % 12) + 12) % 12;
    const double centre = centreCents + 100.0 * sizeSt;
    double best = static_cast<double>(pc) * 100.0;
    for (int m = pc; m < 140; m += 12) {
        const double c = static_cast<double>(m) * 100.0;
        if (std::abs(c - centre) < std::abs(best - centre)) {
            best = c;
        }
    }
    return best;
}

/// Pitch shift (cents) for a syllable whose note is `anchorCents`.
/// k = 1: its note lands exactly on the target. k = 0: the bird keeps its own melody, moved so that
/// its centre lands on the target. In between: the Melody/snap control (D-020). Prototype `Placement.shift`.
[[nodiscard]] inline double placementShift(double anchorCents, double targetCents, double centreCents,
                                           double k) noexcept {
    const double natural = anchorCents + (targetCents - centreCents);
    return (1.0 - k) * natural + k * targetCents - anchorCents;
}

/// MIDI velocity (0–1) → syllable gain, as in the prototype: 0.12 + 0.28·v^1.5.
[[nodiscard]] inline double velocityGain(double velocity01) noexcept {
    const double v = velocity01 < 0.0 ? 0.0 : (velocity01 > 1.0 ? 1.0 : velocity01);
    return 0.12 + 0.28 * std::pow(v, 1.5);
}

} // namespace cuicui::control
