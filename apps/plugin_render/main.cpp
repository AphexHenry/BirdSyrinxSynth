#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "cuicui/io/wav_writer.hpp"
#include "cuicui/params.hpp"
#include "cuicui/version.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MidiEvt {
    int sample = 0;
    juce::MidiMessage msg;
};

struct Lane {
    std::vector<std::pair<double, float>> pts;
    float valueAt(double t) const {
        if (pts.empty()) {
            return 0.0f;
        }
        if (t <= pts.front().first) {
            return pts.front().second;
        }
        if (t >= pts.back().first) {
            return pts.back().second;
        }
        for (std::size_t i = 1; i < pts.size(); ++i) {
            if (t <= pts[i].first) {
                const double t0 = pts[i - 1].first;
                const double t1 = pts[i].first;
                const float v0 = pts[i - 1].second;
                const float v1 = pts[i].second;
                const double u = (t1 > t0) ? (t - t0) / (t1 - t0) : 1.0;
                return static_cast<float>(v0 + (v1 - v0) * u);
            }
        }
        return pts.back().second;
    }
    double endTime() const { return pts.empty() ? 0.0 : pts.back().first; }
};

void usage(const char* argv0) {
    std::cerr << "plugin_render " << cuicui::kVersion << " (" << cuicui::kGitHash << ")\n"
              << "usage: " << argv0
              << " [--midi phrase.mid] [--notes \"60:0.0:1.0:100,...\"]\n"
              << "              [--params params.json] [--automation automation.json]\n"
              << "              [--sr 48000] [--block 256] [--tail 1.5] [--duration 0]\n"
              << "              [--seed 1] [--bpm 120] [--playing 0|1] [--preset \"Finch, natural\" | file.preset.json]\n"
              << "              [--screenshot editor.png] [--screenshot-page play|advanced] [--screenshot-scale 2]\n"
              << "              [--syllable-log starts.json] [--no-release-alarm] [--bank a.json[,b.json]] -o out.wav\n"
              << "  --preset applies a factory preset (by name) or a preset file before --seed and --params.\n"
              << "  --screenshot without -o draws the editor at rest; with -o, after the render (the display\n"
              << "  then shows the last seconds of the render).\n"
              << "  --no-release-alarm plays as R1 / the PROTO1 prototype did (D-030 off; reference tests).\n"
              << "  --bank sings these bank files (merged in order) instead of the factory birds (WP-R3).\n";
}

std::vector<MidiEvt> parseNotes(const std::string& spec, double sr) {
    std::vector<MidiEvt> out;
    std::stringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) {
            continue;
        }
        int note = 60, vel = 100;
        double start = 0.0, dur = 1.0;
        if (std::sscanf(tok.c_str(), "%d:%lf:%lf:%d", &note, &start, &dur, &vel) < 3) {
            throw std::runtime_error("bad --notes token (want note:start_s:dur_s:velocity): " + tok);
        }
        const int on = std::max(0, static_cast<int>(std::llround(start * sr)));
        const int off = std::max(on + 1, static_cast<int>(std::llround((start + dur) * sr)));
        out.push_back({on, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(std::clamp(vel, 1, 127)))});
        out.push_back({off, juce::MidiMessage::noteOff(1, note)});
    }
    return out;
}

std::vector<MidiEvt> loadMidiFile(const std::filesystem::path& path, double sr) {
    juce::File f(path.string());
    juce::FileInputStream in(f);
    if (in.failedToOpen()) {
        throw std::runtime_error("cannot open MIDI file: " + path.string());
    }
    juce::MidiFile mf;
    if (!mf.readFrom(in)) {
        throw std::runtime_error("cannot parse MIDI file: " + path.string());
    }
    mf.convertTimestampTicksToSeconds();
    std::vector<MidiEvt> out;
    for (int t = 0; t < mf.getNumTracks(); ++t) {
        const auto* seq = mf.getTrack(t);
        if (seq == nullptr) {
            continue;
        }
        for (int i = 0; i < seq->getNumEvents(); ++i) {
            const auto* h = seq->getEventPointer(i);
            const double sec = h->message.getTimeStamp();
            const int sample = std::max(0, static_cast<int>(std::llround(sec * sr)));
            out.push_back({sample, h->message});
        }
    }
    return out;
}

std::map<std::string, float> loadParams(const std::filesystem::path& path) {
    std::map<std::string, float> out;
    nlohmann::json j = nlohmann::json::parse(std::ifstream(path));
    for (auto it = j.begin(); it != j.end(); ++it) {
        out[it.key()] = it.value().get<float>();
    }
    return out;
}

std::map<std::string, Lane> loadAutomation(const std::filesystem::path& path) {
    std::map<std::string, Lane> out;
    nlohmann::json j = nlohmann::json::parse(std::ifstream(path));
    for (auto it = j.begin(); it != j.end(); ++it) {
        Lane lane;
        for (const auto& pt : it.value()) {
            if (!pt.is_array() || pt.size() < 2) {
                throw std::runtime_error("automation points must be [t, value]");
            }
            lane.pts.emplace_back(pt[0].get<double>(), pt[1].get<float>());
        }
        std::sort(lane.pts.begin(), lane.pts.end());
        out[it.key()] = std::move(lane);
    }
    return out;
}

class RenderPlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setBpm(bpm);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        p.setTimeInSamples(samples);
        if (sr > 0.0) {
            p.setTimeInSeconds(static_cast<double>(samples) / sr);
        }
        return p;
    }
    double bpm = 120.0;
    double ppq = 0.0;
    double sr = 48000.0;
    bool playing = false;
    juce::int64 samples = 0;
};

/// WP-R4: a factory preset by name, or a preset file.
void applyPreset(SyrinxAudioProcessor& proc, const std::string& arg) {
    auto& pm = proc.presets();
    if (const auto* p = pm.findFactory(juce::String(arg))) {
        pm.load(*p);
        return;
    }
    std::ifstream in(arg);
    if (!in) {
        throw std::runtime_error("no factory preset or file called: " + arg);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Preset p;
    juce::String error;
    if (!pm.fromJson(ss.str(), p, error)) {
        throw std::runtime_error("bad preset " + arg + ": " + error.toStdString());
    }
    pm.load(p);
}

void writeScreenshot(juce::AudioProcessorEditor& ed, const std::filesystem::path& path, float scale) {
    const int w = std::max(1, ed.getWidth());
    const int h = std::max(1, ed.getHeight());
    juce::Image img(juce::Image::RGB, std::max(1, juce::roundToInt(static_cast<float>(w) * scale)),
                    std::max(1, juce::roundToInt(static_cast<float>(h) * scale)), true);
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale(scale));
    ed.paintEntireComponent(g, true);
    juce::File f(path.string());
    f.getParentDirectory().createDirectory();
    if (f.existsAsFile()) {
        f.deleteFile();
    }
    juce::FileOutputStream os(f);
    if (os.failedToOpen()) {
        throw std::runtime_error("cannot write screenshot: " + path.string());
    }
    juce::PNGImageFormat fmt;
    if (!fmt.writeImageToStream(img, os)) {
        throw std::runtime_error("PNG encode failed");
    }
}

std::unique_ptr<SyrinxAudioProcessorEditor> makeEditor(SyrinxAudioProcessor& proc, int page) {
    std::unique_ptr<SyrinxAudioProcessorEditor> ed(dynamic_cast<SyrinxAudioProcessorEditor*>(proc.createEditor()));
    if (ed == nullptr) {
        throw std::runtime_error("no editor");
    }
    ed->showPage(page);
    return ed;
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    try {
        std::filesystem::path midiPath, paramsPath, autoPath, outPath, screenshotPath, sylLogPath;
        std::string notes, presetArg, bankArg;
        bool releaseAlarm = true;
        int screenshotPage = 0;
        float screenshotScale = 1.0f;
        double sr = 48000.0;
        int block = 256;
        double tail = 1.5;
        double duration = 0.0;
        std::uint64_t seed = 1;
        bool seedSet = false;
        double bpm = 120.0;
        bool playing = false;
        bool bpmSet = false;

        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto need = [&](const char* flag) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error(std::string("missing value for ") + flag);
                }
                return argv[++i];
            };
            if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            }
            if (a == "-o") {
                outPath = need("-o");
            } else if (a == "--midi") {
                midiPath = need("--midi");
            } else if (a == "--notes") {
                notes = need("--notes");
            } else if (a == "--params") {
                paramsPath = need("--params");
            } else if (a == "--automation") {
                autoPath = need("--automation");
            } else if (a == "--sr") {
                sr = std::stod(need("--sr"));
            } else if (a == "--block") {
                block = std::stoi(need("--block"));
            } else if (a == "--tail") {
                tail = std::stod(need("--tail"));
            } else if (a == "--duration") {
                duration = std::stod(need("--duration"));
            } else if (a == "--seed") {
                seed = static_cast<std::uint64_t>(std::stoll(need("--seed")));
                seedSet = true;
            } else if (a == "--syllable-log") {
                sylLogPath = need("--syllable-log");
            } else if (a == "--screenshot") {
                screenshotPath = need("--screenshot");
            } else if (a == "--screenshot-page") {
                screenshotPage = need("--screenshot-page") == "advanced" ? 1 : 0;
            } else if (a == "--screenshot-scale") {
                screenshotScale = std::clamp(std::stof(need("--screenshot-scale")), 0.25f, 4.0f);
            } else if (a == "--preset") {
                presetArg = need("--preset");
            } else if (a == "--no-release-alarm") {
                releaseAlarm = false;
            } else if (a == "--bank") {
                bankArg = need("--bank");
            } else if (a == "--bpm") {
                bpm = std::stod(need("--bpm"));
                bpmSet = true;
            } else if (a == "--playing") {
                playing = std::stoi(need("--playing")) != 0;
                bpmSet = true;
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        if (!screenshotPath.empty() && outPath.empty()) { // the editor at rest
            SyrinxAudioProcessor proc;
            if (!presetArg.empty()) {
                applyPreset(proc, presetArg);
            }
            if (!paramsPath.empty()) {
                for (const auto& [id, v] : loadParams(paramsPath)) {
                    proc.setParamPlain(id, v);
                }
            }
            auto ed = makeEditor(proc, screenshotPage);
            ed->prepareSnapshot();
            writeScreenshot(*ed, screenshotPath, screenshotScale);
            return 0;
        }
        if (outPath.empty() || sr <= 0.0 || block < 1) {
            usage(argv[0]);
            return 1;
        }

        std::vector<MidiEvt> events;
        if (!midiPath.empty()) {
            auto m = loadMidiFile(midiPath, sr);
            events.insert(events.end(), m.begin(), m.end());
        }
        if (!notes.empty()) {
            auto m = parseNotes(notes, sr);
            events.insert(events.end(), m.begin(), m.end());
        }
        std::sort(events.begin(), events.end(), [](const MidiEvt& a, const MidiEvt& b) { return a.sample < b.sample; });

        auto params = paramsPath.empty() ? std::map<std::string, float>{} : loadParams(paramsPath);
        auto automation = autoPath.empty() ? std::map<std::string, Lane>{} : loadAutomation(autoPath);

        int lastEvt = 0;
        for (const auto& e : events) {
            lastEvt = std::max(lastEvt, e.sample);
        }
        double autoEnd = 0.0;
        for (const auto& [_, lane] : automation) {
            autoEnd = std::max(autoEnd, lane.endTime());
        }
        double totalS = duration;
        if (totalS <= 0.0) {
            totalS = std::max(static_cast<double>(lastEvt) / sr, autoEnd) + tail;
        }
        if (totalS <= 0.0) {
            totalS = 1.0;
        }
        const int n = static_cast<int>(std::llround(totalS * sr));

        SyrinxAudioProcessor proc;
        proc.setNonRealtime(true);
        proc.setReleaseAlarm(releaseAlarm);
        if (!bankArg.empty()) { // WP-R3: one bird alone, or the R1 / R5 bank
            std::vector<std::string> files;
            std::stringstream ss(bankArg);
            for (std::string f; std::getline(ss, f, ',');) {
                files.push_back(f);
            }
            proc.useBankFiles(files);
        }
        if (!presetArg.empty()) {
            applyPreset(proc, presetArg);
        }
        if (seedSet) {
            proc.setParamPlain("engine.seed", static_cast<float>(seed));
        }
        for (const auto& [id, v] : params) {
            if (!proc.setParamPlain(id, v)) {
                throw std::runtime_error("unknown parameter: " + id);
            }
        }
        std::vector<cuicui::control::SyllableLogEntry> sylLog;
        if (!sylLogPath.empty()) {
            sylLog.resize(1u << 18);
            proc.setSyllableLog(sylLog.data(), static_cast<int>(sylLog.size()));
        }
        proc.prepareToPlay(sr, block);
        RenderPlayHead playHead;
        playHead.sr = sr;
        playHead.bpm = bpm;
        playHead.playing = playing;
        playHead.ppq = 0.0;
        playHead.samples = 0;
        if (bpmSet) {
            proc.setPlayHead(&playHead);
        } else {
            proc.setPlayHead(nullptr);
        }

        // With --screenshot, the editor reads the syllable feed during the render, as it would live.
        std::unique_ptr<SyrinxAudioProcessorEditor> shotEditor;
        if (!screenshotPath.empty()) {
            shotEditor = makeEditor(proc, screenshotPage);
        }

        std::vector<float> stereo(static_cast<std::size_t>(n) * 2u, 0.0f);
        juce::AudioBuffer<float> buf(2, block);
        std::size_t evtI = 0;
        int done = 0;
        while (done < n) {
            const int nThis = std::min(block, n - done);
            buf.setSize(2, nThis, false, false, true);
            buf.clear();
            const double t = static_cast<double>(done) / sr;
            for (const auto& [id, lane] : automation) {
                proc.setParamPlain(id, lane.valueAt(t));
            }
            juce::MidiBuffer midi;
            while (evtI < events.size() && events[evtI].sample < done + nThis) {
                midi.addEvent(events[evtI].msg, events[evtI].sample - done);
                ++evtI;
            }
            proc.processBlock(buf, midi);
            if (bpmSet && playHead.bpm > 0.0) {
                playHead.samples += nThis;
                playHead.ppq += (static_cast<double>(nThis) / sr) * playHead.bpm / 60.0;
            }
            for (int i = 0; i < nThis; ++i) {
                stereo[static_cast<std::size_t>((done + i) * 2)] = buf.getSample(0, i);
                stereo[static_cast<std::size_t>((done + i) * 2 + 1)] = buf.getSample(1, i);
            }
            done += nThis;
            if (shotEditor != nullptr) {
                shotEditor->prepareSnapshot();
            }
        }
        if (shotEditor != nullptr) {
            shotEditor->prepareSnapshot();
            writeScreenshot(*shotEditor, screenshotPath, screenshotScale);
            shotEditor.reset();
        }
        const int nLogged = proc.syllableLogCount();
        const int latency = proc.getLatencySamples();
        proc.setSyllableLog(nullptr, 0);
        proc.releaseResources();

        cuicui::io::write_wav(outPath, stereo, 2, static_cast<int>(std::lround(sr)));
        if (!sylLogPath.empty()) { // WP-R1: every syllable the whistle path started
            const auto& bank = proc.syllableBank();
            nlohmann::json j;
            j["format"] = "cuicui.syllable_log";
            j["sample_rate"] = sr;
            j["latency_samples"] = latency;
            j["bank"] = bank.name();
            j["bank_hash"] = bank.hash();
            j["note"] = "sample = start before the plugin latency; audio onset = sample + latency_samples";
            auto& arr = j["starts"] = nlohmann::json::array();
            for (int i = 0; i < nLogged; ++i) {
                const auto& e = sylLog[static_cast<std::size_t>(i)];
                const auto& syl = bank.syllable(e.request.syllable);
                arr.push_back({{"sample", e.sample},
                               {"time_s", static_cast<double>(e.sample) / sr},
                               {"syllable", e.request.syllable},
                               {"phrase", syl.phrase},
                               {"index", syl.index},
                               {"label", bank.label(e.request.syllable)},
                               {"length_ms", syl.lengthMs},
                               {"shift_cents", e.request.shiftCents},
                               {"gain", e.request.gain},
                               {"pan", e.request.pan},
                               {"singer", e.request.singer},
                               {"first", e.request.first},
                               {"key", e.request.note},
                               {"target_cents", e.request.targetCents},
                               {"release_alarm", e.request.releaseAlarm},
                               {"variation", e.request.variation},
                               {"length_scale", e.request.lengthScale},
                               {"range_scale", e.request.rangeScale},
                               {"variant_seed", e.request.variantSeed},
                               {"variant_note_cents", e.request.variantNoteCents},
                               {"bird", syl.bird}});
            }
            std::ofstream(sylLogPath) << j.dump(1) << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "plugin_render: " << e.what() << '\n';
        return 1;
    }
}
