#pragma once

#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

namespace syrinx::ui {

/// WP-R4: the on-screen keyboard, drawn in the Syrinx look. A pressed key takes the colour of the bird
/// it plays (the same colour as its trace in the display). Mouse and computer keys play it; a MIDI
/// keyboard lights it.
class BirdKeyboard : public juce::MidiKeyboardComponent {
public:
    /// `singerForNote` returns the playing-mode slot of a key (its colour), or −1.
    BirdKeyboard(juce::MidiKeyboardState& state, std::function<int(int)> singerForNote);

    void drawWhiteNote(int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                       juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote(int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                       juce::Colour noteFillColour) override;
    void drawUpDownButton(juce::Graphics&, int w, int h, bool isMouseOver, bool isButtonPressed,
                          bool movesOctavesUp) override;
    juce::String getWhiteNoteText(int note) override;

private:
    [[nodiscard]] juce::Colour keyColour(int note) const;
    std::function<int(int)> singerForNote_;
};

} // namespace syrinx::ui
