#pragma once

#include "../Presets.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace syrinx::ui {

/// WP-R4: previous / name (menu) / next / save. Presets load on the message thread only.
class PresetBar : public juce::Component {
public:
    explicit PresetBar(PresetManager& presets);
    ~PresetBar() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    /// Timer (message thread): show the current name and whether the sound was changed since.
    void refresh();

private:
    class Arrow : public juce::Button {
    public:
        explicit Arrow(bool right) : juce::Button(right ? "Next preset" : "Previous preset"), right_(right) {}
        void paintButton(juce::Graphics&, bool over, bool down) override;

    private:
        bool right_;
    };
    class NameBox : public juce::Component {
    public:
        explicit NameBox(PresetBar& bar) : bar_(bar) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { bar_.showMenu(); }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }

    private:
        PresetBar& bar_;
    };

    void showMenu();
    void askName();
    void save(const juce::String& name, bool overwrite);

    PresetManager& presets_;
    Arrow prev_{false}, next_{true};
    NameBox name_{*this};
    juce::TextButton save_{"Save"};
    juce::Component::SafePointer<juce::AlertWindow> dialog_; // open dialog, closed if the window goes
    juce::String shownName_, shownCategory_;
    bool shownModified_ = false;
};

} // namespace syrinx::ui
