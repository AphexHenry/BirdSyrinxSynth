#include "PluginProcessor.h"
#include "BinaryData.h"
#include "PluginEditor.h"
#include "cuicui/control/gesture_engine.hpp"
#include "cuicui/io/syllable_bank_io.hpp"
#include "cuicui/model/labial_oscillator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

/// Empirical f0(β) of the labial oscillator at MIDI vel-100 α. Physiology mode only.
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
constexpr float kMidiC4Hz = 0.5f * 4947.6f;

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

/// MIDI → playing-mode event (WP-R1). Returns false for messages the whistle path ignores.
bool toPlayEvent(const juce::MidiMessage& m, int offset, cuicui::control::PlayEvent& e) noexcept {
    using Type = cuicui::control::PlayEvent::Type;
    e.offset = std::max(0, offset);
    e.note = 60;
    e.value = 0.0f;
    if (m.isNoteOn()) {
        e.type = Type::NoteOn;
        e.note = m.getNoteNumber();
        e.value = m.getFloatVelocity();
        return true;
    }
    if (m.isNoteOff()) {
        e.type = Type::NoteOff;
        e.note = m.getNoteNumber();
        return true;
    }
    if (m.isAllNotesOff()) {
        e.type = Type::AllNotesOff;
        return true;
    }
    if (m.isAllSoundOff()) {
        e.type = Type::AllSoundOff;
        return true;
    }
    if (m.isController()) {
        const float v = static_cast<float>(m.getControllerValue()) / 127.0f;
        if (m.getControllerNumber() == 64) {
            e.type = Type::Sustain;
            e.value = v;
            return true;
        }
        if (m.getControllerNumber() == 1) {
            e.type = Type::ModWheel;
            e.value = v;
            return true;
        }
    }
    return false;
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
    try { // WP-R3 (D-036): one bank per bird, merged here
        const auto a = cuicui::io::parseSyllableBank(std::string_view(
            BinaryData::hf_22938_bank_json, static_cast<std::size_t>(BinaryData::hf_22938_bank_jsonSize)));
        const auto b = cuicui::io::parseSyllableBank(std::string_view(
            BinaryData::hf_22937_bank_json, static_cast<std::size_t>(BinaryData::hf_22937_bank_jsonSize)));
        bank_ = cuicui::control::mergeBanks({&a, &b});
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("Syrinx: the syllable banks did not load: ") + e.what());
    }
    presets_ = std::make_unique<PresetManager>(apvts);
    presets_->setVoiceBankName(juce::String(bank_.name()));
}

void SyrinxAudioProcessor::useBankFiles(const std::vector<std::string>& paths) {
    std::vector<cuicui::control::SyllableBank> banks;
    for (const auto& path : paths) {
        banks.push_back(cuicui::io::loadSyllableBank(path));
    }
    std::vector<const cuicui::control::SyllableBank*> ptrs;
    for (const auto& b : banks) {
        ptrs.push_back(&b);
    }
    auto merged = ptrs.size() == 1 ? std::move(banks.front()) : cuicui::control::mergeBanks(ptrs);
    whistle_.setBank(nullptr);
    bank_ = std::move(merged);
    whistle_.setBank(&bank_);
    presets_->setVoiceBankName(juce::String(bank_.name()));
}

SyrinxAudioProcessor::~SyrinxAudioProcessor() = default;

void SyrinxAudioProcessor::cacheParamPointers() {
    for (int i = 0; i < cuicui::kParamCount; ++i) {
        raw_[static_cast<std::size_t>(i)] = apvts.getRawParameterValue(cuicui::kParamTable[static_cast<std::size_t>(i)].id);
        jassert(raw_[static_cast<std::size_t>(i)] != nullptr);
    }
    seedParam_ = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("engine.seed"));
    osParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("engine.oversampling"));
    modeParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.mode"));
    scaleParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.scale"));
    syncDivParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.sync_division"));
    polyParam_ = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("perf.polyphony"));
    perfSeedParam_ = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("perf.seed"));
    voiceParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("voice.type"));
    ruleParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.note_rule"));
    mappingParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.mapping"));
    orderParam_ = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("perf.order"));
}

bool SyrinxAudioProcessor::whistleSelected() const noexcept {
    return choiceIndex(voiceParam_) == 0;
}

std::uint64_t SyrinxAudioProcessor::whistleSeed() const noexcept {
    const int perf = perfSeedParam_ != nullptr ? perfSeedParam_->get() : 0;
    return perf > 0 ? static_cast<std::uint64_t>(perf) : readSeed();
}

cuicui::control::WhistleSettings SyrinxAudioProcessor::readWhistleSettings() const noexcept {
    using namespace cuicui::control;
    WhistleSettings s;
    s.play.rule = static_cast<NoteRule>(std::clamp(choiceIndex(ruleParam_), 0, kNoteRuleCount - 1));
    s.play.mapping = choiceIndex(mappingParam_) == 1 ? KeyMapping::Follow : KeyMapping::OwnHeight;
    s.play.sizeSt = static_cast<double>(raw(cuicui::ParamId::PerfSizeSt));
    s.play.firstOnNote = raw(cuicui::ParamId::PerfFirstOnNote) >= 0.5f;
    s.play.snap = static_cast<double>(raw(cuicui::ParamId::PerfSnap));
    s.play.order = static_cast<OrderMode>(std::clamp(choiceIndex(orderParam_), 0, 2));
    s.play.pauseS = static_cast<double>(raw(cuicui::ParamId::PerfPauseS));
    s.play.polyphony = polyParam_ != nullptr ? polyParam_->get() : 4;
    s.play.spread = static_cast<double>(raw(cuicui::ParamId::OutStereoSpread));
    s.play.releaseAlarm = releaseAlarm_.load(std::memory_order_relaxed);
    s.play.variation = static_cast<double>(raw(cuicui::ParamId::PerfNaturalVariation));
    s.harmonics = raw(cuicui::ParamId::VoiceHarmonics) >= 0.5f;
    s.room = raw(cuicui::ParamId::OutRoom) >= 0.5f;
    return s;
}

float SyrinxAudioProcessor::raw(cuicui::ParamId id) const noexcept {
    auto* p = raw_[static_cast<std::size_t>(id)];
    return p != nullptr ? p->load(std::memory_order_relaxed) : static_cast<float>(cuicui::specOf(id).def);
}

int SyrinxAudioProcessor::choiceIndex(juce::AudioParameterChoice* p) const noexcept {
    return p != nullptr ? p->getIndex() : 0;
}

bool SyrinxAudioProcessor::performMode() const noexcept {
    return choiceIndex(modeParam_) == 0;
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
    const double ctrlRate = sampleRate / static_cast<double>(kSubBlock);
    controller_.prepare(ctrlRate, map_);
    allocator_.prepare(spec, map_);
    allocator_.reset(readSeed());
    mono_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    right_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    phraseCounter_ = 1;
    masterSm_.prepare(sampleRate);
    masterSm_.setTimeConstantMs(cuicui::specOf(cuicui::ParamId::OutMasterDb).smoothingMs);
    limiter_.reset();
    nHeld_ = 0;
    bendNorm_ = 0.0f;
    betaDriven_ = false;
    midiPressurePhys_ = 0.0f;
    aftertouch_ = 0.0f;
    polyAt_ = 0.0f;
    cc1_ = 0.0f;
    cc74_ = 0.5f;
    sustain_ = false;
    wasGated_ = false;
    prepared_ = true;

    lastFrame_ = readControlFrame();
    primeVoice();
    if (!performMode()) {
        applyPhysiologyNote(lastFrame_);
    }
    masterSm_.snap(raw(cuicui::ParamId::OutMasterDb));
    setLatencySamples(voice_.latencySamples());

    // WP-R1 whistle path. It is delayed by the anatomical latency so the plugin's latency never changes.
    whistle_.prepare(sampleRate);
    whistle_.setBank(&bank_);
    whistle_.reset(whistleSeed());
    { // every syllable a process() call can start fits: kMaxRequests per internal chunk
        const int chunks = (maxBlock + cuicui::control::SyllableEngine::kChunk - 1) / cuicui::control::SyllableEngine::kChunk;
        blockLog_.assign(static_cast<std::size_t>(chunks * cuicui::control::SyllableEngine::kMaxRequests), {});
    }
    extLogCount_ = 0;
    feed_.reset();
    wl_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    wr_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    fadeL_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    fadeR_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    const int lat = std::max(0, voice_.latencySamples());
    delayL_.assign(static_cast<std::size_t>(lat), 0.0f);
    delayR_.assign(static_cast<std::size_t>(lat), 0.0f);
    delayPos_ = 0;
    whistleMasterSm_.prepare(sampleRate);
    whistleMasterSm_.setTimeConstantMs(cuicui::specOf(cuicui::ParamId::OutMasterDb).smoothingMs);
    whistleMasterSm_.snap(raw(cuicui::ParamId::OutMasterDb));
    lastWhistle_ = whistleSelected();
    anatomicalFadeLeft_ = 0;
    anatomicalFadeLen_ = std::max(1, static_cast<int>(std::lround(0.010 * sampleRate)));
}

void SyrinxAudioProcessor::primeVoice() noexcept {
    const auto st = readSettings();
    voice_.setSettings(st);
    voice_.reset(st.seed);
    voice_.setOutGainRamp(static_cast<float>(st.out_gain_db), static_cast<float>(st.out_gain_db));
    const double off = static_cast<double>(raw(cuicui::ParamId::SrcLAlphaOffset));
    const double beta = static_cast<double>(raw(cuicui::ParamId::SrcLBeta));
    const double oec = static_cast<double>(raw(cuicui::ParamId::OecFreqHz));
    const double gape = static_cast<double>(raw(cuicui::ParamId::BeakGape));
    const double rOff = static_cast<double>(raw(cuicui::ParamId::SrcRAlphaOffset));
    const double rBeta = static_cast<double>(raw(cuicui::ParamId::SrcRBeta));
    voice_.primeLabial(cuicui::model::kAlphaRest + off, beta, oec, gape, rOff, rBeta);
    allocator_.prime(st, cuicui::model::kAlphaRest + off, beta, oec, gape, rOff, rBeta, lastFrame_);
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

void SyrinxAudioProcessor::pushHeld(int note, int vel) noexcept {
    for (int i = 0; i < nHeld_; ++i) {
        if (held_[i].note == note) {
            for (int j = i; j < nHeld_ - 1; ++j) {
                held_[j] = held_[j + 1];
            }
            --nHeld_;
            break;
        }
    }
    if (nHeld_ < kMaxHeldNotes) {
        held_[nHeld_++] = HeldNote{note, vel};
    } else {
        held_[kMaxHeldNotes - 1] = HeldNote{note, vel};
    }
    lastNote_ = note;
    lastVel_ = vel;
}

void SyrinxAudioProcessor::popHeld(int note) noexcept {
    for (int i = 0; i < nHeld_; ++i) {
        if (held_[i].note == note) {
            for (int j = i; j < nHeld_ - 1; ++j) {
                held_[j] = held_[j + 1];
            }
            --nHeld_;
            break;
        }
    }
    if (nHeld_ > 0) {
        lastNote_ = held_[nHeld_ - 1].note;
        lastVel_ = held_[nHeld_ - 1].vel;
    }
}

void SyrinxAudioProcessor::handleMidi(const juce::MidiMessage& msg) noexcept {
    if (msg.isNoteOn()) {
        const int vel = msg.getVelocity();
        if (vel <= 0) {
            popHeld(msg.getNoteNumber());
            if (nHeld_ == 0 && !sustain_) {
                midiPressurePhys_ = 0.0f;
            }
            return;
        }
        pushHeld(msg.getNoteNumber(), vel);
        if (nHeld_ == 1) {
            midiPressurePhys_ = 0.35f + 0.55f * (static_cast<float>(vel) / 127.0f);
        }
        betaDriven_ = true;
        ++phraseCounter_;
        if (performMode() && polyParam_ != nullptr && polyParam_->get() > 1) {
            allocator_.noteOn(msg.getNoteNumber(), vel, makePhraseSeed(msg.getNoteNumber()));
        }
    } else if (msg.isNoteOff()) {
        popHeld(msg.getNoteNumber());
        if (performMode() && polyParam_ != nullptr && polyParam_->get() > 1) {
            allocator_.noteOff(msg.getNoteNumber());
        }
        popHeld(msg.getNoteNumber());
        if (nHeld_ == 0 && !sustain_) {
            midiPressurePhys_ = 0.0f;
        }
    } else if (msg.isPitchWheel()) {
        const int pw = msg.getPitchWheelValue();
        bendNorm_ = static_cast<float>(pw - 8192) / 8192.0f;
    } else if (msg.isChannelPressure()) {
        aftertouch_ = static_cast<float>(msg.getChannelPressureValue()) / 127.0f;
    } else if (msg.isAftertouch()) {
        polyAt_ = static_cast<float>(msg.getAfterTouchValue()) / 127.0f;
    } else if (msg.isController()) {
        const int cc = msg.getControllerNumber();
        const float v = static_cast<float>(msg.getControllerValue()) / 127.0f;
        if (cc == 1) {
            cc1_ = v;
        } else if (cc == 74) {
            cc74_ = v;
        } else if (cc == 64) {
            sustain_ = msg.getControllerValue() >= 64;
            if (!sustain_ && nHeld_ == 0) {
                midiPressurePhys_ = 0.0f;
            }
        }
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
        nHeld_ = 0;
        midiPressurePhys_ = 0.0f;
        bendNorm_ = 0.0f;
        sustain_ = false;
        aftertouch_ = 0.0f;
        polyAt_ = 0.0f;
        allocator_.allNotesOff();
    }
}

void SyrinxAudioProcessor::applyPhysiologyNote(cuicui::ControlFrame& frame) noexcept {
    const float bendSt = 2.0f * bendNorm_;
    if (nHeld_ > 0) {
        const int note = held_[nHeld_ - 1].note;
        const float beta = crudeBetaFromMidi(static_cast<float>(note), bendSt);
        frame.resp_pressure = midiPressurePhys_;
        frame.src_L_beta = static_cast<float>(
            cuicui::clampParam(cuicui::specOf(cuicui::ParamId::SrcLBeta), static_cast<double>(beta)));
        midiBeta_ = frame.src_L_beta;
    } else if (betaDriven_) {
        frame.resp_pressure = midiPressurePhys_;
        frame.src_L_beta = midiBeta_;
    }
}

void SyrinxAudioProcessor::fillPerformInputs(cuicui::control::PerformInputs& in, bool noteOnEvent,
                                             bool noteOffEvent) noexcept {
    const bool gated = nHeld_ > 0 || sustain_;
    in.gate = gated;
    in.noteCents = static_cast<double>(lastNote_) * 100.0;
    const double bendSt = static_cast<double>(raw(cuicui::ParamId::PerfBendRangeSt));
    in.bendCents = static_cast<double>(bendNorm_) * bendSt * 100.0;
    in.velocity01 = static_cast<double>(lastVel_) / 127.0;
    in.pressure01 = static_cast<double>(std::max(aftertouch_, polyAt_));
    in.breath = static_cast<double>(raw(cuicui::ParamId::PerfBreath));
    const double toneHost = static_cast<double>(raw(cuicui::ParamId::PerfTone));
    in.tone = std::clamp(toneHost + (static_cast<double>(cc74_) - 0.5), 0.0, 1.0);
    in.physics = static_cast<double>(raw(cuicui::ParamId::PerfPhysics));
    const double motionHost = static_cast<double>(raw(cuicui::ParamId::PerfMotion));
    in.motion = std::clamp(motionHost + static_cast<double>(cc1_), 0.0, 1.0);
    in.range = static_cast<double>(raw(cuicui::ParamId::PerfRange));
    in.speed = static_cast<double>(raw(cuicui::ParamId::PerfSpeed));
    in.snap = static_cast<double>(raw(cuicui::ParamId::PerfSnap));
    in.glideMs = static_cast<double>(raw(cuicui::ParamId::PerfGlideMs));
    in.size = static_cast<double>(raw(cuicui::ParamId::BodySize));
    in.scale = choiceIndex(scaleParam_);
    in.root = static_cast<int>(std::lround(raw(cuicui::ParamId::PerfRoot)));
    in.oecTracking = static_cast<double>(raw(cuicui::ParamId::PerfOecTracking));
    in.gapeTracking = static_cast<double>(raw(cuicui::ParamId::PerfGapeTracking));
    in.sideSwitchCents = static_cast<double>(raw(cuicui::ParamId::PerfSideSwitchCents));
    in.rangeFold = raw(cuicui::ParamId::PerfRangeFold) >= 0.5f;
    in.leash = static_cast<double>(raw(cuicui::ParamId::PerfLeash));
    in.variation = static_cast<double>(raw(cuicui::ParamId::PerfVariation));
    in.density = static_cast<double>(raw(cuicui::ParamId::PerfDensity));
    in.freeze = raw(cuicui::ParamId::PerfFreeze) >= 0.5f;
    in.tempoSync = raw(cuicui::ParamId::PerfTempoSync) >= 0.5f;
    in.syncDivision = choiceIndex(syncDivParam_);
    in.tempoValid = hostTempoValid_;
    in.hostPlaying = hostPlaying_;
    in.bpm = hostBpm_;
    in.ppq = hostPpq_;
    in.seed = readSeed();
    in.phraseSeed = makePhraseSeed(lastNote_);
    in.noteOnEvent = noteOnEvent;
    in.noteOffEvent = noteOffEvent;
}

std::uint32_t SyrinxAudioProcessor::makePhraseSeed(int note) noexcept {
    const std::uint32_t perf =
        perfSeedParam_ != nullptr ? static_cast<std::uint32_t>(std::max(0, perfSeedParam_->get())) : 0u;
    const std::uint32_t c = phraseCounter_;
    if (perf != 0) {
        return cuicui::control::hashPhraseSeed(perf, note, c);
    }
    return cuicui::control::hashPhraseSeed(static_cast<std::uint32_t>(readSeed()) ^ 0xC50u, note, c);
}

void SyrinxAudioProcessor::readHostTempo() noexcept {
    hostTempoValid_ = false;
    hostPlaying_ = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            hostTempoValid_ = true;
            hostBpm_ = pos->getBpm().orFallback(120.0);
            hostPpq_ = pos->getPpqPosition().orFallback(0.0);
            hostPlaying_ = pos->getIsPlaying();
        }
    }
}

void SyrinxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (!midi.isEmpty()) { // MIDI from outside (before the on-screen keyboard adds its own): editor's MIDI light
        int ons = 0;
        for (const auto meta : midi) { // raw bytes: no MidiMessage copy on the audio thread
            ons += (meta.numBytes >= 3 && (meta.data[0] & 0xf0) == 0x90 && meta.data[2] > 0) ? 1 : 0;
        }
        feed_.countExternalNotes(ons);
    }
    keyboardState.processNextMidiBuffer(midi, 0, std::max(0, n), true);
    const int nOut = buffer.getNumChannels();
    for (int ch = 0; ch < nOut; ++ch) {
        buffer.clear(ch, 0, n);
    }
    if (!prepared_ || n <= 0 || mono_.empty()) {
        midi.clear();
        return;
    }

    const bool whistle = whistleSelected();
    if (whistle != lastWhistle_) {
        switchVoice(whistle);
        lastWhistle_ = whistle;
    }
    if (whistle) {
        processWhistle(buffer, midi, false);
        if (anatomicalFadeLeft_ > 0) { // the anatomical voice fades out over 10 ms after a switch
            const int m = std::min(n, static_cast<int>(fadeL_.size()));
            float* ptrs[2] = {fadeL_.data(), fadeR_.data()};
            juce::AudioBuffer<float> tail(ptrs, 2, m);
            juce::MidiBuffer none;
            processAnatomical(tail, none);
            for (int i = 0; i < m; ++i) {
                const float g = static_cast<float>(std::max(0, anatomicalFadeLeft_ - i)) /
                                static_cast<float>(anatomicalFadeLen_);
                for (int ch = 0; ch < nOut; ++ch) {
                    auto* d = buffer.getWritePointer(ch);
                    d[i] = SafetyLimiter::processSample(d[i] + g * (ch == 0 ? fadeL_ : fadeR_)[static_cast<std::size_t>(i)]);
                }
            }
            anatomicalFadeLeft_ = std::max(0, anatomicalFadeLeft_ - m);
        }
    } else {
        processAnatomical(buffer, midi);
        if (whistle_.busy()) { // whistle syllables still sounding after a switch finish naturally
            juce::MidiBuffer none;
            processWhistle(buffer, none, true);
        }
    }
    publishTelemetry();
    midi.clear();
}

void SyrinxAudioProcessor::switchVoice(bool toWhistle) noexcept {
    if (toWhistle) {
        anatomicalFadeLeft_ = anatomicalFadeLen_;
        allocator_.allNotesOff();
        nHeld_ = 0;
        sustain_ = false;
        midiPressurePhys_ = 0.0f;
        betaDriven_ = false;
    } else {
        whistle_.releaseAll();
    }
}

void SyrinxAudioProcessor::processWhistle(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                                          bool add) noexcept {
    const int n = buffer.getNumSamples();
    const int nOut = buffer.getNumChannels();
    const auto settings = readWhistleSettings();
    whistleMasterSm_.setTarget(raw(cuicui::ParamId::OutMasterDb));
    const int cap = static_cast<int>(wl_.size());
    const int lat = static_cast<int>(delayL_.size());
    auto it = midi.cbegin();
    const auto end = midi.cend();
    for (int offset = 0; offset < n;) {
        const int chunk = std::min(n - offset, cap);
        int ne = 0;
        while (it != end && (*it).samplePosition < offset + chunk) {
            const auto meta = *it;
            if (ne < kMaxBlockEvents &&
                toPlayEvent(meta.getMessage(), meta.samplePosition - offset, events_[static_cast<std::size_t>(ne)])) {
                ++ne;
            }
            ++it;
        }
        whistle_.setLog(blockLog_.data(), static_cast<int>(blockLog_.size()));
        whistle_.process(events_.data(), ne, chunk, settings, wl_.data(), wr_.data());
        for (int k = 0, nl = whistle_.logCount(); k < nl; ++k) { // syllable starts → offline log + editor
            const auto& entry = blockLog_[static_cast<std::size_t>(k)];
            if (extLog_ != nullptr && extLogCount_ < extLogCap_) {
                extLog_[extLogCount_++] = entry;
            }
            feed_.push(entry);
        }
        float* outL = buffer.getWritePointer(0) + offset;
        float* outR = nOut > 1 ? buffer.getWritePointer(1) + offset : nullptr;
        for (int i = 0; i < chunk; ++i) {
            float l = wl_[static_cast<std::size_t>(i)];
            float r = wr_[static_cast<std::size_t>(i)];
            if (lat > 0) {
                std::swap(l, delayL_[static_cast<std::size_t>(delayPos_)]);
                std::swap(r, delayR_[static_cast<std::size_t>(delayPos_)]);
                delayPos_ = delayPos_ + 1 == lat ? 0 : delayPos_ + 1;
            }
            const float g = juce::Decibels::decibelsToGain(whistleMasterSm_.next());
            if (outR != nullptr) {
                outL[i] = SafetyLimiter::processSample((add ? outL[i] : 0.0f) + l * g);
                outR[i] = SafetyLimiter::processSample((add ? outR[i] : 0.0f) + r * g);
            } else {
                outL[i] = SafetyLimiter::processSample((add ? outL[i] : 0.0f) + (l + r) * 0.70710678f * g);
            }
        }
        offset += chunk;
    }
    telem_singers_.store(whistle_.mode().activeSingers(), std::memory_order_relaxed);
    telem_voices_.store(whistle_.activeVoices(), std::memory_order_relaxed);
    feed_.publish(whistle_.samplesProcessed(), getSampleRate(), static_cast<float>(whistle_.modeAB().modWheel()),
                  whistle_.modeAB().sustainDown());
}

void SyrinxAudioProcessor::processAnatomical(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept {
    const int n = buffer.getNumSamples();
    const int nOut = buffer.getNumChannels();
    const auto t0 = std::chrono::steady_clock::now();
    readHostTempo();
    const int poly = polyParam_ != nullptr ? polyParam_->get() : 1;
    allocator_.setPolyphony(poly);
    allocator_.setSpread(raw(cuicui::ParamId::OutStereoSpread));

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
            if (performMode()) {
                std::memcpy(right + offset, right_.data(), static_cast<std::size_t>(chunk) * sizeof(float));
            } else {
                std::memcpy(right + offset, mono_.data(), static_cast<std::size_t>(chunk) * sizeof(float));
            }
        }
        offset += chunk;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    allocator_.notifyBlockTime(elapsed, static_cast<double>(n) / sr);
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
        bool noteOnEvent = false;
        bool noteOffEvent = false;
        while (it != midiEnd && (*it).samplePosition <= t0 + pos) {
            const auto msg = (*it).getMessage();
            const int nBefore = nHeld_;
            handleMidi(msg);
            if (msg.isNoteOn() && msg.getVelocity() > 0) {
                noteOnEvent = true;
            }
            if (nBefore > 0 && nHeld_ == 0 && !sustain_) {
                noteOffEvent = true;
            }
            if (!performMode() && nHeld_ > 0) {
                const float bendSt = 2.0f * bendNorm_;
                midiBeta_ = static_cast<float>(cuicui::clampParam(
                    cuicui::specOf(cuicui::ParamId::SrcLBeta),
                    static_cast<double>(crudeBetaFromMidi(static_cast<float>(held_[nHeld_ - 1].note), bendSt))));
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
        const int count = end - pos;
        const float gdb = raw(cuicui::ParamId::OutGainDb);
        if (performMode()) {
            cuicui::control::PerformInputs in;
            fillPerformInputs(in, noteOnEvent, noteOffEvent);
            const int poly = polyParam_ != nullptr ? polyParam_->get() : 1;
            allocator_.setPolyphony(poly);
            if (poly <= 1) {
                allocator_.setMono(lastNote_, lastVel_, in.gate, noteOnEvent, noteOffEvent, in.phraseSeed);
            }
            float* r = right_.empty() ? dest : right_.data();
            allocator_.process(dest + pos, r + pos, count, in, readSettings(), to);
        } else {
            applyPhysiologyNote(to);
            if (nHeld_ > 0) {
                midiBeta_ = to.src_L_beta;
            }
            voice_.setOutGainRamp(gdb, gdb);
            voice_.process(lastFrame_, to, dest + pos, count);
        }
        wasGated_ = nHeld_ > 0 || sustain_;

        masterSm_.setTarget(raw(cuicui::ParamId::OutMasterDb));
        for (int i = 0; i < count; ++i) {
            const float g = juce::Decibels::decibelsToGain(masterSm_.next());
            dest[pos + i] = SafetyLimiter::processSample(dest[pos + i] * g);
            if (performMode() && !right_.empty()) {
                right_[static_cast<std::size_t>(pos + i)] =
                    SafetyLimiter::processSample(right_[static_cast<std::size_t>(pos + i)] * g);
            }
        }

        lastFrame_ = to;
        pos = end;
        if (hostTempoValid_ && hostPlaying_ && hostBpm_ > 0.0) {
            const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
            hostPpq_ += (static_cast<double>(count) / sr) * hostBpm_ / 60.0;
        }
    }
}

void SyrinxAudioProcessor::publishTelemetry() noexcept {
    const auto t = performMode() ? allocator_.firstTelemetry() : voice_.telemetry();
    telem_f0_.store(t.f0_estimate_hz, std::memory_order_relaxed);
    telem_aL_.store(t.alpha_L, std::memory_order_relaxed);
    telem_aR_.store(t.alpha_R, std::memory_order_relaxed);
    telem_bL_.store(t.beta_L, std::memory_order_relaxed);
    telem_bR_.store(t.beta_R, std::memory_order_relaxed);
    telem_p_.store(t.pressure, std::memory_order_relaxed);
    telem_level_.store(t.level_db, std::memory_order_relaxed);
    telem_resets_.store(t.nonFiniteResets, std::memory_order_relaxed);
    const auto d = performMode() ? allocator_.firstDebug() : controller_.debug();
    telem_target_.store(static_cast<float>(d.f0Target), std::memory_order_relaxed);
    telem_side_.store(d.activeSide, std::memory_order_relaxed);
    telem_folded_.store(d.folded ? 1 : 0, std::memory_order_relaxed);
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
    s.target_hz = telem_target_.load(std::memory_order_relaxed);
    s.active_side = telem_side_.load(std::memory_order_relaxed);
    s.folded = telem_folded_.load(std::memory_order_relaxed) != 0;
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
    if (auto* pb = dynamic_cast<juce::AudioParameterBool*>(p)) {
        *pb = value >= 0.5f;
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
        presets_->writeState(*xml);
        xml->setAttribute("uiPage", uiPage.load());
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
    presets_->readState(*xml);
    uiPage.store(std::clamp(xml->getIntAttribute("uiPage", 0), 0, 1));
}

juce::AudioProcessorEditor* SyrinxAudioProcessor::createEditor() {
    return new SyrinxAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SyrinxAudioProcessor();
}
