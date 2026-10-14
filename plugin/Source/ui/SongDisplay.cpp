#include "SongDisplay.h"

#include "cuicui/control/variation.hpp"

#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace syrinx::ui {

namespace {

constexpr double kLn440 = 6.0867747269123065; // ln(440)
constexpr double kCentsPerLn = 1200.0 / 0.69314718055994531;

double centsOfLogHz(double logHz) noexcept {
    return (logHz - kLn440) * kCentsPerLn + 6900.0;
}

double hzOfCents(double cents) noexcept {
    return 440.0 * std::pow(2.0, (cents - 6900.0) / 1200.0);
}

bool isBlackKey(int midi) noexcept {
    const int pc = ((midi % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

juce::String noteName(int midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

juce::String khz(double hz) {
    return juce::String(hz / 1000.0, hz < 9950.0 ? 1 : 0) + " kHz";
}

} // namespace

SongDisplay::SongDisplay(SyrinxAudioProcessor& proc) : proc_(proc) {
    items_.reserve(512);
    singerOfNote_.fill(-1);
    noteSeenAt_.fill(-1.0e18);
    targetOfNote_.fill(0.0f);
    epoch_ = proc_.feed().epoch();
    axisCentre_ = naturalCentre();
    lastUpdateMs_ = juce::Time::getMillisecondCounterHiRes();
    setInterceptsMouseClicks(false, false);
    setHelpText("What the bird sings: each syllable's real pitch curve as it plays (last 4 seconds), one colour "
                "per key, over a piano roll. Lines show the notes of the keys you hold.");
}

double SongDisplay::naturalCentre() const {
    const auto& bank = proc_.syllableBank();
    if (bank.empty()) {
        return 9600.0;
    }
    const int rule = juce::jlimit(0, cuicui::control::kNoteRuleCount - 1,
                                  static_cast<int>(proc_.apvts.getRawParameterValue("perf.note_rule")->load()));
    const double size = static_cast<double>(proc_.apvts.getRawParameterValue("perf.size_st")->load());
    return bank.centreCents(static_cast<cuicui::control::NoteRule>(rule)) + 100.0 * size;
}

double SongDisplay::currentSample() const {
    const auto& f = proc_.feed();
    const double clock = static_cast<double>(f.clock());
    if (frozen_) {
        return clock;
    }
    // Between audio blocks, move on with the wall clock (smooth scrolling), never more than 60 ms ahead.
    const double ahead = (juce::Time::getMillisecondCounterHiRes() - f.clockWallMs()) * 0.001 * sr_;
    return clock + juce::jlimit(0.0, 0.06 * sr_, ahead);
}

void SongDisplay::update() {
    auto& f = proc_.feed();
    const auto& bank = proc_.syllableBank();
    if (f.epoch() != epoch_) { // the audio restarted (prepareToPlay): a new clock
        epoch_ = f.epoch();
        items_.clear();
        now_ = 0.0;
        singerOfNote_.fill(-1);
        noteSeenAt_.fill(-1.0e18);
    }
    sr_ = f.sampleRate() > 1000.0 ? f.sampleRate() : 48000.0;
    bool got = false;
    SyllableFeed::Event buf[64];
    for (int k = 0; (k = f.pop(buf, 64)) > 0;) {
        for (int i = 0; i < k; ++i) {
            const auto& e = buf[i];
            if (e.syllable < 0 || e.syllable >= bank.numSyllables()) {
                continue;
            }
            const auto& syl = bank.syllable(e.syllable);
            Item item{e, static_cast<double>(e.sample) + syl.lengthMs * 0.001 * sr_, {}, {}};
            if ((e.shape > 0.0f && bank.hasVariation()) || e.lengthScale != 1.0 || e.rangeScale != 1.0) { // WP-R3
                constexpr int cap = cuicui::control::SyllableEngine::kMaxVariantMs;
                item.logHz.resize(static_cast<std::size_t>(cap));
                item.amp.resize(static_cast<std::size_t>(cap));
                const auto v = cuicui::control::makeVariant(
                    bank, e.syllable, static_cast<double>(e.shape),
                    e.shape > 0.0f ? cuicui::control::variantDraws(e.variantSeed) : cuicui::control::VariantDraws{},
                    e.lengthScale, e.rangeScale, item.logHz.data(), item.amp.data(), cap); // the same rendition
                item.logHz.resize(static_cast<std::size_t>(v.lengthMs));
                item.amp.resize(static_cast<std::size_t>(v.lengthMs));
                item.endSample = static_cast<double>(e.sample) + v.lengthMs * 0.001 * sr_;
            }
            items_.push_back(std::move(item));
            if (e.note >= 0 && e.note < 128) {
                singerOfNote_[static_cast<std::size_t>(e.note)] = e.singer;
                noteSeenAt_[static_cast<std::size_t>(e.note)] = static_cast<double>(e.sample);
                targetOfNote_[static_cast<std::size_t>(e.note)] = e.targetCents;
            }
            got = true;
        }
    }
    now_ = frozen_ ? currentSample() : std::max(now_, currentSample());
    const double oldest = now_ - kWindowS * sr_;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const Item& it) {
                                    return it.endSample < oldest || static_cast<double>(it.e.sample) > now_ + 2.0 * sr_;
                                }),
                 items_.end());
    if (items_.size() > 400) {
        items_.erase(items_.begin(), items_.begin() + static_cast<long>(items_.size() - 400));
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit(0.0, 0.25, (nowMs - lastUpdateMs_) * 0.001);
    lastUpdateMs_ = nowMs;
    const double before = axisCentre_;
    updateAxis(frozen_ ? 10.0 : dt);

    bool keys = false;
    for (int n = 0; n < 128 && !keys; ++n) {
        keys = proc_.keyboardState.isNoteOnForChannels(0xffff, n);
    }
    std::uint64_t chips = proc_.feed().sustain() ? 0x9e3779b97f4a7c15ULL : 1ULL;
    for (const auto& it : items_) {
        if (static_cast<double>(it.e.sample) <= now_ && now_ < it.endSample) {
            chips = chips * 1099511628211ULL + static_cast<std::uint64_t>(it.e.sample) * 31ULL +
                    static_cast<std::uint64_t>(it.e.syllable);
        }
    }
    const bool axisMoved = std::abs(axisCentre_ - before) > 0.25;
    const bool active = !items_.empty() || keys || axisMoved;
    if (axisMoved || active != wasActive_) {
        repaint(); // the labels move too, or the idle text comes or goes
    } else if (active || got) {
        repaint(plotArea().expanded(2.0f).toNearestInt()); // only the scrolling song
        if (chips != lastChips_) {
            repaint(getLocalBounds().withHeight(36));
        }
    }
    lastChips_ = chips;
    wasActive_ = active;
}

void SongDisplay::updateAxis(double dt) {
    const auto& bank = proc_.syllableBank();
    double lo = 1e9, hi = -1e9;
    for (const auto& it : items_) {
        if (static_cast<double>(it.e.sample) > now_) {
            continue;
        }
        const auto& syl = bank.syllable(it.e.syllable);
        const double* lh = bank.logHz() + syl.offset;
        const float* amp = bank.amp() + syl.offset;
        for (int i = 0; i < syl.lengthMs; i += 4) {
            if (amp[i] >= 0.06f) {
                const double c = centsOfLogHz(lh[i]) + static_cast<double>(it.e.shiftCents);
                lo = std::min(lo, c);
                hi = std::max(hi, c);
            }
        }
    }
    double desired = axisCentre_;
    if (hi >= lo) {
        const double half = kSpanCents * 0.5;
        const double margin = 220.0;
        if (lo < axisCentre_ - half + margin || hi > axisCentre_ + half - margin) {
            desired = 0.5 * (lo + hi);
        }
    } else {
        desired = naturalCentre();
    }
    axisCentre_ += (desired - axisCentre_) * (1.0 - std::exp(-dt * 3.0));
}

float SongDisplay::yFor(double cents, juce::Rectangle<float> plot) const noexcept {
    const double lo = axisCentre_ - kSpanCents * 0.5;
    return plot.getBottom() - static_cast<float>((cents - lo) / kSpanCents) * plot.getHeight();
}

void SongDisplay::setBackgroundPainter(std::function<void(juce::Graphics&)> painter) {
    paintBehind_ = std::move(painter);
    setOpaque(paintBehind_ != nullptr);
    staticLayer_ = {};
    repaint();
}

int SongDisplay::singerForNote(int note) const noexcept {
    if (note < 0 || note > 127) {
        return -1;
    }
    const auto n = static_cast<std::size_t>(note);
    return noteSeenAt_[n] > now_ - 30.0 * sr_ ? singerOfNote_[n] : -1;
}

// --------------------------------------------------------------------------------------------- painting
juce::Rectangle<float> SongDisplay::plotArea() const {
    return getLocalBounds().toFloat().reduced(0.5f).withTrimmedLeft(40.0f).withTrimmedRight(46.0f).withTrimmedTop(40.0f)
        .withTrimmedBottom(16.0f);
}

void SongDisplay::paintStatic(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> plot) const {
    if (paintBehind_) {
        juce::Graphics::ScopedSaveState s(g);
        paintBehind_(g);
    }
    g.setColour(colours::inset);
    g.fillRoundedRectangle(r, 12.0f);
    {
        juce::Graphics::ScopedSaveState s(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, 12.0f);
        g.reduceClipRegion(clip);
        paintGrid(g, plot);
        g.setColour(colours::text3.withAlpha(0.35f)); // the "now" edge
        g.drawVerticalLine(static_cast<int>(plot.getRight()), plot.getY(), plot.getBottom());
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.35f), 0.0f, r.getY(),
                                               juce::Colours::transparentBlack, 0.0f, r.getY() + 10.0f, false));
        g.fillRect(r.withHeight(10.0f)); // depth under the panel's top edge
    }
    g.setColour(colours::insetBorder);
    g.drawRoundedRectangle(r, 12.0f, 1.0f);
    g.setFont(font(10.0f, Weight::medium));
    g.setColour(colours::text3);
    g.drawText("last 4 s", juce::Rectangle<float>(plot.getX(), plot.getBottom() + 1.0f, 120.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("now", juce::Rectangle<float>(plot.getRight() - 60.0f, plot.getBottom() + 1.0f, 60.0f, 14.0f),
               juce::Justification::centredRight, false);
    const auto& bank = proc_.syllableBank();
    g.setFont(titleFont(9.5f));
    const auto species = juce::String(bank.species()).isNotEmpty() ? juce::String(bank.species()) : juce::String("bird");
    const auto nb = static_cast<int>(bank.birds().size());
    const auto info = species.toUpperCase() + "  " +
                      (nb > 1 ? juce::String(nb) + " BIRDS" : juce::String(bank.name()).toUpperCase()) + "  ·  " +
                      juce::String(bank.numSyllables()) + " SYLLABLES";
    g.drawText(info, r.withHeight(34.0f).reduced(14.0f, 0.0f), juce::Justification::centredRight, false);
}

void SongDisplay::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto plot = plotArea();
    // The still layer is cached as an image at the screen's real resolution; only the song is drawn live.
    const float scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int iw = juce::roundToInt(static_cast<float>(getWidth()) * scale);
    const int ih = juce::roundToInt(static_cast<float>(getHeight()) * scale);
    if (!staticLayer_.isValid() || staticLayer_.getWidth() != iw || staticLayer_.getHeight() != ih ||
        std::abs(scale - staticScale_) > 1e-3f || std::abs(axisCentre_ - staticCentre_) > 0.25) {
        staticLayer_ = juce::Image(juce::Image::ARGB, std::max(1, iw), std::max(1, ih), true);
        juce::Graphics ig(staticLayer_);
        ig.addTransform(juce::AffineTransform::scale(scale));
        paintStatic(ig, r, plot);
        staticScale_ = scale;
        staticCentre_ = axisCentre_;
    }
    g.drawImageTransformed(staticLayer_, juce::AffineTransform::scale(1.0f / scale));
    {
        juce::Graphics::ScopedSaveState s2(g);
        g.reduceClipRegion(plot.toNearestInt());
        paintBands(g, plot, false);
        paintTraces(g, plot);
        // old song fades into the left edge
        g.setGradientFill(juce::ColourGradient(colours::inset, plot.getX(), 0.0f, colours::inset.withAlpha(0.0f),
                                               plot.getX() + 90.0f, 0.0f, false));
        g.fillRect(plot.withWidth(90.0f));
        paintBands(g, plot, true);
    }
    paintChips(g, r);

    const bool idle = items_.empty() && !wasActive_;
    if (idle || items_.empty()) {
        const bool anatomical = !proc_.whistleSelected();
        g.setFont(font(13.5f, Weight::medium));
        g.setColour(colours::text3);
        g.drawFittedText(anatomical ? "Anatomical voice (experimental): its controls are on the Advanced page."
                                    : "Tap a key for one real syllable.  Hold it, and the bird keeps singing around your "
                                      "note;  let go, and it ends with its long rising call.",
                         plot.toNearestInt().reduced(20, 0), juce::Justification::centred, 2);
    }
}

void SongDisplay::paintGrid(juce::Graphics& g, juce::Rectangle<float> plot) const {
    const double lo = axisCentre_ - kSpanCents * 0.5;
    const double hi = lo + kSpanCents;
    const int m0 = static_cast<int>(std::floor(lo / 100.0)) - 1;
    const int m1 = static_cast<int>(std::ceil(hi / 100.0)) + 1;
    const auto full = plot.withX(plot.getX() - 40.0f).withRight(plot.getRight() + 46.0f);
    g.setFont(font(10.0f, Weight::medium));
    for (int m = m0; m <= m1; ++m) {
        const float yTop = yFor(m * 100.0 + 50.0, plot);
        const float yBot = yFor(m * 100.0 - 50.0, plot);
        if (yBot < plot.getY() || yTop > plot.getBottom()) {
            continue;
        }
        if (isBlackKey(m)) {
            g.setColour(colours::rowBlack);
            g.fillRect(juce::Rectangle<float>(plot.getX(), yTop, plot.getWidth(), yBot - yTop)
                           .getIntersection(plot));
        }
        if (((m % 12) + 12) % 12 == 0) {
            g.setColour(colours::gridC);
            g.drawHorizontalLine(static_cast<int>(std::round(yBot)), plot.getX(), plot.getRight());
            g.setColour(colours::text3);
            g.drawText(noteName(m), juce::Rectangle<float>(full.getX() + 8.0f, yBot - 13.0f, 32.0f, 13.0f),
                       juce::Justification::centredLeft, false);
        }
    }
    static const double kHzMarks[] = {1000.0, 1500.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 12000.0, 16000.0};
    for (const double hz : kHzMarks) {
        const double c = 1200.0 * std::log2(hz / 440.0) + 6900.0;
        if (c < lo + 40.0 || c > hi - 40.0) {
            continue;
        }
        const float y = yFor(c, plot);
        g.setColour(colours::text3.withAlpha(0.5f));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), plot.getRight(), plot.getRight() + 4.0f);
        g.setColour(colours::text3);
        g.drawText(hz >= 10000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                                 : juce::String(hz / 1000.0, hz == 1500.0 ? 1 : 0) + "k",
                   juce::Rectangle<float>(plot.getRight() + 7.0f, y - 7.0f, 36.0f, 14.0f),
                   juce::Justification::centredLeft, false);
    }
}

void SongDisplay::paintBands(juce::Graphics& g, juce::Rectangle<float> plot, bool tags) const {
    for (int n = 0; n < 128; ++n) {
        const auto i = static_cast<std::size_t>(n);
        if (!proc_.keyboardState.isNoteOnForChannels(0xffff, n) || noteSeenAt_[i] < now_ - 60.0 * sr_) {
            continue;
        }
        const double target = static_cast<double>(targetOfNote_[i]);
        const float y = yFor(target, plot);
        if (y < plot.getY() || y > plot.getBottom()) {
            continue;
        }
        const auto col = singerColour(singerOfNote_[i]);
        if (!tags) { // a soft band and a fine line (solid: dashes cost far more to draw)
            g.setColour(col.withAlpha(0.10f));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - 3.0f, plot.getWidth(), 6.0f));
            g.setColour(col.withAlpha(0.42f));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
            continue;
        }
        const auto label = noteName(static_cast<int>(std::lround(target / 100.0)));
        const auto f = font(10.5f, Weight::demibold);
        auto known = tagWidths_.find(label);
        if (known == tagWidths_.end()) {
            known = tagWidths_.emplace(label, textWidth(f, label)).first;
        }
        const float w = known->second + 12.0f;
        const auto pill = juce::Rectangle<float>(plot.getX() + 4.0f, y - 8.0f, w, 16.0f);
        g.setColour(col.withAlpha(0.95f));
        g.fillRoundedRectangle(pill, 8.0f);
        g.setColour(colours::inset);
        g.setFont(f);
        g.drawText(label, pill, juce::Justification::centred, false);
    }
}

void SongDisplay::paintTraces(juce::Graphics& g, juce::Rectangle<float> plot) const {
    const auto& bank = proc_.syllableBank();
    const bool harmonics = proc_.apvts.getRawParameterValue("voice.harmonics")->load() >= 0.5f;
    const double window = kWindowS * sr_;
    const float pxPerCent = plot.getHeight() / static_cast<float>(kSpanCents);
    // One path per bird and age group (recent / older): a few large strokes are much cheaper than many.
    constexpr int kGroups = 6 * 2;
    std::array<juce::Path, kGroups> paths;
    std::array<float, kGroups> vel{};
    struct Head {
        juce::Point<float> at;
        int singer;
    };
    std::array<Head, 32> heads{};
    int nHeads = 0;
    for (const auto& it : items_) {
        const auto& e = it.e;
        if (static_cast<double>(e.sample) > now_) {
            continue;
        }
        const auto& syl = bank.syllable(e.syllable);
        const bool varied = !it.logHz.empty();
        const double* lh = varied ? it.logHz.data() : bank.logHz() + syl.offset;
        const float* amp = varied ? it.amp.data() : bank.amp() + syl.offset;
        const int lengthMs = varied ? static_cast<int>(it.logHz.size()) : syl.lengthMs;
        const double playedMs = (now_ - static_cast<double>(e.sample)) * 1000.0 / sr_;
        const int iEnd = std::min(lengthMs - 1, static_cast<int>(std::floor(playedMs)));
        const int iStart = std::max(0, static_cast<int>(std::ceil((now_ - window - static_cast<double>(e.sample)) * 1000.0 / sr_)));
        if (iEnd < iStart) {
            continue;
        }
        juce::Path p;
        bool pen = false;
        juce::Point<float> head;
        for (int i = iStart;; i = std::min(i + 3, iEnd)) { // a point every 3 ms (≈ 1 px)
            const double t = static_cast<double>(e.sample) + i * 0.001 * sr_;
            const float x = plot.getRight() - static_cast<float>((now_ - t) / window) * plot.getWidth();
            const float y = yFor(centsOfLogHz(lh[i]) + static_cast<double>(e.shiftCents), plot);
            if (amp[i] < 0.05f) {
                pen = false;
            } else {
                if (pen) {
                    p.lineTo(x, y);
                } else {
                    p.startNewSubPath(x, y);
                }
                pen = true;
                head = {x, y};
            }
            if (i == iEnd) {
                break;
            }
        }
        if (p.isEmpty()) {
            continue;
        }
        const bool older = now_ - it.endSample > 0.5 * window;
        const int k = (((e.singer % 6) + 6) % 6) * 2 + (older ? 1 : 0);
        paths[static_cast<std::size_t>(k)].addPath(p);
        vel[static_cast<std::size_t>(k)] = juce::jlimit(0.0f, 1.0f, (e.gain - 0.12f) / 0.28f);
        if (now_ < it.endSample && nHeads < static_cast<int>(heads.size())) { // still singing
            heads[static_cast<std::size_t>(nHeads++)] = {head, e.singer};
        }
    }
    for (int k = 0; k < kGroups; ++k) {
        const auto& p = paths[static_cast<std::size_t>(k)];
        if (p.isEmpty()) {
            continue;
        }
        const auto col = singerColour(k / 2);
        const float alpha = (0.65f + 0.35f * vel[static_cast<std::size_t>(k)]) * ((k % 2) == 1 ? 0.6f : 1.0f);
        if (harmonics) { // the bird's 2nd harmonic, an octave up (a shift on this log axis)
            auto h2 = p;
            h2.applyTransform(juce::AffineTransform::translation(0.0f, -1200.0f * pxPerCent));
            g.setColour(col.withAlpha(0.22f * alpha));
            g.strokePath(h2, juce::PathStrokeType(1.2f));
        }
        g.setColour(col.withAlpha(0.16f * alpha));
        g.strokePath(p, juce::PathStrokeType(6.5f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
        g.setColour(col.withAlpha(0.95f * alpha));
        g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    for (int i = 0; i < nHeads; ++i) { // a glowing pen head on each syllable still singing
        const auto& h = heads[static_cast<std::size_t>(i)];
        g.setColour(singerColour(h.singer).withAlpha(0.3f));
        g.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(h.at));
        g.setColour(colours::text);
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(h.at));
    }
}

void SongDisplay::paintChips(juce::Graphics& g, juce::Rectangle<float> area) const {
    const auto& bank = proc_.syllableBank();
    auto top = area.withHeight(34.0f).reduced(14.0f, 0.0f);
    if (!g.getClipBounds().intersects(area.withHeight(36.0f).toNearestInt())) {
        return; // only the song scrolled: the chips did not change (and text costs time even when clipped)
    }
    if (proc_.feed().sustain()) { // left of the bird's name (drawn in the still layer)
        const float w = textWidth(titleFont(9.5f), juce::String(bank.species()).toUpperCase() + "  " +
                                                       juce::String(bank.name()).toUpperCase() + "  ·  " +
                                                       juce::String(bank.numSyllables()) + " SYLLABLES");
        auto pedal = juce::Rectangle<float>(top.getRight() - w - 64.0f, top.getCentreY() - 8.0f, 52.0f, 16.0f);
        g.setColour(colours::accent2.withAlpha(0.18f));
        g.fillRoundedRectangle(pedal, 8.0f);
        g.setColour(colours::accent2);
        g.setFont(titleFont(9.5f));
        g.drawText("PEDAL", pedal, juce::Justification::centred, false);
    }

    // the syllables sounding now, newest first
    float x = top.getX();
    int shown = 0;
    for (auto it = items_.rbegin(); it != items_.rend() && shown < 4; ++it) {
        const auto& e = it->e;
        if (static_cast<double>(e.sample) > now_ || now_ >= it->endSample) {
            continue;
        }
        const auto col = singerColour(e.singer);
        const auto rule = static_cast<cuicui::control::NoteRule>(juce::jlimit(
            0, 3, static_cast<int>(proc_.apvts.getRawParameterValue("perf.note_rule")->load())));
        const double noteCents = bank.anchorCents(e.syllable, rule) + static_cast<double>(e.shiftCents);
        const auto key = noteName(static_cast<int>(std::lround(static_cast<double>(e.targetCents) / 100.0)));
        const auto label = juce::String(bank.label(e.syllable));
        const auto hz = khz(hzOfCents(noteCents));
        const auto fKey = font(11.5f, Weight::demibold);
        const auto fTxt = font(11.5f, Weight::medium, true);
        const float w = 26.0f + textWidth(fKey, key) + textWidth(fTxt, label + "   " + hz) + 16.0f;
        if (x + w > top.getRight() - 230.0f) {
            break;
        }
        const auto chip = juce::Rectangle<float>(x, top.getCentreY() - 11.0f, w, 22.0f);
        g.setColour(col.withAlpha(0.12f));
        g.fillRoundedRectangle(chip, 11.0f);
        g.setColour(col.withAlpha(0.45f));
        g.drawRoundedRectangle(chip.reduced(0.5f), 11.0f, 1.0f);
        g.setColour(col);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({chip.getX() + 13.0f, chip.getCentreY()}));
        g.setColour(colours::text);
        g.setFont(fKey);
        const float kx = chip.getX() + 22.0f;
        g.drawText(key, juce::Rectangle<float>(kx, chip.getY(), 60.0f, chip.getHeight()), juce::Justification::centredLeft,
                   false);
        g.setColour(colours::text2);
        g.setFont(fTxt);
        g.drawText(label + "   " + hz,
                   juce::Rectangle<float>(kx + textWidth(fKey, key) + 7.0f, chip.getY(), w, chip.getHeight()),
                   juce::Justification::centredLeft, false);
        x += w + 8.0f;
        ++shown;
    }
}

} // namespace syrinx::ui
