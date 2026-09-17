#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "cuicui/model/labial_oscillator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

/// Empirical f0(β) of the labial oscillator at MIDI vel-100 α
/// (α = α_rest + p·(α_max−α_rest) ≈ 0.331). Hopf β=(2πf/γ)² sits near 0.02
/// on piano Hz and makes C≈D. C3 (atlas) replaces this.
constexpr float kF0Hz[] = {
    1337.5f, 1817.5f, 2150.1f, 2405.1f, 2617.6f, 2800.1f, 2960.1f, 3105.1f,
    3237.6f, 3360.1f, 3472.6f, 3580.1f, 3682.6f, 3780.1f, 3875.1f, 3965.1f,
    4050.1f, 4137.6f, 4220.1f, 4300.1f, 4375.1f, 4452.6f, 4527.6f, 4600.1f,
    4670.1f, 4742.6f, 4812.6f, 4880.1f, 4947.6f,
};
constexpr float kBetaAtF0[] = {
    -0.20f, -0.15f, -0.10f, -0.05f, 0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f,
    0.30f,  0.35f,  0.40f,  0.45f, 0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f,
    0.80f,  0.85f,  0.90f,  0.95f, 1.00f, 1.05f, 1.10f, 1.15f, 1.20f,
};
constexpr int kF0Knots = static_cast<int>(sizeof(kF0Hz) / sizeof(kF0Hz[0]));
constexpr float kMidiC4Hz = 0.5f * 4947.6f; // C5 hits the model ceiling (~2×)

float betaFromTargetHz(float fTarget) noexcept {
    const float f = std::clamp(fTarget, kF0Hz[0], kF0Hz[kF0Knots - 1]);
    int i = 0;
    while (i + 1 < kF0Knots && kF0Hz[i + 1] < f) {
        ++i;
    }
    if (i + 1 >= kF0Knots) {
        return kBetaAtF0[kF0Knots - 1];
    }
    const float f0 = kF0Hz[i];
    const float f1 = kF0Hz[i + 1];
    const float t = (f1 > f0) ? (f - f0) / (f1 - f0) : 0.0f;
    return kBetaAtF0[i] + t * (kBetaAtF0[i + 1] - kBetaAtF0[i]);
}

float crudeBetaFromMidi(float note, float bendSt) noexcept {
    const float n = note + bendSt;
    const float f0 = kMidiC4Hz * std::pow(2.0f, (n - 60.0f) / 12.0f);
    return betaFromTargetHz(f0);
}

} // namespace

SyrinxAudioProcessor::SyrinxAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()) {
    cacheParamPointers();
}

SyrinxAudioProcessor::~SyrinxAudioProcessor() = default;

void SyrinxAudioProcessor::cacheParamPointers() {
    for (int i = 0; i < cuicui::kParamCount; ++i) {
        raw_[static_cast<std::size_t>(i)] = apvts.getRawParameterValue(cuicui::kParamTable[static_cast<std::size_t>(i)].id);
        jassert(raw_[static_cast<std::size_t>(i)] != nullptr);
    }
    seedParam_ = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("engine.seed"));
    osParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("engine.oversampling"));
}

float SyrinxAudioProcessor::raw(cuicui::ParamId id) const noexcept {
    auto* p = raw_[static_cast<std::size_t>(id)];
    return p != nullptr ? p->load(std::memory_order_relaxed) : static_cast<float>(cuicui::specOf(id).def);
}

int SyrinxAudioProcessor::readOversampling() const noexcept {
    if (osParam_ == nullptr) {
        return 0;
    }
    return cuicui::oversamplingValueFromChoiceIndex(osParam_->getIndex());
}

std::uint64_t SyrinxAudioProcessor::readSeed() const noexcept {
    if (seedParam_ != nullptr) {
        return static_cast<std::uint64_t>(std::max(0, seedParam_->get()));
    }
    return 1;
}

void SyrinxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    cacheParamPointers();
    const int maxBlock = std::max(kSubBlock, std::max(8192, samplesPerBlock));
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = sampleRate;
    spec.maxBlockSize = kSubBlock;
    spec.oversampling = readOversampling();

    voice_.prepare(spec);
    voice_.setSource(cuicui::VoiceSource::Labial);
    mono_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    masterSm_.prepare(sampleRate);
    masterSm_.setTimeConstantMs(cuicui::specOf(cuicui::ParamId::OutMasterDb).smoothingMs);
    limiter_.reset();
    nHeld_ = 0;
    bendSt_ = 0.0f;
    betaDriven_ = false;
    midiPressure_ = 0.0f;
    prepared_ = true;

    primeVoice();
    lastFrame_ = readControlFrame();
    applyNotePhysics(lastFrame_);
    masterSm_.snap(raw(cuicui::ParamId::OutMasterDb));
    setLatencySamples(voice_.latencySamples());
}

void SyrinxAudioProcessor::primeVoice() noexcept {
    const auto st = readSettings();
    voice_.setSettings(st);
    voice_.reset(st.seed);
    voice_.setOutGainRamp(static_cast<float>(st.out_gain_db), static_cast<float>(st.out_gain_db));
    const double off = static_cast<double>(raw(cuicui::ParamId::SrcLAlphaOffset));
    const double beta = static_cast<double>(raw(cuicui::ParamId::SrcLBeta));
    voice_.primeLabial(cuicui::model::kAlphaRest + off, beta, static_cast<double>(raw(cuicui::ParamId::OecFreqHz)),
                       static_cast<double>(raw(cuicui::ParamId::BeakGape)),
                       static_cast<double>(raw(cuicui::ParamId::SrcRAlphaOffset)),
                       static_cast<double>(raw(cuicui::ParamId::SrcRBeta)));
}

void SyrinxAudioProcessor::releaseResources() {
    prepared_ = false;
}

bool SyrinxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.getMainInputChannelSet().isDisabled()) {
        return false;
    }
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

cuicui::ControlFrame SyrinxAudioProcessor::readControlFrame() const noexcept {
    cuicui::ControlFrame f;
    auto cl = [](cuicui::ParamId id, float v) {
        return static_cast<float>(cuicui::clampParam(cuicui::specOf(id), static_cast<double>(v)));
    };
    f.resp_pressure = cl(cuicui::ParamId::RespPressure, raw(cuicui::ParamId::RespPressure));
    f.src_L_beta = cl(cuicui::ParamId::SrcLBeta, raw(cuicui::ParamId::SrcLBeta));
    f.src_R_beta = cl(cuicui::ParamId::SrcRBeta, raw(cuicui::ParamId::SrcRBeta));
    f.src_L_gate = cl(cuicui::ParamId::SrcLGate, raw(cuicui::ParamId::SrcLGate));
    f.src_R_gate = cl(cuicui::ParamId::SrcRGate, raw(cuicui::ParamId::SrcRGate));
    f.src_L_alpha_offset = cl(cuicui::ParamId::SrcLAlphaOffset, raw(cuicui::ParamId::SrcLAlphaOffset));
    f.src_R_alpha_offset = cl(cuicui::ParamId::SrcRAlphaOffset, raw(cuicui::ParamId::SrcRAlphaOffset));
    f.oec_freq_hz = cl(cuicui::ParamId::OecFreqHz, raw(cuicui::ParamId::OecFreqHz));
    f.beak_gape = cl(cuicui::ParamId::BeakGape, raw(cuicui::ParamId::BeakGape));
    f.noise_beta_sigma = cl(cuicui::ParamId::NoiseBetaSigma, raw(cuicui::ParamId::NoiseBetaSigma));
    return f;
}

cuicui::VoiceSettings SyrinxAudioProcessor::readSettings() const noexcept {
    cuicui::VoiceSettings s;
    auto cl = [](cuicui::ParamId id, float v) { return cuicui::clampParam(cuicui::specOf(id), static_cast<double>(v)); };
    s.body_size = cl(cuicui::ParamId::BodySize, raw(cuicui::ParamId::BodySize));
    s.src_gamma = cl(cuicui::ParamId::SrcGamma, raw(cuicui::ParamId::SrcGamma));
    s.trachea_length_mm = cl(cuicui::ParamId::TracheaLengthMm, raw(cuicui::ParamId::TracheaLengthMm));
    s.trachea_reflection = cl(cuicui::ParamId::TracheaReflection, raw(cuicui::ParamId::TracheaReflection));
    s.trachea_loss = cl(cuicui::ParamId::TracheaLoss, raw(cuicui::ParamId::TracheaLoss));
    s.trachea_input_gain = cl(cuicui::ParamId::TracheaInputGain, raw(cuicui::ParamId::TracheaInputGain));
    s.oec_damping = cl(cuicui::ParamId::OecDamping, raw(cuicui::ParamId::OecDamping));
    s.beak_damping = cl(cuicui::ParamId::BeakDamping, raw(cuicui::ParamId::BeakDamping));
    s.out_radiation = cl(cuicui::ParamId::OutRadiation, raw(cuicui::ParamId::OutRadiation));
    s.out_gain_db = cl(cuicui::ParamId::OutGainDb, raw(cuicui::ParamId::OutGainDb));
    s.resp_attack_ms = cl(cuicui::ParamId::RespAttackMs, raw(cuicui::ParamId::RespAttackMs));
    s.resp_release_ms = cl(cuicui::ParamId::RespReleaseMs, raw(cuicui::ParamId::RespReleaseMs));
    s.noise_beta_bandwidth_hz = cl(cuicui::ParamId::NoiseBetaBandwidthHz, raw(cuicui::ParamId::NoiseBetaBandwidthHz));
    s.noise_aspiration = cl(cuicui::ParamId::NoiseAspiration, raw(cuicui::ParamId::NoiseAspiration));
    s.src_coupling = cl(cuicui::ParamId::SrcCoupling, raw(cuicui::ParamId::SrcCoupling));
    s.oversampling = readOversampling();
    s.seed = readSeed();
    s.test_waveform = raw(cuicui::ParamId::TestWaveform);
    return s;
}

void SyrinxAudioProcessor::handleMidi(const juce::MidiMessage& msg) noexcept {
    if (msg.isNoteOn()) {
        const int note = msg.getNoteNumber();
        for (int i = 0; i < nHeld_; ++i) {
            if (heldNotes_[i] == note) {
                for (int j = i; j < nHeld_ - 1; ++j) {
                    heldNotes_[j] = heldNotes_[j + 1];
                }
                --nHeld_;
                break;
            }
        }
        const bool legato = nHeld_ > 0;
        if (nHeld_ < kMaxHeldNotes) {
            heldNotes_[nHeld_++] = note;
        } else {
            heldNotes_[kMaxHeldNotes - 1] = note;
        }
        const float vel = static_cast<float>(msg.getVelocity()) / 127.0f;
        if (!legato) {
            midiPressure_ = 0.35f + 0.55f * vel;
        }
        betaDriven_ = true;
    } else if (msg.isNoteOff()) {
        const int note = msg.getNoteNumber();
        for (int i = 0; i < nHeld_; ++i) {
            if (heldNotes_[i] == note) {
                for (int j = i; j < nHeld_ - 1; ++j) {
                    heldNotes_[j] = heldNotes_[j + 1];
                }
                --nHeld_;
                break;
            }
        }
        if (nHeld_ == 0) {
            midiPressure_ = 0.0f;
            // β holds (betaDriven_ stays true until a new param-only path; keep last midiBeta_).
        }
    } else if (msg.isPitchWheel()) {
        const int pw = msg.getPitchWheelValue();
        bendSt_ = 2.0f * (static_cast<float>(pw - 8192) / 8192.0f);
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
        nHeld_ = 0;
        midiPressure_ = 0.0f;
        bendSt_ = 0.0f;
    }
}

void SyrinxAudioProcessor::applyNotePhysics(cuicui::ControlFrame& frame) const noexcept {
    if (nHeld_ > 0) {
        const int note = heldNotes_[nHeld_ - 1];
        const float beta = crudeBetaFromMidi(static_cast<float>(note), bendSt_);
        frame.resp_pressure = midiPressure_;
        frame.src_L_beta = static_cast<float>(
            cuicui::clampParam(cuicui::specOf(cuicui::ParamId::SrcLBeta), static_cast<double>(beta)));
    } else if (betaDriven_) {
        frame.resp_pressure = midiPressure_;
        frame.src_L_beta = midiBeta_;
    }
}

void SyrinxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer(midi, 0, std::max(0, n), true);
    const int nOut = buffer.getNumChannels();
    for (int ch = 0; ch < nOut; ++ch) {
        buffer.clear(ch, 0, n);
    }
    if (!prepared_ || n <= 0 || mono_.empty()) {
        midi.clear();
        return;
    }

    voice_.setSettings(readSettings());
    {
        const float gdb = raw(cuicui::ParamId::OutGainDb);
        voice_.setOutGainRamp(gdb, gdb);
    }

    int offset = 0;
    while (offset < n) {
        const int chunk = std::min(n - offset, static_cast<int>(mono_.size()));
        processSubblocks(mono_.data(), chunk, midi, offset);
        auto* left = buffer.getWritePointer(0);
        std::memcpy(left + offset, mono_.data(), static_cast<std::size_t>(chunk) * sizeof(float));
        if (nOut > 1) {
            auto* right = buffer.getWritePointer(1);
            std::memcpy(right + offset, mono_.data(), static_cast<std::size_t>(chunk) * sizeof(float));
        }
        offset += chunk;
    }

    publishTelemetry();
    midi.clear();
}

void SyrinxAudioProcessor::processSubblocks(float* dest, int n, const juce::MidiBuffer& midi,
                                            int timeOffset) noexcept {
    int pos = 0;
    auto it = midi.cbegin();
    const auto midiEnd = midi.cend();
    const int t0 = timeOffset;
    const int t1 = timeOffset + n;
    while (it != midiEnd && (*it).samplePosition < t0) {
        ++it;
    }
    while (pos < n) {
        while (it != midiEnd && (*it).samplePosition <= t0 + pos) {
            handleMidi((*it).getMessage());
            if (nHeld_ > 0) {
                midiBeta_ = static_cast<float>(cuicui::clampParam(
                    cuicui::specOf(cuicui::ParamId::SrcLBeta),
                    static_cast<double>(crudeBetaFromMidi(static_cast<float>(heldNotes_[nHeld_ - 1]), bendSt_))));
            }
            ++it;
        }
        int nextMidi = (it != midiEnd && (*it).samplePosition < t1) ? ((*it).samplePosition - t0) : n;
        int end = pos + kSubBlock;
        if (end > n) {
            end = n;
        }
        if (nextMidi > pos && nextMidi < end) {
            end = nextMidi;
        }
        if (end <= pos) {
            end = pos + 1;
        }

        cuicui::ControlFrame to = readControlFrame();
        applyNotePhysics(to);
        if (nHeld_ > 0) {
            midiBeta_ = to.src_L_beta;
        }

        const int count = end - pos;
        const float gdb = raw(cuicui::ParamId::OutGainDb);
        voice_.setOutGainRamp(gdb, gdb);
        voice_.process(lastFrame_, to, dest + pos, count);

        masterSm_.setTarget(raw(cuicui::ParamId::OutMasterDb));
        for (int i = 0; i < count; ++i) {
            const float g = juce::Decibels::decibelsToGain(masterSm_.next());
            dest[pos + i] = SafetyLimiter::processSample(dest[pos + i] * g);
        }

        lastFrame_ = to;
        pos = end;
    }
}

void SyrinxAudioProcessor::publishTelemetry() noexcept {
    const auto t = voice_.telemetry();
    telem_f0_.store(t.f0_estimate_hz, std::memory_order_relaxed);
    telem_aL_.store(t.alpha_L, std::memory_order_relaxed);
    telem_aR_.store(t.alpha_R, std::memory_order_relaxed);
    telem_bL_.store(t.beta_L, std::memory_order_relaxed);
    telem_bR_.store(t.beta_R, std::memory_order_relaxed);
    telem_p_.store(t.pressure, std::memory_order_relaxed);
    telem_level_.store(t.level_db, std::memory_order_relaxed);
    telem_resets_.store(t.nonFiniteResets, std::memory_order_relaxed);
}

SyrinxAudioProcessor::TelemetrySnapshot SyrinxAudioProcessor::telemetrySnapshot() const noexcept {
    TelemetrySnapshot s;
    s.f0_hz = telem_f0_.load(std::memory_order_relaxed);
    s.alpha_L = telem_aL_.load(std::memory_order_relaxed);
    s.alpha_R = telem_aR_.load(std::memory_order_relaxed);
    s.beta_L = telem_bL_.load(std::memory_order_relaxed);
    s.beta_R = telem_bR_.load(std::memory_order_relaxed);
    s.pressure = telem_p_.load(std::memory_order_relaxed);
    s.level_db = telem_level_.load(std::memory_order_relaxed);
    s.nonFiniteResets = telem_resets_.load(std::memory_order_relaxed);
    return s;
}

bool SyrinxAudioProcessor::setParamPlain(std::string_view id, float value) {
    auto* p = apvts.getParameter(juce::String(id.data(), id.size()));
    if (p == nullptr) {
        return false;
    }
    if (auto* pf = dynamic_cast<juce::AudioParameterFloat*>(p)) {
        *pf = value;
        return true;
    }
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(p)) {
        *pi = static_cast<int>(std::lround(value));
        return true;
    }
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*>(p)) {
        int idx = static_cast<int>(std::lround(value));
        if (id == "engine.oversampling") {
            idx = cuicui::oversamplingChoiceIndexFromValue(static_cast<int>(std::lround(value)));
        }
        *pc = idx;
        return true;
    }
    return false;
}

void SyrinxAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto xml = apvts.copyState().createXml();
    if (xml != nullptr) {
        xml->setAttribute("stateVersion", kStateVersion);
        copyXmlToBinary(*xml, destData);
    }
}

void SyrinxAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) {
        return;
    }
    auto tree = juce::ValueTree::fromXml(*xml);
    if (tree.isValid()) {
        apvts.replaceState(tree);
    }
}

juce::AudioProcessorEditor* SyrinxAudioProcessor::createEditor() {
    return new SyrinxAudioProcessorEditor(*this);
}

// JUCE plugin entry point (formats). plugin_render / tests construct the processor directly.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SyrinxAudioProcessor();
}
