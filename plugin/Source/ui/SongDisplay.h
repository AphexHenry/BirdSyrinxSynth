#pragma once

#include "../SyllableFeed.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

class SyrinxAudioProcessor;

namespace syrinx::ui {

/// WP-R4: "now singing". The last seconds of song as a sonogram-like trace: each syllable's real pitch
/// curve (bank contour + its shift) drawn as it plays, over a piano-roll grid, one colour per key; the
/// held keys' target notes as lines; chips naming the syllables that sound now.
/// Fed by SyllableFeed (lock-free); update() runs on the message thread and repaints only when needed.
class SongDisplay : public juce::Component {
public:
    explicit SongDisplay(SyrinxAudioProcessor& proc);

    /// Message thread, ~30 Hz: read new syllable starts, advance time, repaint if something moves.
    void update();
    /// Draw at an exact time (offline screenshots): no interpolation from the wall clock.
    void freezeAtClock(bool freeze) noexcept { frozen_ = freeze; }
    /// The playing-mode slot of a key that sang recently (its colour), or −1.
    [[nodiscard]] int singerForNote(int note) const noexcept;
    /// Paints what lies behind the display (its rounded corners), in display coordinates. With it, the
    /// display is opaque and nothing under it repaints while the song scrolls.
    void setBackgroundPainter(std::function<void(juce::Graphics&)> painter);

    void paint(juce::Graphics&) override;

    static constexpr double kWindowS = 4.0;

private:
    struct Item {
        SyllableFeed::Event e;
        double endSample = 0.0;
        std::vector<double> logHz; ///< WP-R3: the varied curve (empty = the bank's real syllable)
        std::vector<float> amp;
    };

    [[nodiscard]] double currentSample() const;
    [[nodiscard]] float yFor(double cents, juce::Rectangle<float> plot) const noexcept;
    [[nodiscard]] double naturalCentre() const;
    void updateAxis(double dt);
    void paintGrid(juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintStatic(juce::Graphics&, juce::Rectangle<float> r, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    void paintBands(juce::Graphics&, juce::Rectangle<float> plot, bool tags) const;
    void paintTraces(juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintChips(juce::Graphics&, juce::Rectangle<float> area) const;

    SyrinxAudioProcessor& proc_;
    std::vector<Item> items_;
    std::uint32_t epoch_ = 0;
    double now_ = 0.0;           // samples, whistle clock
    double sr_ = 48000.0;
    double axisCentre_ = 8400.0; // MIDI cents at the middle of the plot
    double lastUpdateMs_ = 0.0;
    bool frozen_ = false;
    bool wasActive_ = true;
    std::uint64_t lastChips_ = 0; // which syllables the chips show (+ pedal): repaint the chip row only on change
    std::array<int, 128> singerOfNote_{};
    std::array<double, 128> noteSeenAt_{};
    std::array<float, 128> targetOfNote_{};
    // the still layer (panel, piano roll, labels), drawn once per size / scale / axis position
    std::function<void(juce::Graphics&)> paintBehind_;
    mutable std::map<juce::String, float> tagWidths_; // text widths, measured once (shaping text is slow)
    juce::Image staticLayer_;
    float staticScale_ = 0.0f;
    double staticCentre_ = -1.0;
    static constexpr double kSpanCents = 3000.0; // 2.5 octaves
};

} // namespace syrinx::ui
