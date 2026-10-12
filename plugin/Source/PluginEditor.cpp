#include "PluginEditor.h"

#include "StandaloneMidi.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

using namespace syrinx::ui;

namespace {

constexpr int kMargin = 16;
const juce::Rectangle<int> kHeader{kMargin, 12, kWidth - 2 * kMargin, 44};
const juce::Rectangle<int> kPage{kMargin, 66, kWidth - 2 * kMargin, 498};
const juce::Rectangle<int> kHelp{kMargin, 568, kWidth - 2 * kMargin, 24};
const juce::Rectangle<int> kKeys{kMargin, 598, kWidth - 2 * kMargin, 112};

const char* kDefaultHelp = "Tap a key: one real syllable. Hold it: the bird sings around your note; let go after "
                           "0.6 s or more and it ends with its long rising call. Point at any control for help.";

} // namespace

SyrinxAudioProcessorEditor::SyrinxAudioProcessorEditor(SyrinxAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p),
      tabs_(juce::StringArray{"Play", "Advanced"}, [this](int page) { showPage(page); }), presetBar_(p.presets()),
      play_(p), advanced_(p),
      keyboard_(p.keyboardState, [this](int note) { return play_.display().singerForNote(note); }) {
    addAndMakeVisible(canvas_);
    canvas_.setBounds(0, 0, kWidth, kHeight);
    canvas_.setOpaque(true);
    for (juce::Component* c : {static_cast<juce::Component*>(&tabs_), static_cast<juce::Component*>(&presetBar_),
                               static_cast<juce::Component*>(&play_), static_cast<juce::Component*>(&advanced_),
                               static_cast<juce::Component*>(&keyboard_), &midiZone_}) {
        canvas_.addAndMakeVisible(c);
    }
    tabs_.setTextHeight(13.0f);
    tabs_.setHelpText("Play: the instrument. Advanced: the anatomical voice's controls (experimental).");
    midiZone_.setHelpText("Lights up when notes arrive from a MIDI keyboard or the host. In the Standalone app, MIDI "
                          "inputs are chosen under Options > Audio/MIDI Settings.");

    tabs_.setBounds(326, kHeader.getY() + 7, 180, 30);
    presetBar_.setBounds(596, kHeader.getY() + 4, 404, 36);
    midiZone_.setBounds(kHeader.getRight() - 56, kHeader.getY() + 6, 56, 32);
    play_.setBounds(kPage);
    advanced_.setBounds(kPage);
    keyboard_.setBounds(kKeys);
    keyboard_.setAvailableRange(36, 108); // C2–C8: own height takes any key; "follow" sings best from C4 to C6
    keyboard_.setOctaveForMiddleC(4);
    keyboard_.setVelocity(0.7f, true);
    keyboard_.setWantsKeyboardFocus(true);
    {
        constexpr int kWhiteKeys = 22; // C4–C7 visible (the key width first: a range that fits resets the view)
        keyboard_.setKeyWidth(static_cast<float>(kKeys.getWidth() - 40) / static_cast<float>(kWhiteKeys));
    }
    keyboard_.setLowestVisibleKey(60);
    buildLogo();
    play_.display().setBackgroundPainter([this](juce::Graphics& g) { // the canvas, seen through the corners
        const auto origin = canvas_.getLocalPoint(&play_.display(), juce::Point<int>());
        g.addTransform(juce::AffineTransform::translation(static_cast<float>(-origin.x), static_cast<float>(-origin.y)));
        canvas_.paintBackground(g);
    });

    const int page = juce::jlimit(0, 1, proc_.uiPage.load());
    tabs_.setIndex(page, false);
    showPage(page);

    setResizable(true, false); // the host's (or the Standalone window's) own frame resizes; no corner grip
    setResizeLimits(kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 3 / 2, kHeight * 3 / 2);
    if (auto* c = getConstrainer()) {
        c->setFixedAspectRatio(static_cast<double>(kWidth) / static_cast<double>(kHeight));
    }
    setSize(kWidth, kHeight);
    canvas_.help = kDefaultHelp;
    // Last: every child is attached now, so all of them (and their text boxes) take the Syrinx look.
    setLookAndFeel(&lnf_);
    startTimerHz(30);
}

SyrinxAudioProcessorEditor::~SyrinxAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void SyrinxAudioProcessorEditor::showPage(int page) {
    tabs_.setIndex(page, false);
    play_.setVisible(page == 0);
    advanced_.setVisible(page == 1);
    proc_.uiPage.store(page);
    if (page == 1) {
        advanced_.update();
    }
}

void SyrinxAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(colours::bgBottom);
}

void SyrinxAudioProcessorEditor::resized() {
    const float s = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    canvas_.setTransform(juce::AffineTransform::scale(s));
}

void SyrinxAudioProcessorEditor::buildLogo() {
    // The mark is one of the bird's own syllables: the "swipe" with the widest sweep in the bank.
    const auto& bank = proc_.syllableBank();
    int best = -1;
    double bestRange = 0.0;
    for (int i = 0; i < bank.numSyllables(); ++i) {
        if (bank.label(i).find("swipe") == std::string::npos) {
            continue;
        }
        const auto& s = bank.syllable(i);
        double lo = 1e9, hi = -1e9;
        for (int k = 0; k < s.lengthMs; ++k) {
            if (bank.amp()[s.offset + k] > 0.2f) {
                lo = std::min(lo, bank.logHz()[s.offset + k]);
                hi = std::max(hi, bank.logHz()[s.offset + k]);
            }
        }
        if (hi - lo > bestRange && s.lengthMs >= 40) {
            bestRange = hi - lo;
            best = i;
        }
    }
    juce::Path p;
    const juce::Rectangle<float> box(20.0f, 21.0f, 30.0f, 22.0f);
    if (best >= 0) {
        const auto& s = bank.syllable(best);
        double lo = 1e9, hi = -1e9;
        int k0 = -1, k1 = -1;
        for (int k = 0; k < s.lengthMs; ++k) {
            if (bank.amp()[s.offset + k] > 0.2f) {
                k0 = k0 < 0 ? k : k0;
                k1 = k;
                lo = std::min(lo, bank.logHz()[s.offset + k]);
                hi = std::max(hi, bank.logHz()[s.offset + k]);
            }
        }
        auto smooth = [&](int k) { // 9 ms moving average: the bird's line, without the tracker's grain
            double sum = 0.0;
            int n = 0;
            for (int j = std::max(k0, k - 4); j <= std::min(k1, k + 4); ++j, ++n) {
                sum += bank.logHz()[s.offset + j];
            }
            return sum / std::max(1, n);
        };
        for (int k = k0; k <= k1; k = (k == k1 ? k1 + 1 : std::min(k1, k + 2))) {
            const float x = box.getX() + box.getWidth() * static_cast<float>(k - k0) / static_cast<float>(std::max(1, k1 - k0));
            const float y = box.getBottom() - box.getHeight() * static_cast<float>((smooth(k) - lo) / std::max(1e-6, hi - lo));
            k == k0 ? p.startNewSubPath(x, y) : p.lineTo(x, y);
        }
    } else {
        p.startNewSubPath(box.getX(), box.getBottom());
        p.cubicTo(box.getX() + 8.0f, box.getY(), box.getX() + 18.0f, box.getBottom(), box.getRight(), box.getY());
    }
    canvas_.logo = p;
}

void SyrinxAudioProcessorEditor::Canvas::paint(juce::Graphics& g) {
    // The still background is an image at the screen's real resolution, so repainting (the live display
    // above it scrolls 30 times a second) costs a copy, not gradients and text.
    const float scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!background_.isValid() || std::abs(scale - backgroundScale_) > 1e-3f) {
        background_ = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * scale), juce::roundToInt(kHeight * scale),
                                  false);
        juce::Graphics bg(background_);
        bg.addTransform(juce::AffineTransform::scale(scale));
        paintBackground(bg);
        backgroundScale_ = scale;
    }
    g.drawImageTransformed(background_, juce::AffineTransform::scale(1.0f / scale));
    paintLive(g);
}

void SyrinxAudioProcessorEditor::Canvas::paintBackground(juce::Graphics& g) const {
    const auto all = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colours::bgTop, 0.0f, 0.0f, colours::bgBottom, 0.0f, all.getBottom(), false));
    g.fillAll();
    g.setGradientFill(juce::ColourGradient(colours::accent.withAlpha(0.07f), 120.0f, 0.0f,
                                           colours::accent.withAlpha(0.0f), 520.0f, 260.0f, true));
    g.fillRect(all.withHeight(320.0f));

    // the mark and the name
    g.setColour(colours::accent.withAlpha(0.22f));
    g.strokePath(logo, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colours::accent);
    g.strokePath(logo, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto nameFont = font(25.0f, Weight::demibold);
    g.setFont(nameFont);
    g.setColour(colours::text);
    g.drawText("Syrinx", juce::Rectangle<float>(60.0f, 14.0f, 120.0f, 36.0f), juce::Justification::centredLeft, false);
    const float nameW = textWidth(nameFont, "Syrinx");
    g.setColour(colours::border.brighter(0.2f));
    g.fillRect(juce::Rectangle<float>(60.0f + nameW + 12.0f, 24.0f, 1.0f, 17.0f));
    g.setFont(titleFont(9.5f));
    g.setColour(colours::text3);
    g.drawText("CUICUI INSTRUMENTS", juce::Rectangle<float>(60.0f + nameW + 25.0f, 14.0f, 200.0f, 36.0f),
               juce::Justification::centredLeft, false);
}

void SyrinxAudioProcessorEditor::Canvas::paintLive(juce::Graphics& g) const {
    // MIDI light
    const auto zone = ed_.midiZone_.getBounds().toFloat();
    const auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({zone.getX() + 18.0f, zone.getCentreY()});
    if (midiLit) {
        g.setColour(colours::accent2.withAlpha(0.3f));
        g.fillEllipse(dot.expanded(4.0f));
    }
    g.setColour(midiLit ? colours::accent2 : colours::track);
    g.fillEllipse(dot);
    g.setFont(titleFont(9.5f));
    g.setColour(midiLit ? colours::text2 : colours::text3);
    g.drawText("MIDI", zone.withTrimmedLeft(28.0f), juce::Justification::centredLeft, false);

    // help line
    const auto h = kHelp.toFloat();
    const auto icon = juce::Rectangle<float>(14.0f, 14.0f).withCentre({h.getX() + 9.0f, h.getCentreY()});
    g.setColour(colours::text3);
    g.drawEllipse(icon, 1.0f);
    g.setFont(font(10.0f, Weight::bold));
    g.drawText("?", icon.translated(0.0f, 0.5f), juce::Justification::centred, false);
    g.setFont(font(12.0f));
    g.setColour(colours::text2);
    g.drawFittedText(help, h.withTrimmedLeft(26.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}

void SyrinxAudioProcessorEditor::updateHelp() {
    juce::String text = kDefaultHelp;
    if (auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        c != nullptr && (c == this || isParentOf(c))) {
        for (auto* k = c; k != nullptr && k != this; k = k->getParentComponent()) {
            if (k->getHelpText().isNotEmpty()) {
                text = k->getHelpText();
                break;
            }
        }
    }
    if (text != canvas_.help) {
        canvas_.help = text;
        canvas_.repaint(kHelp.expanded(2));
    }
}

void SyrinxAudioProcessorEditor::timerCallback() {
    ++ticks_;
    if (!focusDone_ && isShowing()) {
        focusDone_ = true;
        keyboard_.grabKeyboardFocus();
    }
    if (!midiChecked_) {
        midiChecked_ = true;
        syrinx::enableMidiInputsInStandalone();
    }
    if (play_.isVisible()) {
        play_.update();
    } else {
        play_.display().update(); // keep reading the feed (the keyboard's colours)
        advanced_.update();
    }
    if (ticks_ % 5 == 0) {
        presetBar_.refresh();
    }
    updateHelp();

    // MIDI light: notes from outside (a MIDI keyboard, the host)
    const auto notes = proc_.feed().externalNotes();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (notes != lastNotes_) {
        lastNotes_ = notes;
        midiLitUntilMs_ = now + 180.0;
    }
    const bool lit = now < midiLitUntilMs_;
    if (lit != canvas_.midiLit) {
        canvas_.midiLit = lit;
        canvas_.repaint(midiZone_.getBounds().expanded(6));
    }
    // the keyboard takes its birds' colours once their first syllable is known
    int hash = 17;
    for (int n = 36; n <= 108; ++n) {
        if (proc_.keyboardState.isNoteOnForChannels(0xffff, n)) {
            hash = hash * 31 + n * 7 + play_.display().singerForNote(n);
        }
    }
    if (hash != keyHash_) {
        keyHash_ = hash;
        keyboard_.repaint();
    }
}

void SyrinxAudioProcessorEditor::prepareSnapshot() {
    play_.display().freezeAtClock(true);
    play_.update();
    advanced_.update();
    presetBar_.refresh();
    canvas_.help = kDefaultHelp;
}
