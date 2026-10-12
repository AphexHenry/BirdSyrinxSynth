#include "BirdKeyboard.h"

namespace syrinx::ui {

namespace {
const juce::Colour kWhiteTop{0xffefeae1};
const juce::Colour kWhiteBottom{0xffd9d3c8};
const juce::Colour kBlackTop{0xff30343b};
const juce::Colour kBlackBottom{0xff15171b};
} // namespace

BirdKeyboard::BirdKeyboard(juce::MidiKeyboardState& state, std::function<int(int)> singerForNote)
    : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard),
      singerForNote_(std::move(singerForNote)) {
    setColour(whiteNoteColourId, colours::inset); // shows between the keys
    setColour(keySeparatorLineColourId, juce::Colours::transparentBlack);
    setColour(shadowColourId, juce::Colours::transparentBlack);
    setColour(upDownButtonBackgroundColourId, colours::surface);
    setColour(upDownButtonArrowColourId, colours::text2);
    setBlackNoteLengthProportion(0.6f);
    setBlackNoteWidthProportion(0.62f);
    setScrollButtonWidth(20);
    // JUCE's ListenerList grows a small vector the first time it calls its listeners (this keyboard is one).
    // Make that first call here, on the message thread, so the audio thread never allocates for it.
    // Note 0 on channel 16: outside the keyboard, never sent to the audio (state only).
    state.processNextMidiEvent(juce::MidiMessage::noteOn(16, 0, 0.01f));
    state.processNextMidiEvent(juce::MidiMessage::noteOff(16, 0));
    setHelpText("Click or drag to play; the computer keys play too. Tap a key for one syllable; hold it and the "
                "bird keeps singing; let go after 0.6 s or more and it ends with its rising call. A pressed key has "
                "the colour of its bird in the display.");
}

juce::Colour BirdKeyboard::keyColour(int note) const {
    const int s = singerForNote_ ? singerForNote_(note) : -1;
    return s >= 0 ? singerColour(s) : colours::accent;
}

juce::String BirdKeyboard::getWhiteNoteText(int note) {
    return note % 12 == 0 ? juce::MidiMessage::getMidiNoteName(note, true, true, 4) : juce::String();
}

void BirdKeyboard::drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour, juce::Colour) {
    const auto r = area.reduced(0.75f, 0.0f).withTrimmedBottom(1.0f);
    juce::Path key;
    key.addRoundedRectangle(r.getX(), r.getY() - 6.0f, r.getWidth(), r.getHeight() + 6.0f, 4.5f, 4.5f, false, false,
                            true, true);
    if (isDown) {
        const auto c = keyColour(note);
        g.setGradientFill(juce::ColourGradient(c.interpolatedWith(kWhiteTop, 0.25f), 0.0f, r.getY(), c, 0.0f,
                                               r.getBottom(), false));
    } else {
        g.setGradientFill(juce::ColourGradient(isOver ? kWhiteTop.darker(0.04f) : kWhiteTop, 0.0f, r.getY(),
                                               isOver ? kWhiteBottom.darker(0.06f) : kWhiteBottom, 0.0f,
                                               r.getBottom(), false));
    }
    g.fillPath(key);
    // a soft shade under the top edge (the panel overhang)
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.28f), 0.0f, r.getY(),
                                           juce::Colours::transparentBlack, 0.0f, r.getY() + 9.0f, false));
    g.fillRect(r.withHeight(9.0f));
    if (!isDown) { // a lip at the bottom of each key
        g.setColour(juce::Colours::black.withAlpha(0.10f));
        g.fillRoundedRectangle(r.withTop(r.getBottom() - 5.0f), 4.0f);
    }
    const auto text = getWhiteNoteText(note);
    if (text.isNotEmpty()) {
        g.setColour(isDown ? colours::inset : juce::Colour(0xff8b857b));
        g.setFont(font(10.0f, Weight::demibold));
        g.drawText(text, r.withTrimmedBottom(7.0f), juce::Justification::centredBottom, false);
    }
}

void BirdKeyboard::drawBlackNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour) {
    const auto r = area.withTrimmedTop(-4.0f);
    juce::Path key;
    key.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.5f, 3.5f, false, false, true, true);
    {
        juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {1, 2}).drawForPath(g, key);
    }
    if (isDown) {
        const auto c = keyColour(note);
        g.setGradientFill(juce::ColourGradient(c.darker(0.2f), 0.0f, r.getY(), c, 0.0f, r.getBottom(), false));
    } else {
        g.setGradientFill(juce::ColourGradient(isOver ? kBlackTop.brighter(0.15f) : kBlackTop, 0.0f, r.getY(),
                                               kBlackBottom, 0.0f, r.getBottom(), false));
    }
    g.fillPath(key);
    if (!isDown) { // the key's top face
        const auto face = r.reduced(r.getWidth() * 0.14f, 0.0f).withTrimmedBottom(r.getHeight() * 0.12f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), 0.0f, face.getY(),
                                               juce::Colours::white.withAlpha(0.09f), 0.0f, face.getBottom(), false));
        g.fillRoundedRectangle(face, 2.5f);
    }
}

void BirdKeyboard::drawUpDownButton(juce::Graphics& g, int w, int h, bool over, bool down, bool up) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(colours::surface.brighter(over ? 0.08f : 0.0f));
    g.fillRect(r);
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    juce::Path chev;
    if (up) {
        chev.startNewSubPath(cx - 2.5f, cy - 5.0f);
        chev.lineTo(cx + 2.5f, cy);
        chev.lineTo(cx - 2.5f, cy + 5.0f);
    } else {
        chev.startNewSubPath(cx + 2.5f, cy - 5.0f);
        chev.lineTo(cx - 2.5f, cy);
        chev.lineTo(cx + 2.5f, cy + 5.0f);
    }
    g.setColour(down ? colours::accent : (over ? colours::text : colours::text2));
    g.strokePath(chev, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace syrinx::ui
