#include "PresetBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace syrinx::ui {

namespace {

/// Dims the window behind a dialog.
class Scrim : public juce::Component {
public:
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(0.45f)); }
};

/// Show a dialog inside the plugin window (a child of the editor, so it stays with it in any host).
/// The first button is the main action (drawn in the accent colour).
void showInEditor(juce::Component& from, juce::AlertWindow* w, std::function<void(int)> done) {
    auto* host = from.findParentComponentOfClass<juce::AudioProcessorEditor>();
    juce::Component* parent = host != nullptr ? static_cast<juce::Component*>(host) : from.getTopLevelComponent();
    auto scrim = std::make_shared<juce::Component::SafePointer<Scrim>>(new Scrim());
    parent->addAndMakeVisible(scrim->getComponent());
    scrim->getComponent()->setBounds(parent->getLocalBounds());
    w->setLookAndFeel(&from.getLookAndFeel());
    w->setEscapeKeyCancels(true);
    if (w->getNumButtons() > 1) {
        w->getButton(0)->setToggleState(true, juce::dontSendNotification);
    }
    parent->addAndMakeVisible(w);
    w->setCentrePosition(parent->getLocalBounds().getCentre());
    w->enterModalState(true, juce::ModalCallbackFunction::create([done = std::move(done), scrim](int r) {
                           delete scrim->getComponent();
                           done(r);
                       }),
                       true);
}

} // namespace

PresetBar::PresetBar(PresetManager& presets) : presets_(presets) {
    addAndMakeVisible(prev_);
    addAndMakeVisible(name_);
    addAndMakeVisible(next_);
    addAndMakeVisible(save_);
    prev_.onClick = [this] {
        presets_.step(-1);
        refresh();
    };
    next_.onClick = [this] {
        presets_.step(+1);
        refresh();
    };
    save_.onClick = [this] { askName(); };
    prev_.setHelpText("Previous preset.");
    next_.setHelpText("Next preset.");
    save_.setHelpText("Save the current sound as your own preset.");
    refresh();
}

PresetBar::~PresetBar() {
    if (dialog_ != nullptr) { // the editor closes while a dialog is open: dismiss it (it then deletes itself)
        dialog_->exitModalState(0);
    }
}

void PresetBar::resized() {
    auto r = getLocalBounds();
    save_.setBounds(r.removeFromRight(58).reduced(0, 3));
    r.removeFromRight(8);
    prev_.setBounds(r.removeFromLeft(28));
    next_.setBounds(r.removeFromRight(28));
    name_.setBounds(r.reduced(4, 0));
}

void PresetBar::paint(juce::Graphics&) {}

void PresetBar::refresh() {
    const auto name = presets_.currentName();
    const bool modified = presets_.isModified();
    const juce::String category = presets_.currentIsUser() ? "Yours" : ""; // a tag for your own presets only
    juce::String description;
    if (const auto* p = presets_.at(presets_.indexOfCurrent())) {
        description = p->description;
    }
    if (name != shownName_ || modified != shownModified_ || category != shownCategory_) {
        shownName_ = name;
        shownModified_ = modified;
        shownCategory_ = category;
        name_.setHelpText((description.isNotEmpty() ? description + "  " : juce::String()) +
                          "(Click for the list of presets. A dot after the name: you changed the sound since.)");
        name_.repaint();
    }
}

void PresetBar::Arrow::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat().reduced(1.0f, 4.0f);
    g.setColour(colours::inset);
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(over ? colours::text3 : colours::insetBorder);
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    juce::Path chev;
    const float s = right_ ? 1.0f : -1.0f;
    chev.startNewSubPath(cx - 2.0f * s, cy - 4.5f);
    chev.lineTo(cx + 2.5f * s, cy);
    chev.lineTo(cx - 2.0f * s, cy + 4.5f);
    g.setColour(down ? colours::accent : (over ? colours::text : colours::text2));
    g.strokePath(chev, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PresetBar::NameBox::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f, 3.5f);
    const bool over = isMouseOver(true);
    g.setColour(colours::inset);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(over ? colours::text3 : colours::insetBorder);
    g.drawRoundedRectangle(r, 8.0f, 1.0f);
    auto inner = r.reduced(12.0f, 0.0f);
    // category, small capitals
    const auto cat = bar_.shownCategory_.toUpperCase();
    const auto fc = titleFont(9.5f);
    g.setFont(fc);
    g.setColour(colours::text3);
    const float cw = cat.isEmpty() ? 0.0f : textWidth(fc, cat) + 10.0f;
    g.drawText(cat, inner.withWidth(cw), juce::Justification::centredLeft, false);
    inner.removeFromLeft(cw);
    // chevron
    const float cx = inner.getRight() - 4.0f;
    const float cy = inner.getCentreY();
    juce::Path chev;
    chev.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chev.lineTo(cx, cy + 2.2f);
    chev.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(colours::text2);
    g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    inner.removeFromRight(16.0f);
    // name (+ a dot when the sound was changed)
    const auto fn = font(13.5f, Weight::demibold);
    g.setFont(fn);
    g.setColour(colours::text);
    const float nw = std::min(inner.getWidth() - 12.0f, textWidth(fn, bar_.shownName_));
    g.drawText(bar_.shownName_, inner.withWidth(nw), juce::Justification::centredLeft, true);
    if (bar_.shownModified_) {
        g.setColour(colours::accent2);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({inner.getX() + nw + 8.0f, cy}));
    }
}

void PresetBar::showMenu() {
    presets_.rescanUser();
    juce::PopupMenu m;
    const int current = presets_.indexOfCurrent();
    juce::String section;
    for (int i = 0; i < presets_.size(); ++i) {
        const auto* p = presets_.at(i);
        const auto cat = p->factory ? p->category : juce::String("Your presets");
        if (cat != section) {
            m.addSectionHeader(cat);
            section = cat;
        }
        m.addItem(i + 1, p->name, true, i == current);
    }
    if (presets_.user().empty()) {
        m.addSectionHeader("Your presets");
        m.addItem(-1, "None yet: use Save", false, false);
    }
    m.addSeparator();
    m.addItem(10001, "Save as new preset...");
    m.addItem(10002, "Show the presets folder");
    juce::Component::SafePointer<PresetBar> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&name_)
                        .withMinimumWidth(name_.getWidth())
                        .withStandardItemHeight(26),
                    [safe](int r) {
                        if (safe == nullptr || r == 0) {
                            return;
                        }
                        if (r == 10001) {
                            safe->askName();
                        } else if (r == 10002) {
                            auto folder = safe->presets_.userFolder();
                            folder.createDirectory();
                            folder.revealToUser();
                        } else if (r > 0) {
                            safe->presets_.loadIndex(r - 1);
                            safe->refresh();
                        }
                    });
}

void PresetBar::askName() {
    auto* w = new juce::AlertWindow("Save preset", "Save the current sound as your own preset.",
                                    juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", presets_.currentIsUser() ? presets_.currentName() : juce::String(), "Name");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe(this);
    juce::Component::SafePointer<juce::AlertWindow> win(w);
    dialog_ = w;
    showInEditor(*this, w, [safe, win](int r) {
        if (r != 1 || safe == nullptr || win == nullptr) {
            return;
        }
        safe->save(win->getTextEditorContents("name"), false);
    });
    if (auto* ed = w->getTextEditor("name")) {
        ed->grabKeyboardFocus();
    }
}

void PresetBar::save(const juce::String& name, bool overwrite) {
    juce::String error;
    if (presets_.saveUser(name, {}, overwrite, error)) {
        refresh();
        return;
    }
    juce::Component::SafePointer<PresetBar> safe(this);
    if (!overwrite && name.trim().isNotEmpty() && presets_.userFileFor(name).existsAsFile()) {
        auto* w = new juce::AlertWindow("Replace preset?",
                                        "You already have a preset called \"" + name.trim() + "\". Replace it?",
                                        juce::MessageBoxIconType::NoIcon);
        w->addButton("Replace", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog_ = w;
        showInEditor(*this, w, [safe, name](int r) {
            if (r == 1 && safe != nullptr) {
                safe->save(name, true);
            }
        });
        return;
    }
    auto* w = new juce::AlertWindow("Not saved", error, juce::MessageBoxIconType::NoIcon);
    w->addButton("OK", 0, juce::KeyPress(juce::KeyPress::returnKey));
    dialog_ = w;
    showInEditor(*this, w, [](int) {});
}

} // namespace syrinx::ui
