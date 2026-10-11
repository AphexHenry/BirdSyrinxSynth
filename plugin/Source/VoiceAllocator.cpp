#include "VoiceAllocator.h"

#include "cuicui/dsp/rng.hpp"
#include "cuicui/model/labial_oscillator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

void VoiceAllocator::prepare(const cuicui::ProcessSpec& spec, const cuicui::control::InverseMap& map) {
    spec_ = spec;
    map_ = &map;
    const double sr = spec.hostSampleRate > 0.0 ? spec.hostSampleRate : 48000.0;
    ctrlRate_ = sr / 32.0;
    fadeLen_ = std::max(1, static_cast<int>(std::lround(0.001 * static_cast<double>(kFadeMs) * sr)));
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& s = slots_[static_cast<std::size_t>(i)];
        s.voice.prepare(spec);
        s.voice.setSource(cuicui::VoiceSource::Labial);
        s.voice.primeLabial(cuicui::model::kAlphaRest, 0.3, 3000.0, 0.5, 0.0, 0.3);
        s.controller.prepare(ctrlRate_, map);
        s.engine.prepare();
        s.controller.setGestureSource(&s.engine);
        s.lastFrame = {};
        s.playing = false;
        s.gated = false;
        s.note = -1;
        s.prepared = true;
        updatePan(i);
    }
    prepared_ = true;
    stealCount_ = 0;
    cpuCap_ = kMaxVoices;
    overrunBlocks_ = 0;
    ageCounter_ = 1;
}

void VoiceAllocator::reset(std::uint64_t engineSeed) noexcept {
    for (auto& s : slots_) {
        if (!s.prepared) {
            continue;
        }
        s.voice.reset(engineSeed == 0 ? 1ULL : engineSeed);
        s.controller.reset();
        s.engine.noteOff();
        s.playing = false;
        s.gated = false;
        s.note = -1;
        s.fadeIn = false;
        s.fadeSamples = 0;
        s.lastFrame = {};
    }
    stealCount_ = 0;
}

void VoiceAllocator::prime(const cuicui::VoiceSettings& settings, double alpha, double beta, double oecHz,
                           double gape, double alphaOffR, double betaR,
                           const cuicui::ControlFrame& restFrame) noexcept {
    for (auto& s : slots_) {
        if (!s.prepared) {
            continue;
        }
        s.voice.setSettings(settings);
        s.voice.reset(settings.seed == 0 ? 1ULL : settings.seed);
        s.voice.setOutGainRamp(static_cast<float>(settings.out_gain_db), static_cast<float>(settings.out_gain_db));
        s.voice.primeLabial(alpha, beta, oecHz, gape, alphaOffR, betaR);
        s.lastFrame = restFrame;
    }
}

void VoiceAllocator::setPolyphony(int n) noexcept {
    polyphony_ = std::clamp(n, 1, kMaxVoices);
}

void VoiceAllocator::setCpuCap(int cap) noexcept {
    cpuCap_ = std::clamp(cap, 1, kMaxVoices);
}

int VoiceAllocator::playingCount() const noexcept {
    int n = 0;
    for (const auto& s : slots_) {
        if (s.playing && s.gated) {
            ++n;
        }
    }
    return n;
}

void VoiceAllocator::updatePan(int i) noexcept {
    auto& s = slots_[static_cast<std::size_t>(i)];
    if (polyphony_ <= 1) {
        s.panL = 1.0f;
        s.panR = 1.0f;
        return;
    }
    const double u = std::fmod(0.5 + static_cast<double>(i) * 0.6180339887498948, 1.0);
    const double pos = (2.0 * u - 1.0) * static_cast<double>(spread_);
    const double ang = (pos + 1.0) * (std::numbers::pi / 4.0);
    s.panL = static_cast<float>(std::cos(ang));
    s.panR = static_cast<float>(std::sin(ang));
}

void VoiceAllocator::startSlot(int i, int note, int vel, std::uint32_t phraseSeed, bool fadeIn) noexcept {
    auto& s = slots_[static_cast<std::size_t>(i)];
    s.note = note;
    s.vel = vel;
    s.phraseSeed = phraseSeed == 0 ? 1u : phraseSeed;
    s.age = ageCounter_++;
    s.playing = true;
    s.gated = true;
    s.fadeIn = fadeIn;
    s.fadeSamples = 0;
    s.fadeLen = fadeLen_;
    s.fadeFrom = fadeIn ? 0.0f : 1.0f;
    s.fadeTo = 1.0f;
    const double v = 1.0; // Variation applied by caller via macros; flock uses seed.
    cuicui::dsp::Rng rng(cuicui::dsp::deriveStreamSeed(s.phraseSeed, 9));
    const double u1 = static_cast<double>(rng.uniform()) * 2.0 - 1.0;
    const double u2 = static_cast<double>(rng.uniform()) * 2.0 - 1.0;
    (void)v;
    s.sizeMul = 1.0 + 0.03 * u1; // Variation scaled in process()
    s.toneOff = 0.05 * u2;
    s.controller.reset();
    s.engine.prepare();
    updatePan(i);
}

void VoiceAllocator::releaseSlot(int i) noexcept {
    auto& s = slots_[static_cast<std::size_t>(i)];
    s.gated = false;
    s.engine.noteOff();
}

int VoiceAllocator::allocSlot(int note) noexcept {
    const int cap = std::min(polyphony_, cpuCap_);
    int free = -1;
    int oldestRel = -1;
    int oldest = -1;
    std::uint32_t relAge = 0xffffffffu;
    std::uint32_t playAge = 0xffffffffu;
    int gated = 0;
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& s = slots_[static_cast<std::size_t>(i)];
        if (s.playing && s.note == note && s.gated) {
            return i; // retrigger same note
        }
        if (!s.playing && free < 0) {
            free = i;
        }
        if (s.playing && s.gated) {
            ++gated;
            if (s.age < playAge) {
                playAge = s.age;
                oldest = i;
            }
        } else if (s.playing && !s.gated) {
            if (s.age < relAge) {
                relAge = s.age;
                oldestRel = i;
            }
        }
    }
    if (gated < cap && free >= 0) {
        return free;
    }
    // Steal: prefer a releasing voice, else oldest gated. Fade-in the replacement.
    const int steal = oldestRel >= 0 ? oldestRel : oldest;
    if (steal >= 0) {
        ++stealCount_;
        auto& s = slots_[static_cast<std::size_t>(steal)];
        s.playing = false;
        s.gated = false;
        return steal;
    }
    if (free >= 0) {
        return free;
    }
    return 0;
}

void VoiceAllocator::noteOn(int note, int vel, std::uint32_t phraseSeed) noexcept {
    if (!prepared_) {
        return;
    }
    if (polyphony_ <= 1) {
        setMono(note, vel, true, true, false, phraseSeed);
        return;
    }
    const int i = allocSlot(note);
    startSlot(i, note, vel, phraseSeed, true);
}

void VoiceAllocator::noteOff(int note) noexcept {
    if (polyphony_ <= 1) {
        return;
    }
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& s = slots_[static_cast<std::size_t>(i)];
        if (s.playing && s.note == note && s.gated) {
            releaseSlot(i);
            s.fadeSamples = 0;
            s.fadeFrom = 1.0f;
            s.fadeTo = 0.0f;
            s.fadeLen = fadeLen_;
        }
    }
}

void VoiceAllocator::allNotesOff() noexcept {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (slots_[static_cast<std::size_t>(i)].playing) {
            releaseSlot(i);
            auto& s = slots_[static_cast<std::size_t>(i)];
            s.fadeFrom = 1.0f;
            s.fadeTo = 0.0f;
            s.fadeSamples = 0;
            s.fadeLen = fadeLen_;
        }
    }
    monoNote_ = 60;
    monoVel_ = 100;
}

void VoiceAllocator::setMono(int note, int vel, bool gate, bool noteOnEvent, bool noteOffEvent,
                             std::uint32_t phraseSeed) noexcept {
    (void)noteOffEvent;
    auto& s = slots_[0];
    if (!s.prepared) {
        return;
    }
    s.note = note;
    s.vel = vel;
    s.gated = gate;
    s.playing = true;
    if (noteOnEvent) {
        s.phraseSeed = phraseSeed == 0 ? 1u : phraseSeed;
    }
    s.panL = 1.0f;
    s.panR = 1.0f;
    s.sizeMul = 1.0;
    s.toneOff = 0.0;
    monoNote_ = note;
    monoVel_ = vel;
}

void VoiceAllocator::process(float* left, float* right, int n, const cuicui::control::PerformInputs& macros,
                             const cuicui::VoiceSettings& settings, const cuicui::ControlFrame& hostFrame) noexcept {
    if (!prepared_ || n <= 0 || left == nullptr) {
        return;
    }
    std::memset(left, 0, static_cast<std::size_t>(n) * sizeof(float));
    if (right != nullptr && right != left) {
        std::memset(right, 0, static_cast<std::size_t>(n) * sizeof(float));
    }

    const bool mono = polyphony_ <= 1;
    const int nSlots = mono ? 1 : kMaxVoices;
    for (int i = 0; i < nSlots; ++i) {
        auto& s = slots_[static_cast<std::size_t>(i)];
        if (!s.prepared) {
            continue;
        }
        if (!mono && !s.playing) {
            continue;
        }
        if (mono) {
            s.playing = true;
            s.panL = 1.0f;
            s.panR = 1.0f;
            s.sizeMul = 1.0;
            s.toneOff = 0.0;
        } else {
            updatePan(i);
        }

        cuicui::control::GestureEngineContext cx;
        cx.motion = macros.motion;
        cx.range = macros.range;
        cx.speed = macros.speed;
        cx.variation = macros.variation;
        cx.density = macros.density;
        cx.snap = macros.snap;
        cx.tone = std::clamp(macros.tone + s.toneOff * std::clamp(macros.variation, 0.0, 2.0), 0.0, 1.0);
        cx.scale = macros.scale;
        cx.root = macros.root;
        cx.freeze = macros.freeze;
        cx.tempoSync = macros.tempoSync;
        cx.syncDivision = macros.syncDivision;
        cx.tempoValid = macros.tempoValid;
        cx.hostPlaying = macros.hostPlaying;
        cx.bpm = macros.bpm;
        cx.ppq = macros.ppq;
        s.engine.setContext(cx);

        cuicui::VoiceSettings st = settings;
        if (!mono) {
            st.body_size = std::clamp(
                st.body_size * (1.0 + (s.sizeMul - 1.0) * std::clamp(macros.variation, 0.0, 2.0)), 0.25, 4.0);
        }
        s.voice.setSettings(st);
        s.voice.setOutGainRamp(static_cast<float>(st.out_gain_db), static_cast<float>(st.out_gain_db));

        cuicui::control::PerformInputs in = macros;
        if (!mono) {
            in.gate = s.gated;
            in.noteCents = static_cast<double>(std::max(0, s.note)) * 100.0;
            in.velocity01 = static_cast<double>(s.vel) / 127.0;
            in.noteOnEvent = false;
            in.noteOffEvent = false;
            in.phraseSeed = s.phraseSeed;
        }
        in.tone = cx.tone;
        in.size = st.body_size;

        cuicui::ControlFrame to = hostFrame;
        s.controller.tick(in, st, to);

        std::array<float, 64> tmp{};
        int pos = 0;
        cuicui::ControlFrame from = s.lastFrame;
        while (pos < n) {
            const int m = std::min(64, n - pos);
            s.voice.process(from, to, tmp.data(), m);
            from = to;
            for (int k = 0; k < m; ++k) {
                float g = 1.0f;
                if (!mono && s.fadeLen > 0 && s.fadeFrom != s.fadeTo) {
                    const float t =
                        static_cast<float>(s.fadeSamples + k) / static_cast<float>(std::max(1, s.fadeLen));
                    const float u = std::clamp(t, 0.0f, 1.0f);
                    const float w = 0.5f * (1.0f - std::cos(static_cast<float>(std::numbers::pi) * u));
                    g = s.fadeFrom + (s.fadeTo - s.fadeFrom) * w;
                }
                const float y = tmp[static_cast<std::size_t>(k)] * g;
                left[pos + k] += y * s.panL;
                if (right != nullptr) {
                    right[pos + k] += y * s.panR;
                }
            }
            s.fadeSamples += m;
            pos += m;
        }
        s.lastFrame = to;
        if (!mono && s.fadeLen > 0 && s.fadeSamples >= s.fadeLen) {
            s.fadeFrom = s.fadeTo;
            s.fadeIn = false;
            if (!s.gated && s.fadeTo <= 0.0f) {
                s.playing = false;
                s.note = -1;
            }
        }
    }
}

cuicui::control::VoiceController::Debug VoiceAllocator::firstDebug() const noexcept {
    for (const auto& s : slots_) {
        if (s.playing) {
            return s.controller.debug();
        }
    }
    return slots_[0].controller.debug();
}

cuicui::VoiceTelemetry VoiceAllocator::firstTelemetry() const noexcept {
    for (const auto& s : slots_) {
        if (s.playing) {
            return s.voice.telemetry();
        }
    }
    return slots_[0].voice.telemetry();
}

void VoiceAllocator::notifyBlockTime(double elapsedSec, double blockSec) noexcept {
    if (!(blockSec > 0.0)) {
        return;
    }
    if (elapsedSec > 0.70 * blockSec) {
        ++overrunBlocks_;
        if (overrunBlocks_ >= 10) {
            const int cur = std::max(1, playingCount());
            cpuCap_ = cur;
        }
    } else {
        overrunBlocks_ = 0;
    }
}
