#include "Widgets.h"

#include <cmath>

namespace syrinx::ui {

namespace {

constexpr float kKnobStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float kKnobEnd = juce::MathConstants<float>::pi * 2.75f;

juce::RangedAudioParameter& paramOf(juce::AudioProcessorValueTreeState& apvts, const juce::String& id) {
    auto* p = apvts.getParameter(id);
    jassert(p != nullptr);
    return *p;
}

} // namespace

// --------------------------------------------------------------------------------------------- Card
void Card::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    {
        juce::Path shadow;
        shadow.addRoundedRectangle(r.translated(0.0f, 3.0f), 12.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.45f), 14, {0, 2}).drawForPath(g, shadow);
    }
    g.setGradientFill(juce::ColourGradient(colours::surfaceTop, 0.0f, r.getY(), colours::surface, 0.0f, r.getBottom(),
                                           false));
    g.fillRoundedRectangle(r, 12.0f);
    g.setColour(colours::border);
    g.drawRoundedRectangle(r, 12.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.035f));
    g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 12.0f, r.getRight() - 12.0f);

    g.setColour(colours::accent);
    g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({r.getX() + 18.5f, r.getY() + 21.0f}));
    g.setColour(colours::text2);
    g.setFont(titleFont(10.5f));
    g.drawText(title_.toUpperCase(), juce::Rectangle<float>(r.getX() + 27.0f, r.getY() + 12.0f, r.getWidth() - 40.0f, 18.0f),
               juce::Justification::centredLeft, false);
}

// --------------------------------------------------------------------------------------------- Knob
Knob::Knob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String name, Formatter format,
           juce::String leftCaption, juce::String rightCaption, bool bipolar)
    : param_(&paramOf(apvts, paramId)), name_(std::move(name)), left_(std::move(leftCaption)),
      right_(std::move(rightCaption)), format_(std::move(format)) {
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setRotaryParameters(kKnobStart, kKnobEnd, true);
    slider_.setMouseDragSensitivity(240);
    slider_.setPopupDisplayEnabled(false, false, nullptr);
    slider_.getProperties().set("bipolar", bipolar);
    slider_.getProperties().set("valueInside", true);
    slider_.setTitle(name_);
    addAndMakeVisible(slider_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider_);
    slider_.setDoubleClickReturnValue(true, static_cast<double>(param_->convertFrom0to1(param_->getDefaultValue())));
    slider_.onValueChange = [this] { repaint(); };
}

void Knob::resized() {
    auto r = getLocalBounds();
    const int d = std::min(r.getWidth(), r.getHeight() - 26);
    slider_.setBounds(juce::Rectangle<int>(d, d).withCentre({r.getCentreX(), r.getY() + d / 2}));
}

void Knob::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const auto k = slider_.getBounds().toFloat();
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.setFont(font(13.5f, Weight::demibold));
    g.drawText(name_, juce::Rectangle<float>(0.0f, k.getBottom() + 3.0f, static_cast<float>(getWidth()), 20.0f),
               juce::Justification::centredTop, false);
    if (left_.isNotEmpty() || right_.isNotEmpty()) {
        g.setColour(colours::text3.withMultipliedAlpha(alpha));
        g.setFont(font(10.5f, Weight::medium));
        const float y = k.getBottom() - 13.0f;
        const float half = k.getWidth() * 0.5f;
        g.drawText(left_, juce::Rectangle<float>(k.getX() - 26.0f, y, half, 14.0f), juce::Justification::centredLeft, false);
        g.drawText(right_, juce::Rectangle<float>(k.getCentreX() + 26.0f - 0.0f, y, half, 14.0f),
                   juce::Justification::centredRight, false);
    }
}

void Knob::paintOverChildren(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const auto k = slider_.getBounds().toFloat();
    const float d = std::min(k.getWidth(), k.getHeight());
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.setFont(font(std::max(11.0f, d * 0.145f), Weight::demibold, true));
    g.drawText(format_ ? format_(slider_.getValue()) : juce::String(slider_.getValue(), 2),
               k.withSizeKeepingCentre(d * 0.62f, d * 0.3f), juce::Justification::centred, false);
}

void Knob::setModAmount(float amount) {
    amount = juce::jlimit(0.0f, 1.0f, amount);
    if (std::abs(amount - mod_) > 1e-4f) {
        mod_ = amount;
        slider_.getProperties().set("modAmount", static_cast<double>(amount));
        slider_.repaint();
    }
}

// --------------------------------------------------------------------------------------------- Segmented
Segmented::Segmented(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::StringArray labels)
    : labels_(std::move(labels)) {
    auto& p = paramOf(apvts, paramId);
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        p,
        [this](float v) {
            index_ = static_cast<int>(std::lround(v));
            repaint();
        },
        nullptr);
    attachment_->sendInitialUpdate();
    setTitle(p.getName(64));
}

Segmented::Segmented(juce::StringArray labels, std::function<void(int)> onChange)
    : labels_(std::move(labels)), onChange_(std::move(onChange)) {}

int Segmented::segmentAt(juce::Point<int> p) const {
    if (labels_.isEmpty() || !getLocalBounds().contains(p)) {
        return -1;
    }
    return juce::jlimit(0, labels_.size() - 1, p.x * labels_.size() / std::max(1, getWidth()));
}

void Segmented::setIndex(int index, bool notify) {
    index = juce::jlimit(0, std::max(0, labels_.size() - 1), index);
    if (attachment_ != nullptr) {
        if (notify) {
            attachment_->setValueAsCompleteGesture(static_cast<float>(index));
        }
    } else if (notify && onChange_ && index != index_) {
        index_ = index;
        onChange_(index);
    }
    index_ = index;
    repaint();
}

void Segmented::mouseDown(const juce::MouseEvent& e) {
    if (const int i = segmentAt(e.getPosition()); i >= 0 && isEnabled()) {
        setIndex(i, true);
    }
}

void Segmented::mouseMove(const juce::MouseEvent& e) {
    const int i = segmentAt(e.getPosition());
    if (i != hover_) {
        hover_ = i;
        repaint();
    }
}

void Segmented::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void Segmented::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = std::min(8.0f, r.getHeight() * 0.5f);
    g.setColour(colours::inset.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, radius);
    g.setColour(colours::insetBorder.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, radius, 1.0f);
    const int n = std::max(1, labels_.size());
    const float w = r.getWidth() / static_cast<float>(n);
    for (int i = 0; i < labels_.size(); ++i) {
        const auto seg = juce::Rectangle<float>(r.getX() + w * static_cast<float>(i), r.getY(), w, r.getHeight());
        const bool sel = i == index_;
        if (sel) {
            const auto s = seg.reduced(3.0f);
            g.setColour(colours::accent.withAlpha(0.17f * alpha));
            g.fillRoundedRectangle(s, radius - 2.5f);
            g.setColour(colours::accent.withAlpha(0.55f * alpha));
            g.drawRoundedRectangle(s, radius - 2.5f, 1.0f);
        } else if (i == hover_ && isEnabled()) {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(seg.reduced(3.0f), radius - 2.5f);
        }
        g.setColour((sel ? colours::accent.brighter(0.25f) : (i == hover_ ? colours::text : colours::text2))
                        .withMultipliedAlpha(alpha));
        g.setFont(font(textHeight_, sel ? Weight::demibold : Weight::medium));
        g.drawFittedText(labels_[i], seg.reduced(4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
    }
}

// --------------------------------------------------------------------------------------------- Switch
Switch::Switch(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String label)
    : label_(std::move(label)) {
    auto& p = paramOf(apvts, paramId);
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        p,
        [this](float v) {
            on_ = v >= 0.5f;
            repaint();
        },
        nullptr);
    attachment_->sendInitialUpdate();
    setTitle(label_);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void Switch::mouseDown(const juce::MouseEvent&) {
    if (isEnabled()) {
        attachment_->setValueAsCompleteGesture(on_ ? 0.0f : 1.0f);
    }
}

void Switch::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const auto r = getLocalBounds().toFloat();
    const float h = 18.0f;
    const auto track = juce::Rectangle<float>(r.getRight() - 32.0f, r.getCentreY() - h * 0.5f, 32.0f, h);
    const bool over = isMouseOver(true) && isEnabled();
    g.setColour((on_ ? colours::accent : colours::track.brighter(over ? 0.18f : 0.0f)).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, h * 0.5f);
    const float d = h - 4.0f;
    const auto thumb = juce::Rectangle<float>(on_ ? track.getRight() - 2.0f - d : track.getX() + 2.0f,
                                              track.getY() + 2.0f, d, d);
    g.setColour(juce::Colours::black.withAlpha(0.3f * alpha));
    g.fillEllipse(thumb.translated(0.0f, 0.8f));
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.fillEllipse(thumb);
    g.setColour((on_ || over ? colours::text : colours::text2).withMultipliedAlpha(alpha));
    g.setFont(font(12.5f, Weight::medium));
    g.drawFittedText(label_, r.withRight(track.getX() - 8.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
}

// --------------------------------------------------------------------------------------------- MiniSlider
MiniSlider::MiniSlider(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String name,
                       Knob::Formatter format)
    : param_(&paramOf(apvts, paramId)), name_(std::move(name)), format_(std::move(format)) {
    slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setTitle(name_);
    addAndMakeVisible(slider_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider_);
    slider_.setDoubleClickReturnValue(true, static_cast<double>(param_->convertFrom0to1(param_->getDefaultValue())));
    slider_.onValueChange = [this] { repaint(); };
}

void MiniSlider::resized() {
    slider_.setBounds(getLocalBounds().withTrimmedTop(18).reduced(1, 0));
}

void MiniSlider::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    auto top = getLocalBounds().removeFromTop(18).toFloat();
    g.setFont(font(12.5f, Weight::medium));
    g.setColour(colours::text2.withMultipliedAlpha(alpha));
    g.drawText(name_, top, juce::Justification::centredLeft, false);
    g.setFont(font(12.5f, Weight::demibold, true));
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.drawText(format_ ? format_(slider_.getValue()) : juce::String(slider_.getValue(), 2), top,
               juce::Justification::centredRight, false);
}

// --------------------------------------------------------------------------------------------- ChoiceBox
ChoiceBox::ChoiceBox(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::String caption,
                     juce::StringArray labels)
    : caption_(std::move(caption)) {
    box_.addItemList(labels, 1);
    box_.setTitle(caption_);
    addAndMakeVisible(box_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, paramId, box_);
}

void ChoiceBox::resized() {
    box_.setBounds(getLocalBounds().withTrimmedTop(18));
}

void ChoiceBox::paint(juce::Graphics& g) {
    g.setFont(font(12.5f, Weight::medium));
    g.setColour(colours::text2.withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
    g.drawText(caption_, getLocalBounds().removeFromTop(18).toFloat(), juce::Justification::centredLeft, false);
}

} // namespace syrinx::ui
