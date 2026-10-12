#include "AdvancedPage.h"

#include "../ParamHelp.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <array>

namespace syrinx::ui {

namespace {

constexpr std::array<const char*, 9> kGroupNames{"Breath",      "Source L", "Source R",      "Trachea", "OEC & Beak",
                                                 "Noise",       "Body & Output", "Engine", "Perform (anatomical)"};

int groupOf(std::string_view id) {
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (starts("resp.")) {
        return 0;
    }
    if (starts("src.L.")) {
        return 1;
    }
    if (starts("src.R.")) {
        return 2;
    }
    if (starts("trachea.")) {
        return 3;
    }
    if (starts("oec.") || starts("beak.")) {
        return 4;
    }
    if (starts("noise.")) {
        return 5;
    }
    if (starts("engine.") || starts("test.")) {
        return 7;
    }
    if (starts("perf.")) {
        return 8;
    }
    return 6;
}

} // namespace

bool AdvancedPage::isPlayControl(std::string_view id) {
    return id.rfind("voice.", 0) == 0 || id == "perf.note_rule" || id == "perf.mapping" || id == "perf.first_on_note" ||
           id == "perf.snap" || id == "perf.order" || id == "perf.pause_s" || id == "perf.size_st" || id == "out.room" ||
           id == "perf.polyphony" || id == "out.stereo_spread" || id == "out.master_db";
}

AdvancedPage::AdvancedPage(SyrinxAudioProcessor& proc) : proc_(proc) {
    for (const auto* name : kGroupNames) {
        auto g = std::make_unique<juce::GroupComponent>();
        g->setText(name);
        content_.addAndMakeVisible(*g);
        groups_.push_back(std::move(g));
    }
    for (const auto& spec : cuicui::kParamTable) {
        if (!isPlayControl(spec.id)) {
            addRow(spec);
        }
    }
    telem_.setFont(font(11.5f, Weight::medium, true));
    telem_.setColour(juce::Label::textColourId, colours::text3);
    telem_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(telem_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
}

AdvancedPage::~AdvancedPage() = default;

void AdvancedPage::addRow(const cuicui::ParamSpec& spec) {
    auto row = std::make_unique<Row>();
    row->id = spec.id;
    row->label.setText(spec.name, juce::dontSendNotification);
    row->label.setJustificationType(juce::Justification::centredLeft);
    row->label.setColour(juce::Label::textColourId, colours::text2);
    auto* box = groups_[static_cast<std::size_t>(groupOf(spec.id))].get();
    box->addAndMakeVisible(row->label);
    const auto help = paramHelp(spec.id);
    const juce::String helpText(juce::CharPointer_UTF8(help.data()), help.size());

    auto* param = proc_.apvts.getParameter(spec.id);
    juce::Component* control = nullptr;
    if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr) {
        row->isToggle = true;
        row->bAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.apvts, spec.id,
                                                                                              row->toggle);
        control = &row->toggle;
    } else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
        row->isCombo = true;
        row->combo.addItemList(choice->choices, 1);
        row->cAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, spec.id,
                                                                                                row->combo);
        control = &row->combo;
    } else {
        row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
        row->slider.setNumDecimalPlacesToDisplay(2);
        row->sAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc_.apvts, spec.id,
                                                                                              row->slider);
        control = &row->slider;
    }
    control->setHelpText(helpText);
    row->label.setHelpText(helpText);
    box->addAndMakeVisible(control);
    rows_.push_back(std::move(row));
}

void AdvancedPage::paint(juce::Graphics& g) {
    auto top = getLocalBounds().removeFromTop(44).toFloat();
    g.setColour(colours::accent);
    g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({top.getX() + 6.0f, top.getY() + 13.0f}));
    g.setFont(titleFont(10.5f));
    g.setColour(colours::text2);
    g.drawText("ANATOMICAL VOICE (EXPERIMENTAL)", top.withTrimmedLeft(16.0f).withHeight(26.0f),
               juce::Justification::centredLeft, false);
    g.setFont(font(12.0f));
    g.setColour(colours::text3);
    g.drawText("These controls shape the anatomical voice only (Sound > Voice on the Play page). The seeds also set "
               "the whistle's random choices.",
               top.withTrimmedLeft(16.0f).withTrimmedTop(22.0f), juce::Justification::centredLeft, true);
}

void AdvancedPage::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(44);
    telem_.setBounds(top.removeFromRight(520).withTrimmedTop(2).withHeight(22));
    r.removeFromTop(8);
    viewport_.setBounds(r);

    const int sb = viewport_.getScrollBarThickness();
    const int colGap = 12;
    const int colW = (r.getWidth() - sb - colGap - 6) / 2;
    const int rowH = 30;
    const int header = 26;
    const int pad = 6;
    std::array<std::vector<Row*>, kGroupNames.size()> grouped{};
    for (auto& row : rows_) {
        grouped[static_cast<std::size_t>(groupOf(row->id))].push_back(row.get());
    }
    auto place = [&](juce::GroupComponent& box, const std::vector<Row*>& rows, int x, int y) {
        const int h = header + pad + static_cast<int>(rows.size()) * rowH + pad;
        box.setBounds(x, y, colW, h);
        auto inner = box.getLocalBounds().reduced(14, 6);
        inner.removeFromTop(header);
        for (auto* row : rows) {
            auto line = inner.removeFromTop(rowH);
            row->label.setBounds(line.removeFromLeft(150));
            if (row->isCombo) {
                row->combo.setBounds(line.reduced(2, 3));
            } else if (row->isToggle) {
                row->toggle.setBounds(line.reduced(2, 3));
            } else {
                row->slider.setBounds(line.reduced(2, 4));
            }
        }
        return h + 12;
    };
    int yL = 0;
    int yR = 0;
    for (std::size_t i = 0; i < kGroupNames.size(); ++i) {
        if (grouped[i].empty()) {
            groups_[i]->setVisible(false);
            continue;
        }
        const bool left = yL <= yR;
        int& y = left ? yL : yR;
        y += place(*groups_[i], grouped[i], left ? 0 : colW + colGap, y);
    }
    content_.setSize(colW * 2 + colGap, std::max(yL, yR));
}

void AdvancedPage::update() {
    if (!isShowing()) {
        return;
    }
    if (proc_.whistleSelected()) {
        telem_.setText("The anatomical voice is off (the whistle is playing).", juce::dontSendNotification);
        return;
    }
    const auto t = proc_.telemetrySnapshot();
    telem_.setText(juce::String::formatted("f0 %.0f Hz (target %.0f%s)   p %.2f   side %s   level %.1f dB",
                                           static_cast<double>(t.f0_hz), static_cast<double>(t.target_hz),
                                           t.folded ? ", folded" : "", static_cast<double>(t.pressure),
                                           t.active_side ? "R" : "L", static_cast<double>(t.level_db)),
                   juce::dontSendNotification);
}

} // namespace syrinx::ui
