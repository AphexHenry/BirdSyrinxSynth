#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

/// WP-R4: the look of Syrinx. A dusk-coloured instrument; the accent is the zebra finch's beak.
/// The editor is designed on a fixed canvas (kWidth × kHeight) and scaled as a whole.
namespace syrinx::ui {

inline constexpr int kWidth = 1080;
inline constexpr int kHeight = 720;

namespace colours {
inline const juce::Colour bgTop{0xff15181c};
inline const juce::Colour bgBottom{0xff0d0f12};
inline const juce::Colour surface{0xff1a1d22};      // cards
inline const juce::Colour surfaceTop{0xff1e2227};   // cards, top of the gradient
inline const juce::Colour border{0xff282c33};
inline const juce::Colour inset{0xff0c0e11};        // display, fields, segmented controls
inline const juce::Colour insetBorder{0xff22262c};
inline const juce::Colour text{0xffede8e0};         // warm white
inline const juce::Colour text2{0xffa3a7ae};
inline const juce::Colour text3{0xff6d727a};
inline const juce::Colour textOff{0xff4a4e55};
inline const juce::Colour accent{0xffff6b3d};       // the zebra finch's beak
inline const juce::Colour accent2{0xffffb547};      // its cheek patch: the mod wheel
inline const juce::Colour track{0xff2b2f36};
inline const juce::Colour knobTop{0xff31353c};
inline const juce::Colour knobBottom{0xff1b1e23};
inline const juce::Colour gridC{0xff232830};
inline const juce::Colour rowBlack{0xff0f1215};     // piano-roll rows of black keys
} // namespace colours

/// One colour per singing key (playing-mode slot), shared by the display and the keyboard.
inline juce::Colour singerColour(int singer) {
    static const std::array<juce::Colour, 6> hues{
        juce::Colour(0xffff6b3d), // beak orange
        juce::Colour(0xff62c6ff), // sky
        juce::Colour(0xffffc24a), // amber
        juce::Colour(0xffff7aa8), // rose
        juce::Colour(0xff8be0b0), // leaf
        juce::Colour(0xffc4a8ff), // lilac
    };
    return hues[static_cast<std::size_t>(((singer % 6) + 6) % 6)];
}

enum class Weight { regular, medium, demibold, bold };

/// Avenir Next (built into macOS); other systems fall back to the default sans-serif.
inline juce::Font font(float height, Weight w = Weight::regular, bool tabularDigits = false) {
    const char* style = w == Weight::regular    ? "Regular"
                        : w == Weight::medium   ? "Medium"
                        : w == Weight::demibold ? "Demi Bold"
                                                : "Bold";
    auto opts = juce::FontOptions{}.withName("Avenir Next").withStyle(style).withHeight(height);
    if (tabularDigits) {
        opts = opts.withFeatureEnabled(juce::FontFeatureTag{"tnum"});
    }
    return juce::Font{opts};
}

/// Small capitals look for section titles: upper case with open letter spacing.
inline juce::Font titleFont(float height = 10.5f) {
    return font(height, Weight::demibold).withExtraKerningFactor(0.14f);
}

inline float textWidth(const juce::Font& f, const juce::String& s) {
    return juce::GlyphArrangement::getStringWidth(f, s);
}

} // namespace syrinx::ui
