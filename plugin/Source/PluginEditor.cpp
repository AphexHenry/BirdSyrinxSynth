#include "PluginEditor.h"

#include "cuicui/params.hpp"

#include <algorithm>

namespace {

/// WP-R1: the whistle voice's controls, shown first ("Bird").
bool isBirdControl(std::string_view id) {
    return id.rfind("voice.", 0) == 0 || id == "perf.note_rule" || id == "perf.mapping" ||
           id == "perf.first_on_note" || id == "perf.snap" || id == "perf.order" || id == "perf.pause_s" ||
           id == "perf.size_st" || id == "out.room" || id == "perf.polyphony" || id == "out.stereo_spread" ||
           id == "out.master_db";
}

juce::GroupComponent* groupForId(std::string_view id,
                                 juce::GroupComponent& bird,
                                 juce::GroupComponent& breath,
                                 juce::GroupComponent& srcL,
                                 juce::GroupComponent& srcR,
                                 juce::GroupComponent& trachea,
                                 juce::GroupComponent& oec,
                                 juce::GroupComponent& noise,
                                 juce::GroupComponent& body,
                                 juce::GroupComponent& engine,
                                 juce::GroupComponent& perform) {
    if (isBirdControl(id)) {
        return &bird;
    }
    if (id.rfind("resp.", 0) == 0) {
        return &breath;
    }
    if (id.rfind("src.L.", 0) == 0) {
        return &srcL;
    }
    if (id.rfind("src.R.", 0) == 0) {
        return &srcR;
    }
    if (id.rfind("trachea.", 0) == 0) {
        return &trachea;
    }
    if (id.rfind("oec.", 0) == 0 || id.rfind("beak.", 0) == 0) {
        return &oec;
    }
    if (id.rfind("noise.", 0) == 0) {
        return &noise;
    }
    if (id.rfind("perf.", 0) == 0) {
        return &perform;
    }
    if (id.rfind("engine.", 0) == 0 || id.rfind("test.", 0) == 0) {
        return &engine;
    }
    return &body;
}

void layoutGroup(juce::GroupComponent& box, juce::Rectangle<int> r) {
    box.setBounds(r);
}

} // namespace

SyrinxAudioProcessorEditor::SyrinxAudioProcessorEditor(SyrinxAudioProcessor& p)
    : juce::AudioProcessorEditor(p),
      proc_(p),
      keyboard_(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setResizable(true, true);
    setResizeLimits(720, 600, 1600, 1600);
    setSize(980, 900);

    telem_.setJustificationType(juce::Justification::centredLeft);
    telem_.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    addAndMakeVisible(telem_);

    keyboard_.setAvailableRange(36, 108); // whistle: any key (own height); C4–C6 for "follow"
    keyboard_.setLowestVisibleKey(60);
    keyboard_.setOctaveForMiddleC(4);
    keyboard_.setVelocity(0.7f, true);
    keyboard_.setWantsKeyboardFocus(true);
    addAndMakeVisible(keyboard_);

    bird_.box.setText("Bird (whistle voice)");
    breath_.box.setText("Breath");
    srcL_.box.setText("Source L");
    srcR_.box.setText("Source R");
    trachea_.box.setText("Trachea");
    oec_.box.setText("OEC & Beak");
    noise_.box.setText("Noise");
    body_.box.setText("Body & Output");
    engine_.box.setText("Engine");
    perform_.box.setText("Perform");

    for (auto* g : {&bird_.box, &breath_.box, &srcL_.box, &srcR_.box, &trachea_.box, &oec_.box, &noise_.box, &body_.box,
                    &engine_.box, &perform_.box}) {
        content_.addAndMakeVisible(*g);
    }

    for (const auto& spec : cuicui::kParamTable) {
        addSlider(spec, content_);
    }

    viewport_.setViewedComponent(&content_, false);
    addAndMakeVisible(viewport_);
    startTimerHz(30);
    keyboard_.grabKeyboardFocus();
}

SyrinxAudioProcessorEditor::~SyrinxAudioProcessorEditor() {
    stopTimer();
}

void SyrinxAudioProcessorEditor::addSlider(const cuicui::ParamSpec& spec, juce::Component&) {
    auto row = std::make_unique<Row>();
    row->id = spec.id;
    row->label.setText(spec.name, juce::dontSendNotification);
    row->label.setJustificationType(juce::Justification::centredLeft);

    auto* box = groupForId(spec.id, bird_.box, breath_.box, srcL_.box, srcR_.box, trachea_.box, oec_.box,
                           noise_.box, body_.box, engine_.box, perform_.box);
    box->addAndMakeVisible(row->label);

    const std::string_view id{spec.id};
    auto* param = proc_.apvts.getParameter(spec.id);
    if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
        (void)boolParam;
        row->isToggle = true;
        row->toggle.setButtonText("on");
        row->bAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, spec.id,
                                                                                              row->toggle);
        box->addAndMakeVisible(row->toggle);
    } else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param);
               choice != nullptr && isBirdControl(id)) {
        row->isCombo = true;
        row->combo.addItemList(choice->choices, 1);
        row->cAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts,
                                                                                                spec.id, row->combo);
        box->addAndMakeVisible(row->combo);
    } else if (id == "engine.oversampling" || id == "test.waveform" || id == "perf.mode" || id == "perf.scale" ||
        id == "perf.sync_division" || id == "perf.voice_bank") {
        row->isCombo = true;
        if (id == "engine.oversampling") {
            row->combo.addItemList(juce::StringArray{"auto", "1", "2", "4", "8"}, 1);
        } else if (id == "test.waveform") {
            row->combo.addItemList(juce::StringArray{"sine", "saw", "noise"}, 1);
        } else if (id == "perf.mode") {
            row->combo.addItemList(juce::StringArray{"perform", "physiology"}, 1);
        } else if (id == "perf.sync_division") {
            row->combo.addItemList(juce::StringArray{"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"}, 1);
        } else if (id == "perf.voice_bank") {
            row->combo.addItemList(juce::StringArray{"archetypes", "learned"}, 1);
        } else {
            row->combo.addItemList(juce::StringArray{"chromatic", "major", "minor", "pentatonic_major",
                                                    "pentatonic_minor", "whole_tone"},
                                   1);
        }
        row->cAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc_.apvts, spec.id, row->combo);
        box->addAndMakeVisible(row->combo);
    } else {
        row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 18);
        row->slider.setNumDecimalPlacesToDisplay(2);
        row->sAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc_.apvts, spec.id, row->slider);
        box->addAndMakeVisible(row->slider);
    }
    rows_.push_back(std::move(row));
}

void SyrinxAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1c1c1e));
    g.setColour(juce::Colour(0xfff2f2f7));
}

void SyrinxAudioProcessorEditor::resized() {
    auto r = getLocalBounds().reduced(8);
    telem_.setBounds(r.removeFromTop(56));
    keyboard_.setBounds(r.removeFromBottom(88));
    {
        constexpr int kWhiteKeysThreeOctaves = 22; // C4–C7 visible
        const float kw =
            static_cast<float>(std::max(1, keyboard_.getWidth())) / static_cast<float>(kWhiteKeysThreeOctaves);
        keyboard_.setKeyWidth(kw);
    }
    r.removeFromBottom(6);
    r.removeFromTop(4);
    viewport_.setBounds(r);

    const int colW = 460;
    const int rowH = 28;
    const int pad = 10;
    const int header = 22;

    auto placeRows = [&](juce::GroupComponent& box, const std::vector<Row*>& rows, int x, int y) {
        const int h = header + pad + static_cast<int>(rows.size()) * rowH + pad;
        box.setBounds(x, y, colW, h);
        auto inner = box.getLocalBounds().reduced(8, 6);
        inner.removeFromTop(header - 8);
        for (auto* row : rows) {
            auto line = inner.removeFromTop(rowH);
            row->label.setBounds(line.removeFromLeft(140));
            if (row->isCombo) {
                row->combo.setBounds(line.reduced(2, 4));
            } else if (row->isToggle) {
                row->toggle.setBounds(line.reduced(2, 4));
            } else {
                row->slider.setBounds(line.reduced(2, 4));
            }
        }
        return h + 8;
    };

    std::array<juce::GroupComponent*, 10> boxes = {&bird_.box, &breath_.box, &srcL_.box,   &srcR_.box,
                                                  &trachea_.box, &oec_.box, &noise_.box, &body_.box,
                                                  &engine_.box, &perform_.box};
    std::array<std::vector<Row*>, 10> grouped{};
    for (auto& row : rows_) {
        auto* parent = row->label.getParentComponent();
        for (int i = 0; i < 10; ++i) {
            if (parent == boxes[static_cast<std::size_t>(i)]) {
                grouped[static_cast<std::size_t>(i)].push_back(row.get());
                break;
            }
        }
    }

    { // the Bird group in playing order (WP-R1)
        static const std::array<std::string_view, 13> kOrder{
            {"voice.type", "voice.harmonics", "perf.note_rule", "perf.mapping", "perf.first_on_note", "perf.snap",
             "perf.order", "perf.pause_s", "perf.size_st", "perf.polyphony", "out.room", "out.stereo_spread",
             "out.master_db"}};
        auto rank = [](const Row* r) {
            for (std::size_t i = 0; i < kOrder.size(); ++i) {
                if (r->id == kOrder[i]) {
                    return static_cast<int>(i);
                }
            }
            return static_cast<int>(kOrder.size());
        };
        std::stable_sort(grouped[0].begin(), grouped[0].end(),
                         [&](const Row* a, const Row* b) { return rank(a) < rank(b); });
    }
    int yL = 8, yR = 8;
    for (int i = 0; i < 10; ++i) {
        const bool left = (i % 2) == 0;
        int& y = left ? yL : yR;
        const int x = left ? 8 : 8 + colW + 8;
        y += placeRows(*boxes[static_cast<std::size_t>(i)], grouped[static_cast<std::size_t>(i)], x, y);
    }
    content_.setSize(8 + colW + 8 + colW + 8, std::max(yL, yR) + 8);
}

void SyrinxAudioProcessorEditor::timerCallback() {
    if (proc_.whistleSelected()) {
        const auto w = proc_.whistleTelemetry();
        const auto& b = proc_.syllableBank();
        telem_.setText(juce::String::formatted("Whistle voice, bank %s (%d syllables, %d phrases)   keys singing %d   "
                                               "syllables sounding %d",
                                               b.name().c_str(), b.numSyllables(), b.numPhrases(), w.singers,
                                               w.voices),
                       juce::dontSendNotification);
        return;
    }
    const auto t = proc_.telemetrySnapshot();
    telem_.setText(juce::String::formatted(
                       "f0 %.1f Hz (target %.1f%s)   αL %.3f  βL %.3f   αR %.3f  βR %.3f   p %.3f   "
                       "side %s   level %.1f dB   resets %u",
                       static_cast<double>(t.f0_hz), static_cast<double>(t.target_hz),
                       t.folded ? " folded" : "", static_cast<double>(t.alpha_L),
                       static_cast<double>(t.beta_L), static_cast<double>(t.alpha_R),
                       static_cast<double>(t.beta_R), static_cast<double>(t.pressure),
                       t.active_side ? "R" : "L", static_cast<double>(t.level_db),
                       static_cast<unsigned>(t.nonFiniteResets)),
                   juce::dontSendNotification);
}
