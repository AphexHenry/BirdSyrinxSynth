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
              << "              [--seed 1] [--bpm 120] [--playing 0|1] [--screenshot editor.png] -o out.wav\n";
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

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    try {
        std::filesystem::path midiPath, paramsPath, autoPath, outPath, screenshotPath;
        std::string notes;
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
            } else if (a == "--screenshot") {
                screenshotPath = need("--screenshot");
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
        if (!screenshotPath.empty()) {
            SyrinxAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            const int w = std::max(1, ed->getWidth());
            const int h = std::max(1, ed->getHeight());
            ed->resized();
            juce::Image img(juce::Image::RGB, w, h, true);
            juce::Graphics g(img);
            ed->paintEntireComponent(g, true);
            juce::File f(screenshotPath.string());
            f.getParentDirectory().createDirectory();
            if (f.existsAsFile()) {
                f.deleteFile();
            }
            juce::FileOutputStream os(f);
            if (os.failedToOpen()) {
                throw std::runtime_error("cannot write screenshot: " + screenshotPath.string());
            }
            juce::PNGImageFormat fmt;
            if (!fmt.writeImageToStream(img, os)) {
                throw std::runtime_error("PNG encode failed");
            }
            if (outPath.empty()) {
                return 0;
            }
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
        if (seedSet) {
            proc.setParamPlain("engine.seed", static_cast<float>(seed));
        }
        for (const auto& [id, v] : params) {
            if (!proc.setParamPlain(id, v)) {
                throw std::runtime_error("unknown parameter: " + id);
            }
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
        }
        proc.releaseResources();

        cuicui::io::write_wav(outPath, stereo, 2, static_cast<int>(std::lround(sr)));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "plugin_render: " << e.what() << '\n';
        return 1;
    }
}
