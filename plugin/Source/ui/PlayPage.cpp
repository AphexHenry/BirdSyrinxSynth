#include "PlayPage.h"

#include "../ParamHelp.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace syrinx::ui {

namespace {

const juce::String kMinus{juce::CharPointer_UTF8("\xe2\x88\x92")}; // a real minus sign

juce::String signedText(double v, int decimals, const juce::String& unit) {
    const double a = std::abs(v);
    const auto num = decimals == 0 ? juce::String(static_cast<int>(std::lround(a))) : juce::String(a, decimals);
    if (std::lround(v * std::pow(10.0, decimals)) == 0) {
        return "0" + unit;
    }
    return (v < 0.0 ? kMinus : juce::String("+")) + num + unit;
}

void setHelp(juce::Component& c, const char* paramId) {
    c.setHelpText(juce::String(juce::CharPointer_UTF8(paramHelp(paramId).data()), paramHelp(paramId).size()));
}

void styleHint(juce::Label& l) {
    l.setFont(font(11.0f));
    l.setColour(juce::Label::textColourId, colours::text3);
    l.setJustificationType(juce::Justification::topLeft);
    l.setMinimumHorizontalScale(1.0f);
    l.setInterceptsMouseClicks(false, false);
    l.setBorderSize({0, 1, 0, 0});
}

void styleCaption(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(font(12.5f, Weight::medium));
    l.setColour(juce::Label::textColourId, colours::text2);
    l.setJustificationType(juce::Justification::centredLeft);
    l.setInterceptsMouseClicks(false, false);
    l.setBorderSize({0, 1, 0, 0});
}

} // namespace

PlayPage::PlayPage(SyrinxAudioProcessor& proc) : proc_(proc), display_(proc) {
    auto& apvts = proc.apvts;
    addAndMakeVisible(display_);
    for (auto* c : {&melody_, &height_, &song_, &flock_, &sound_}) {
        addAndMakeVisible(*c);
    }

    // Melody
    melodyKnob_ = std::make_unique<Knob>(
        apvts, "perf.snap", "Snap to note", [](double v) { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; },
        "natural", "on note");
    firstOnNote_ = std::make_unique<Switch>(apvts, "perf.first_on_note", "First syllable on the note");
    noteRule_ = std::make_unique<ChoiceBox>(apvts, "perf.note_rule", "A syllable's note is its",
                                            juce::StringArray{"longest steady part", "steady parts", "landing", "average"});
    setHelp(*melodyKnob_, "perf.snap");
    setHelp(*firstOnNote_, "perf.first_on_note");
    setHelp(*noteRule_, "perf.note_rule");
    for (juce::Component* c : {static_cast<juce::Component*>(melodyKnob_.get()), static_cast<juce::Component*>(firstOnNote_.get()),
                               static_cast<juce::Component*>(noteRule_.get())}) {
        melody_.addAndMakeVisible(c);
    }

    // Height
    sizeKnob_ = std::make_unique<Knob>(
        apvts, "perf.size_st", "Size", [](double v) { return signedText(v, 0, " st"); }, "bigger", "smaller", true);
    mapping_ = std::make_unique<Segmented>(apvts, "perf.mapping", juce::StringArray{"Own height", "Follow keys"});
    styleCaption(mappingCaption_, "Keyboard");
    styleHint(heightHint_);
    setHelp(*sizeKnob_, "perf.size_st");
    setHelp(*mapping_, "perf.mapping");
    for (juce::Component* c : {static_cast<juce::Component*>(sizeKnob_.get()), static_cast<juce::Component*>(&mappingCaption_),
                               static_cast<juce::Component*>(mapping_.get()), static_cast<juce::Component*>(&heightHint_)}) {
        height_.addAndMakeVisible(c);
    }

    // Song
    pauseKnob_ = std::make_unique<Knob>(
        apvts, "perf.pause_s", "Pause", [](double v) { return juce::String(v, 1) + " s"; }, "0.1 s", "2 s");
    order_ = std::make_unique<Segmented>(apvts, "perf.order", juce::StringArray{"Random", "Phrase", "Shuffle"});
    styleCaption(orderCaption_, "Each key starts");
    styleHint(songHint_);
    setHelp(*pauseKnob_, "perf.pause_s");
    setHelp(*order_, "perf.order");
    for (juce::Component* c : {static_cast<juce::Component*>(pauseKnob_.get()), static_cast<juce::Component*>(&orderCaption_),
                               static_cast<juce::Component*>(order_.get()), static_cast<juce::Component*>(&songHint_)}) {
        song_.addAndMakeVisible(c);
    }

    // Flock
    birdsKnob_ = std::make_unique<Knob>(
        apvts, "perf.polyphony", "Birds", [](double v) { return juce::String(static_cast<int>(std::lround(v))); }, "1",
        "16");
    spread_ = std::make_unique<MiniSlider>(apvts, "out.stereo_spread", "Spread",
                                           [](double v) { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; });
    styleHint(flockHint_);
    flockHint_.setText("Each held key is one bird, with its own place left to right.", juce::dontSendNotification);
    setHelp(*birdsKnob_, "perf.polyphony");
    setHelp(*spread_, "out.stereo_spread");
    for (juce::Component* c : {static_cast<juce::Component*>(birdsKnob_.get()), static_cast<juce::Component*>(spread_.get()),
                               static_cast<juce::Component*>(&flockHint_)}) {
        flock_.addAndMakeVisible(c);
    }

    // Sound
    volumeKnob_ = std::make_unique<Knob>(
        apvts, "out.master_db", "Volume",
        [](double v) { return std::abs(v) >= 9.95 ? signedText(v, 0, " dB") : signedText(v, 1, " dB"); },
        kMinus + "48", "+6");
    voice_ = std::make_unique<Segmented>(apvts, "voice.type", juce::StringArray{"Whistle", "Anatomical"});
    harmonics_ = std::make_unique<Switch>(apvts, "voice.harmonics", "Bird harmonics");
    room_ = std::make_unique<Switch>(apvts, "out.room", "Room");
    setHelp(*volumeKnob_, "out.master_db");
    setHelp(*voice_, "voice.type");
    setHelp(*harmonics_, "voice.harmonics");
    setHelp(*room_, "out.room");
    for (juce::Component* c : {static_cast<juce::Component*>(volumeKnob_.get()), static_cast<juce::Component*>(voice_.get()),
                               static_cast<juce::Component*>(harmonics_.get()), static_cast<juce::Component*>(room_.get())}) {
        sound_.addAndMakeVisible(c);
    }

    melody_.setHelpText(juce::String(juce::CharPointer_UTF8(paramHelp("perf.snap").data()), paramHelp("perf.snap").size()));
    updateHints();
}

PlayPage::~PlayPage() = default;

void PlayPage::resized() {
    auto r = getLocalBounds();
    display_.setBounds(r.removeFromTop(210));
    r.removeFromTop(12);
    const int gap = 12;
    const int w = (r.getWidth() - 4 * gap) / 5;
    int x = r.getX();
    for (auto* c : {&melody_, &height_, &song_, &flock_, &sound_}) {
        c->setBounds(x, r.getY(), w, r.getHeight());
        x += w + gap;
    }
    // inside each card (card coordinates): a knob on top, then its secondary controls
    const int cx = 14;
    const int cw = w - 28;
    const juce::Rectangle<int> knob(cx, 38, cw, 118);
    melodyKnob_->setBounds(knob);
    firstOnNote_->setBounds(cx, 166, cw, 22);
    noteRule_->setBounds(cx, 198, cw, 44);

    sizeKnob_->setBounds(knob);
    mappingCaption_.setBounds(cx, 164, cw, 16);
    mapping_->setBounds(cx, 182, cw, 28);
    heightHint_.setBounds(cx, 216, cw, 36);

    pauseKnob_->setBounds(knob);
    orderCaption_.setBounds(cx, 164, cw, 16);
    order_->setBounds(cx, 182, cw, 28);
    songHint_.setBounds(cx, 216, cw, 36);

    birdsKnob_->setBounds(knob);
    spread_->setBounds(cx, 168, cw, 38);
    flockHint_.setBounds(cx, 216, cw, 36);

    volumeKnob_->setBounds(knob);
    voice_->setBounds(cx, 166, cw, 28);
    harmonics_->setBounds(cx, 204, cw, 22);
    room_->setBounds(cx, 232, cw, 22);
}

void PlayPage::updateHints() {
    const int mapping = static_cast<int>(proc_.apvts.getRawParameterValue("perf.mapping")->load());
    if (mapping != shownMapping_) {
        shownMapping_ = mapping;
        heightHint_.setText(mapping == 1 ? "The bird sings three octaves above your key, so tunes keep their shape."
                                         : "Each key sings its note name at the bird's own height.",
                            juce::dontSendNotification);
    }
    const int order = static_cast<int>(proc_.apvts.getRawParameterValue("perf.order")->load());
    if (order != shownOrder_) {
        shownOrder_ = order;
        songHint_.setText(order == 1   ? "From the start of a real phrase, then the phrases in order."
                          : order == 2 ? "Syllables in random order, without the real phrases."
                                       : "On a random syllable, then the real phrase goes on.",
                          juce::dontSendNotification);
    }
}

void PlayPage::update() {
    display_.update();
    updateHints();
    const bool whistle = proc_.whistleSelected();
    if (whistle != whistle_) { // the whistle's controls do nothing for the anatomical voice
        whistle_ = whistle;
        for (auto* c : {static_cast<juce::Component*>(&melody_), static_cast<juce::Component*>(&height_),
                        static_cast<juce::Component*>(&song_), static_cast<juce::Component*>(harmonics_.get()),
                        static_cast<juce::Component*>(room_.get())}) {
            c->setEnabled(whistle);
        }
    }
    melodyKnob_->setModAmount(whistle ? proc_.feed().modWheel() : 0.0f);
}

} // namespace syrinx::ui
