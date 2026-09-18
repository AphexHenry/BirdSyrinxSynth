#pragma once

#include "cuicui/control/archetypes.hpp"
#include "cuicui/control/gesture.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/params.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace cuicui::io {

struct GestureScoreError : std::runtime_error {
    int exit_code;
    explicit GestureScoreError(const std::string& message, int code = 1)
        : std::runtime_error(message), exit_code(code) {}
};

struct ScoreSegment {
    double t0_s = 0.0;
    double t1_s = 0.0;
    std::string label;
};

struct ScoreGestureEvent {
    double t_s = 0.0;
    double duration_ms = 100.0;
    std::string type;
    double anchor_hz = 2093.0; // C7
    std::unordered_map<std::string, double> params;
    std::unordered_map<std::string, std::string> param_strings;
    control::GestureHints hints;
    control::GestureCurves curves;
    bool has_curves = false;
};

struct GestureScore {
    int version = 1;
    std::string name;
    std::string created_by;
    std::string created_at;
    std::unordered_map<std::string, double> settings;
    double control_rate_hz = 1000.0;
    double t0_s = 0.0;
    std::unordered_map<std::string, std::vector<float>> tracks;
    std::vector<ScoreGestureEvent> gestures;
    std::vector<ScoreSegment> segments;
    bool has_physio = false;
    bool ignore_gestures = false; // both sections present
};

inline void apply_setting_value(VoiceSettings& st, const std::string& id, double v) {
    const auto* p = findParam(id);
    if (p == nullptr) {
        return;
    }
    const double c = clampParam(*p, v);
    switch (p->pid) {
    case ParamId::BodySize:
        st.body_size = c;
        break;
    case ParamId::SrcGamma:
        st.src_gamma = c;
        break;
    case ParamId::TracheaLengthMm:
        st.trachea_length_mm = c;
        break;
    case ParamId::TracheaReflection:
        st.trachea_reflection = c;
        break;
    case ParamId::TracheaLoss:
        st.trachea_loss = c;
        break;
    case ParamId::TracheaInputGain:
        st.trachea_input_gain = c;
        break;
    case ParamId::OecDamping:
        st.oec_damping = c;
        break;
    case ParamId::BeakDamping:
        st.beak_damping = c;
        break;
    case ParamId::OutRadiation:
        st.out_radiation = c;
        break;
    case ParamId::OutGainDb:
        st.out_gain_db = c;
        break;
    case ParamId::RespAttackMs:
        st.resp_attack_ms = c;
        break;
    case ParamId::RespReleaseMs:
        st.resp_release_ms = c;
        break;
    case ParamId::NoiseBetaBandwidthHz:
        st.noise_beta_bandwidth_hz = c;
        break;
    case ParamId::NoiseAspiration:
        st.noise_aspiration = c;
        break;
    case ParamId::SrcCoupling:
        st.src_coupling = c;
        break;
    case ParamId::EngineSeed:
        st.seed = static_cast<std::uint64_t>(std::max(0.0, c));
        break;
    case ParamId::EngineOversampling:
        st.oversampling = static_cast<int>(std::lround(c));
        break;
    default:
        break;
    }
}

inline VoiceSettings settings_from_score(const GestureScore& score) {
    VoiceSettings st;
    for (const auto& kv : score.settings) {
        apply_setting_value(st, kv.first, kv.second);
    }
    return st;
}

inline double score_setting(const GestureScore& score, const std::string& id, double def) {
    if (const auto it = score.settings.find(id); it != score.settings.end()) {
        return it->second;
    }
    return def;
}

inline nlohmann::json curve_array(const std::array<double, control::GestureCurves::kN>& a) {
    nlohmann::json j = nlohmann::json::array();
    for (double v : a) {
        j.push_back(v);
    }
    return j;
}

inline void read_curve_array(const nlohmann::json& j,
                             std::array<double, control::GestureCurves::kN>& dst,
                             const char* name) {
    if (!j.is_array()) {
        throw GestureScoreError(std::string("curves.") + name + " must be an array");
    }
    if (j.size() != static_cast<std::size_t>(control::GestureCurves::kN)) {
        throw GestureScoreError(std::string("curves.") + name + " must contain exactly 64 samples");
    }
    for (int i = 0; i < control::GestureCurves::kN; ++i) {
        if (!j[static_cast<std::size_t>(i)].is_number()) {
            throw GestureScoreError(std::string("curves.") + name + " entries must be numbers");
        }
        dst[static_cast<std::size_t>(i)] = j[static_cast<std::size_t>(i)].get<double>();
    }
}

inline double json_hint(const nlohmann::json& h, const char* key) {
    if (!h.contains(key) || h.at(key).is_null()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (!h.at(key).is_number()) {
        throw GestureScoreError(std::string("hints.") + key + " must be a number or null");
    }
    return h.at(key).get<double>();
}

inline nlohmann::json hint_to_json(double v) {
    if (!std::isfinite(v)) {
        return nullptr;
    }
    return v;
}

inline ScoreGestureEvent parse_gesture_event(const nlohmann::json& j) {
    ScoreGestureEvent e;
    if (j.contains("t_s")) {
        e.t_s = j.at("t_s").get<double>();
    }
    if (j.contains("duration_ms")) {
        e.duration_ms = j.at("duration_ms").get<double>();
    }
    if (j.contains("type") && j.at("type").is_string()) {
        e.type = j.at("type").get<std::string>();
    }
    if (j.contains("anchor_hz")) {
        e.anchor_hz = j.at("anchor_hz").get<double>();
    }
    if (j.contains("params") && j.at("params").is_object()) {
        for (auto it = j.at("params").begin(); it != j.at("params").end(); ++it) {
            if (it.value().is_number()) {
                e.params[it.key()] = it.value().get<double>();
            } else if (it.value().is_string()) {
                e.param_strings[it.key()] = it.value().get<std::string>();
            }
        }
    }
    if (j.contains("hints") && j.at("hints").is_object()) {
        const auto& h = j.at("hints");
        e.hints.oecRatio = json_hint(h, "oec_ratio");
        e.hints.gape = json_hint(h, "gape");
        e.hints.secondVoiceCents = json_hint(h, "second_voice_cents");
    }
    if (j.contains("curves") && j.at("curves").is_object()) {
        const auto& c = j.at("curves");
        int samples = control::GestureCurves::kN;
        if (c.contains("samples")) {
            samples = c.at("samples").get<int>();
        }
        if (samples != control::GestureCurves::kN) {
            throw GestureScoreError("curves.samples must be 64");
        }
        if (!c.contains("pitch_cents") || !c.contains("amp_db") || !c.contains("tone") || !c.contains("side")) {
            throw GestureScoreError("curves must include pitch_cents, amp_db, tone, side");
        }
        read_curve_array(c.at("pitch_cents"), e.curves.pitchCents, "pitch_cents");
        read_curve_array(c.at("amp_db"), e.curves.ampDb, "amp_db");
        read_curve_array(c.at("tone"), e.curves.tone, "tone");
        read_curve_array(c.at("side"), e.curves.side, "side");
        e.has_curves = true;
    }
    if (!e.has_curves) {
        if (e.type.rfind("archetype:", 0) == 0) {
            const std::string name = e.type.substr(10);
            if (control::findArchetype(name) == nullptr) {
                throw GestureScoreError("unknown archetype in type: " + e.type);
            }
            control::ArchetypeParamBag bag;
            bag.num = e.params;
            bag.str = e.param_strings;
            auto inst = control::generateArchetype(name, bag, 1);
            e.curves = inst.curves;
            if (!control::hintSet(e.hints.oecRatio)) {
                e.hints.oecRatio = inst.hints.oecRatio;
            }
            if (!control::hintSet(e.hints.gape)) {
                e.hints.gape = inst.hints.gape;
            }
            if (!control::hintSet(e.hints.secondVoiceCents)) {
                e.hints.secondVoiceCents = inst.hints.secondVoiceCents;
            }
            e.duration_ms = inst.durationMs;
            e.has_curves = true;
        } else {
            throw GestureScoreError("gesture is missing curves (and is not archetype:<name>)");
        }
    }
    return e;
}

inline GestureScore parse_gesture_score_json(const nlohmann::json& j) {
    if (!j.is_object()) {
        throw GestureScoreError("gesture score must be a JSON object");
    }
    if (!j.contains("format") || j.at("format") != "cuicui.gesture_score") {
        throw GestureScoreError("invalid format: expected \"cuicui.gesture_score\"");
    }
    if (!j.contains("version") || j.at("version") != 1) {
        throw GestureScoreError("unsupported gesture score version (expected 1)");
    }
    GestureScore s;
    if (j.contains("name") && j.at("name").is_string()) {
        s.name = j.at("name").get<std::string>();
    }
    if (j.contains("created_by") && j.at("created_by").is_string()) {
        s.created_by = j.at("created_by").get<std::string>();
    }
    if (j.contains("created_at") && j.at("created_at").is_string()) {
        s.created_at = j.at("created_at").get<std::string>();
    }
    if (j.contains("settings")) {
        if (!j.at("settings").is_object()) {
            throw GestureScoreError("settings must be an object");
        }
        for (auto it = j.at("settings").begin(); it != j.at("settings").end(); ++it) {
            if (!it.value().is_number()) {
                throw GestureScoreError("settings values must be numbers (key: " + it.key() + ")");
            }
            if (it.key().rfind("perf.", 0) != 0 && it.key().rfind("body.", 0) != 0 &&
                it.key().rfind("engine.", 0) != 0 && findParam(it.key()) == nullptr) {
                throw GestureScoreError("unknown settings id: " + it.key());
            }
            s.settings[it.key()] = it.value().get<double>();
        }
    }
    if (j.contains("physio") && !j.at("physio").is_null()) {
        const auto& p = j.at("physio");
        if (!p.is_object()) {
            throw GestureScoreError("physio must be an object");
        }
        if (p.contains("control_rate_hz")) {
            s.control_rate_hz = p.at("control_rate_hz").get<double>();
            if (!(s.control_rate_hz > 0.0)) {
                throw GestureScoreError("physio.control_rate_hz must be > 0");
            }
        }
        if (p.contains("t0_s")) {
            s.t0_s = p.at("t0_s").get<double>();
        }
        if (p.contains("tracks")) {
            if (!p.at("tracks").is_object()) {
                throw GestureScoreError("physio.tracks must be an object");
            }
            std::size_t len = 0;
            bool first = true;
            for (auto it = p.at("tracks").begin(); it != p.at("tracks").end(); ++it) {
                if (findParam(it.key()) == nullptr) {
                    throw GestureScoreError("unknown physio track: " + it.key());
                }
                if (!it.value().is_array()) {
                    throw GestureScoreError("track " + it.key() + " must be an array");
                }
                if (first) {
                    len = it.value().size();
                    first = false;
                } else if (it.value().size() != len) {
                    throw GestureScoreError("all physio tracks must have equal length");
                }
                std::vector<float> tr;
                tr.reserve(it.value().size());
                for (const auto& v : it.value()) {
                    if (!v.is_number()) {
                        throw GestureScoreError("track " + it.key() + " entries must be numbers");
                    }
                    tr.push_back(v.get<float>());
                }
                s.tracks[it.key()] = std::move(tr);
            }
            s.has_physio = !s.tracks.empty();
        }
    }
    if (j.contains("gestures") && j.at("gestures").is_array()) {
        for (const auto& g : j.at("gestures")) {
            s.gestures.push_back(parse_gesture_event(g));
        }
    }
    if (j.contains("segments") && j.at("segments").is_array()) {
        for (const auto& seg : j.at("segments")) {
            ScoreSegment sm;
            if (seg.contains("t0_s")) {
                sm.t0_s = seg.at("t0_s").get<double>();
            }
            if (seg.contains("t1_s")) {
                sm.t1_s = seg.at("t1_s").get<double>();
            }
            if (seg.contains("label") && seg.at("label").is_string()) {
                sm.label = seg.at("label").get<std::string>();
            }
            s.segments.push_back(std::move(sm));
        }
    }
    if (s.has_physio && !s.gestures.empty()) {
        s.ignore_gestures = true;
    }
    return s;
}

inline GestureScore parse_gesture_score_string(const std::string& text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        throw GestureScoreError(std::string("invalid JSON: ") + e.what());
    }
    return parse_gesture_score_json(j);
}

inline GestureScore parse_gesture_score_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw GestureScoreError("cannot open gesture score: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_gesture_score_string(ss.str());
}

inline nlohmann::json gesture_event_to_json(const ScoreGestureEvent& e) {
    nlohmann::json j;
    j["t_s"] = e.t_s;
    j["duration_ms"] = e.duration_ms;
    j["type"] = e.type;
    j["anchor_hz"] = e.anchor_hz;
    nlohmann::json params = nlohmann::json::object();
    std::vector<std::string> keys;
    for (const auto& kv : e.params) {
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        params[k] = e.params.at(k);
    }
    for (const auto& kv : e.param_strings) {
        params[kv.first] = kv.second;
    }
    j["params"] = params;
    nlohmann::json hints;
    hints["oec_ratio"] = hint_to_json(e.hints.oecRatio);
    hints["gape"] = hint_to_json(e.hints.gape);
    hints["second_voice_cents"] = hint_to_json(e.hints.secondVoiceCents);
    j["hints"] = hints;
    nlohmann::json curves;
    curves["samples"] = control::GestureCurves::kN;
    curves["pitch_cents"] = curve_array(e.curves.pitchCents);
    curves["amp_db"] = curve_array(e.curves.ampDb);
    curves["tone"] = curve_array(e.curves.tone);
    curves["side"] = curve_array(e.curves.side);
    j["curves"] = curves;
    return j;
}

inline nlohmann::json gesture_score_to_json(const GestureScore& s) {
    nlohmann::json j;
    j["format"] = "cuicui.gesture_score";
    j["version"] = 1;
    j["name"] = s.name;
    j["created_by"] = s.created_by;
    j["created_at"] = s.created_at;
    nlohmann::json settings = nlohmann::json::object();
    std::vector<std::string> skeys;
    for (const auto& kv : s.settings) {
        skeys.push_back(kv.first);
    }
    std::sort(skeys.begin(), skeys.end());
    for (const auto& k : skeys) {
        settings[k] = s.settings.at(k);
    }
    j["settings"] = settings;
    if (s.has_physio) {
        nlohmann::json physio;
        physio["control_rate_hz"] = s.control_rate_hz;
        physio["t0_s"] = s.t0_s;
        nlohmann::json tracks = nlohmann::json::object();
        std::vector<std::string> tkeys;
        for (const auto& kv : s.tracks) {
            tkeys.push_back(kv.first);
        }
        std::sort(tkeys.begin(), tkeys.end());
        for (const auto& k : tkeys) {
            nlohmann::json arr = nlohmann::json::array();
            for (float v : s.tracks.at(k)) {
                arr.push_back(static_cast<double>(v));
            }
            tracks[k] = arr;
        }
        physio["tracks"] = tracks;
        j["physio"] = physio;
    }
    nlohmann::json gestures = nlohmann::json::array();
    for (const auto& g : s.gestures) {
        gestures.push_back(gesture_event_to_json(g));
    }
    j["gestures"] = gestures;
    nlohmann::json segs = nlohmann::json::array();
    for (const auto& seg : s.segments) {
        nlohmann::json sj;
        sj["t0_s"] = seg.t0_s;
        sj["t1_s"] = seg.t1_s;
        sj["label"] = seg.label;
        segs.push_back(sj);
    }
    j["segments"] = segs;
    return j;
}

inline std::string dump_gesture_score(const GestureScore& s) {
    return gesture_score_to_json(s).dump(2);
}

inline void write_gesture_score_file(const std::filesystem::path& path, const GestureScore& s) {
    std::ofstream out(path);
    if (!out) {
        throw GestureScoreError("cannot write gesture score: " + path.string());
    }
    out << dump_gesture_score(s);
    if (!out) {
        throw GestureScoreError("failed writing gesture score: " + path.string());
    }
}

inline control::GestureInstance instance_from_event(const ScoreGestureEvent& e) {
    control::GestureInstance g;
    g.tStart = e.t_s;
    g.durationMs = e.duration_ms;
    g.anchorCents = control::centsFromHz(e.anchor_hz);
    g.curves = e.curves;
    g.hints = e.hints;
    return g;
}

} // namespace cuicui::io
