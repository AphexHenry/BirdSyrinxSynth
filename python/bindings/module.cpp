#include "cuicui/control/archetypes.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/score_player.hpp"
#include "cuicui/control/side_switcher.hpp"
#include "cuicui/dsp/oversampling.hpp"
#include "cuicui/io/gesture_score.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/model/bilateral_source.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/trachea.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"
#include "cuicui/params.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nb = nanobind;

namespace {

std::vector<double> copy_f64_1d(const nb::ndarray<nb::numpy, const double, nb::ndim<1>, nb::c_contig>& a) {
    const std::size_t n = a.shape(0);
    std::vector<double> out(n);
    if (n != 0) {
        std::memcpy(out.data(), a.data(), n * sizeof(double));
    }
    return out;
}

std::vector<float> copy_f32_1d(nb::handle obj) {
    auto a = nb::cast<nb::ndarray<nb::numpy, const float, nb::ndim<1>, nb::c_contig>>(obj);
    const std::size_t n = a.shape(0);
    std::vector<float> out(n);
    if (n != 0) {
        std::memcpy(out.data(), a.data(), n * sizeof(float));
    }
    return out;
}

double dict_d(const nb::dict& d, const char* key, double def) {
    if (!d.contains(key)) {
        return def;
    }
    return nb::cast<double>(d[key]);
}

cuicui::VoiceSettings settings_from_dict(const nb::dict& d) {
    cuicui::VoiceSettings s;
    s.body_size = dict_d(d, "body.size", s.body_size);
    s.src_gamma = dict_d(d, "src.gamma", s.src_gamma);
    s.trachea_length_mm = dict_d(d, "trachea.length_mm", s.trachea_length_mm);
    s.trachea_reflection = dict_d(d, "trachea.reflection", s.trachea_reflection);
    s.trachea_loss = dict_d(d, "trachea.loss", s.trachea_loss);
    s.trachea_input_gain = dict_d(d, "trachea.input_gain", s.trachea_input_gain);
    s.oec_damping = dict_d(d, "oec.damping", s.oec_damping);
    s.beak_damping = dict_d(d, "beak.damping", s.beak_damping);
    s.out_radiation = dict_d(d, "out.radiation", s.out_radiation);
    s.out_gain_db = dict_d(d, "out.gain_db", s.out_gain_db);
    s.resp_attack_ms = dict_d(d, "resp.attack_ms", s.resp_attack_ms);
    s.resp_release_ms = dict_d(d, "resp.release_ms", s.resp_release_ms);
    s.noise_beta_bandwidth_hz = dict_d(d, "noise.beta_bandwidth_hz", s.noise_beta_bandwidth_hz);
    s.noise_aspiration = dict_d(d, "noise.aspiration", s.noise_aspiration);
    s.src_coupling = dict_d(d, "src.coupling", s.src_coupling);
    if (d.contains("engine.seed")) {
        s.seed = static_cast<std::uint64_t>(nb::cast<double>(d["engine.seed"]));
    }
    if (d.contains("engine.oversampling")) {
        s.oversampling = static_cast<int>(nb::cast<double>(d["engine.oversampling"]));
    }
    return s;
}

cuicui::ControlFrame frame_from_dict(const nb::dict& d) {
    cuicui::ControlFrame f;
    auto setf = [&](const char* key, float& dst) {
        if (d.contains(key)) {
            dst = static_cast<float>(nb::cast<double>(d[key]));
        }
    };
    setf("resp.pressure", f.resp_pressure);
    setf("src.L.beta", f.src_L_beta);
    setf("src.R.beta", f.src_R_beta);
    setf("src.L.gate", f.src_L_gate);
    setf("src.R.gate", f.src_R_gate);
    setf("src.L.alpha_offset", f.src_L_alpha_offset);
    setf("src.R.alpha_offset", f.src_R_alpha_offset);
    setf("oec.freq_hz", f.oec_freq_hz);
    setf("beak.gape", f.beak_gape);
    setf("noise.beta_sigma", f.noise_beta_sigma);
    setf("test.freq_hz", f.test_freq_hz);
    setf("test.gain_db", f.test_gain_db);
    return f;
}

nb::dict telemetry_to_dict(const cuicui::VoiceTelemetry& t) {
    nb::dict d;
    d["alpha_L"] = t.alpha_L;
    d["alpha_R"] = t.alpha_R;
    d["beta_L"] = t.beta_L;
    d["beta_R"] = t.beta_R;
    d["pressure"] = t.pressure;
    d["f0_estimate_hz"] = t.f0_estimate_hz;
    d["level_db"] = t.level_db;
    d["non_finite_resets"] = t.nonFiniteResets;
    return d;
}

} // namespace

NB_MODULE(cuicui_core, m) {
    m.doc() = "Syrinx C++ engine (parameter registry, offline render, oversampling, labial source)";

    m.def(
        "param_specs",
        []() {
            nb::list out;
            for (const auto& p : cuicui::kParamTable) {
                nb::dict d;
                d["id"] = p.id;
                d["name"] = p.name;
                d["unit"] = std::string(p.unit);
                d["min"] = p.min;
                d["max"] = p.max;
                d["default"] = p.def;
                d["skew"] = p.skew;
                d["smoothing_ms"] = p.smoothingMs;
                d["layer"] = cuicui::layerName(p.layer);
                d["fast"] = p.fast;
                d["since"] = p.sinceWP;
                out.append(d);
            }
            return out;
        },
        "Parameter registry as a list of dicts (same keys as bird_render --dump-params).");

    m.def(
        "resolve_oversampling",
        &cuicui::dsp::resolveOversampling,
        nb::arg("rate"),
        nb::arg("requested") = 0,
        "Resolve oversampling factor (0 = auto).");

    m.def(
        "render_spec",
        [](const nb::dict& spec) {
            nb::object dumps = nb::module_::import_("json").attr("dumps");
            const std::string text = nb::cast<std::string>(dumps(spec));
            const auto parsed = cuicui::io::parse_render_spec_string(text);
            std::vector<float> buf;
            {
                nb::gil_scoped_release release;
                buf = cuicui::io::render_offline(parsed);
            }
            auto* heap = new std::vector<float>(std::move(buf));
            nb::capsule owner(heap, [](void* p) noexcept { delete static_cast<std::vector<float>*>(p); });
            return nb::ndarray<nb::numpy, float, nb::ndim<1>>(heap->data(), {heap->size()}, owner);
        },
        nb::arg("spec"),
        "Render a render-spec dict to a float32 mono array. Releases the GIL.");

    m.def(
        "rest_state",
        [](double alpha, double beta) {
            const double x = cuicui::model::restDisplacement(alpha, beta);
            return nb::make_tuple(x, 0.0);
        },
        nb::arg("alpha"),
        nb::arg("beta"),
        "Stable rest (x, y=0) of the labial oscillator.");

    m.def(
        "source_only",
        [](nb::ndarray<nb::numpy, const double, nb::ndim<1>, nb::c_contig> alpha,
           nb::ndarray<nb::numpy, const double, nb::ndim<1>, nb::c_contig> beta, double gamma,
           double internal_rate, double x0, double y0) {
            if (alpha.shape(0) != beta.shape(0)) {
                throw std::invalid_argument("source_only: alpha and beta must have the same length");
            }
            const std::size_t n = alpha.shape(0);
            auto a = copy_f64_1d(alpha);
            auto b = copy_f64_1d(beta);
            auto* xs = new std::vector<double>(n);
            auto* ys = new std::vector<double>(n);
            {
                nb::gil_scoped_release release;
                cuicui::model::LabialOscillator osc;
                osc.prepare(internal_rate);
                osc.reset(x0, y0);
                for (std::size_t i = 0; i < n; ++i) {
                    osc.step(a[i], b[i], gamma);
                    (*xs)[i] = osc.x();
                    (*ys)[i] = osc.y();
                }
            }
            nb::capsule ox(xs, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            nb::capsule oy(ys, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            return nb::make_tuple(
                nb::ndarray<nb::numpy, double, nb::ndim<1>>(xs->data(), {n}, ox),
                nb::ndarray<nb::numpy, double, nb::ndim<1>>(ys->data(), {n}, oy));
        },
        nb::arg("alpha"),
        nb::arg("beta"),
        nb::arg("gamma"),
        nb::arg("internal_rate"),
        nb::arg("x0"),
        nb::arg("y0"),
        "Integrate the labial oscillator at the internal rate. Releases the GIL.");

    m.def(
        "inverse_solve",
        [](double nu, double tone, double alpha_breath, double physics, bool range_fold) {
            static const cuicui::control::InverseMap map;
            const auto r = map.solve(nu, tone, alpha_breath, physics, range_fold);
            nb::dict d;
            d["alpha"] = r.alpha;
            d["beta"] = r.beta;
            d["nu_used"] = r.nuUsed;
            d["reachable"] = r.reachable;
            d["folded"] = r.folded;
            d["clamped"] = r.clamped;
            return d;
        },
        nb::arg("nu"),
        nb::arg("tone"),
        nb::arg("alpha_breath"),
        nb::arg("physics"),
        nb::arg("range_fold") = true,
        "Inverse map: target ν and tone → (α, β).");

    m.def(
        "inverse_nu_range",
        [](double tone) {
            static const cuicui::control::InverseMap map;
            return nb::make_tuple(map.nuMin(), map.nuMax(tone));
        },
        nb::arg("tone") = 0.6,
        "Reachable ν interval (nu_min, nu_max(tone)).");

    m.def(
        "trachea_process",
        [](nb::ndarray<nb::numpy, const double, nb::ndim<1>, nb::c_contig> source, double internal_rate,
           double length_mm, double reflection, double loss, double input_gain) {
            const std::size_t n = source.shape(0);
            auto x = copy_f64_1d(source);
            auto* y = new std::vector<double>(n);
            {
                nb::gil_scoped_release release;
                cuicui::model::Trachea tr;
                tr.prepare(internal_rate, cuicui::model::kTracheaMaxLengthMm);
                tr.setParams(length_mm, reflection, loss, input_gain);
                tr.reset();
                for (std::size_t i = 0; i < n; ++i) {
                    (*y)[i] = tr.process(x[i]);
                }
            }
            nb::capsule owner(y, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            return nb::ndarray<nb::numpy, double, nb::ndim<1>>(y->data(), {n}, owner);
        },
        nb::arg("source"),
        nb::arg("internal_rate"),
        nb::arg("length_mm") = 33.0,
        nb::arg("reflection") = 0.5,
        nb::arg("loss") = 0.0,
        nb::arg("input_gain") = 1.0,
        "Delayed-feedback trachea at the internal rate. Releases the GIL.");

    m.def(
        "uvt_process",
        [](nb::ndarray<nb::numpy, const double, nb::ndim<1>, nb::c_contig> source, double internal_rate,
           double oec_freq_hz, double oec_damping, double beak_gape, double beak_damping, double size_scale) {
            const std::size_t n = source.shape(0);
            auto x = copy_f64_1d(source);
            auto* flow = new std::vector<double>(n);
            auto* dflow = new std::vector<double>(n);
            {
                nb::gil_scoped_release release;
                cuicui::model::UpperVocalTract uvt;
                uvt.prepare(internal_rate);
                uvt.setParams(oec_freq_hz, oec_damping, beak_gape, beak_damping, size_scale);
                uvt.reset();
                for (std::size_t i = 0; i < n; ++i) {
                    const auto o = uvt.process(x[i]);
                    (*flow)[i] = o.flow;
                    (*dflow)[i] = o.dflow;
                }
            }
            nb::capsule of(flow, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            nb::capsule od(dflow, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            return nb::make_tuple(
                nb::ndarray<nb::numpy, double, nb::ndim<1>>(flow->data(), {n}, of),
                nb::ndarray<nb::numpy, double, nb::ndim<1>>(dflow->data(), {n}, od));
        },
        nb::arg("source"),
        nb::arg("internal_rate"),
        nb::arg("oec_freq_hz") = 3000.0,
        nb::arg("oec_damping") = 0.1,
        nb::arg("beak_gape") = 0.5,
        nb::arg("beak_damping") = 0.3,
        nb::arg("size_scale") = 1.0,
        "Upper vocal tract (flow, dflow) at the internal rate. Releases the GIL.");

    m.def(
        "render_labial",
        [](nb::ndarray<nb::numpy, const float, nb::ndim<1>, nb::c_contig> pressure,
           nb::ndarray<nb::numpy, const float, nb::ndim<1>, nb::c_contig> beta, double gamma,
           double sample_rate, int oversampling, double gain_db, double alpha_offset, bool use_trachea,
           double trachea_length_mm, double trachea_reflection, double trachea_loss,
           double trachea_input_gain, bool use_uvt, double oec_freq_hz, double oec_damping, double beak_gape,
           double beak_damping, double out_radiation, std::uint64_t seed, float noise_beta_sigma,
           double noise_aspiration, double resp_attack_ms, double resp_release_ms,
           double noise_beta_bandwidth_hz) {
            if (pressure.shape(0) != beta.shape(0)) {
                throw std::invalid_argument("render_labial: pressure and beta must have the same length");
            }
            const int n = static_cast<int>(pressure.shape(0));
            std::vector<float> p(static_cast<std::size_t>(n));
            std::vector<float> b(static_cast<std::size_t>(n));
            const float* pp = pressure.data();
            const float* bb = beta.data();
            const auto sp = pressure.stride(0);
            const auto sb = beta.stride(0);
            for (int i = 0; i < n; ++i) {
                p[static_cast<std::size_t>(i)] = pp[static_cast<std::size_t>(sp) * static_cast<std::size_t>(i)];
                b[static_cast<std::size_t>(i)] = bb[static_cast<std::size_t>(sb) * static_cast<std::size_t>(i)];
            }
            auto* out = new std::vector<float>(static_cast<std::size_t>(n), 0.0f);
            {
                nb::gil_scoped_release release;
                cuicui::ProcessSpec spec;
                spec.hostSampleRate = sample_rate;
                spec.maxBlockSize = 64;
                spec.oversampling = oversampling;
                cuicui::BirdVoice voice;
                voice.prepare(spec);
                voice.setSource(cuicui::VoiceSource::Labial);
                voice.setTracheaEnabled(use_trachea);
                voice.setUvtEnabled(use_uvt);
                cuicui::VoiceSettings settings;
                settings.src_gamma = gamma;
                settings.out_gain_db = gain_db;
                settings.trachea_length_mm = trachea_length_mm;
                settings.trachea_reflection = trachea_reflection;
                settings.trachea_loss = trachea_loss;
                settings.trachea_input_gain = trachea_input_gain;
                settings.oec_damping = oec_damping;
                settings.beak_damping = beak_damping;
                settings.out_radiation = out_radiation;
                settings.seed = seed;
                settings.noise_aspiration = noise_aspiration;
                settings.resp_attack_ms = resp_attack_ms;
                settings.resp_release_ms = resp_release_ms;
                settings.noise_beta_bandwidth_hz = noise_beta_bandwidth_hz;
                voice.setSettings(settings);
                voice.reset(seed);
                voice.setOutGainSmoothingMs(0.0);
                voice.setOutGainRamp(static_cast<float>(gain_db), static_cast<float>(gain_db));
                const double off = alpha_offset;
                const float b0 = b.empty() ? 0.3f : b[0];
                voice.primeLabial(cuicui::model::kAlphaRest + off, static_cast<double>(b0), oec_freq_hz,
                                  beak_gape);
                constexpr int kBlock = 32;
                for (int i = 0; i < n;) {
                    const int ns = std::min(kBlock, n - i);
                    cuicui::ControlFrame from, to;
                    from.resp_pressure = p[static_cast<std::size_t>(i)];
                    to.resp_pressure = p[static_cast<std::size_t>(i + ns - 1)];
                    from.src_L_beta = b[static_cast<std::size_t>(i)];
                    to.src_L_beta = b[static_cast<std::size_t>(i + ns - 1)];
                    from.src_L_alpha_offset = static_cast<float>(off);
                    to.src_L_alpha_offset = static_cast<float>(off);
                    from.oec_freq_hz = static_cast<float>(oec_freq_hz);
                    to.oec_freq_hz = static_cast<float>(oec_freq_hz);
                    from.beak_gape = static_cast<float>(beak_gape);
                    to.beak_gape = static_cast<float>(beak_gape);
                    from.noise_beta_sigma = noise_beta_sigma;
                    to.noise_beta_sigma = noise_beta_sigma;
                    voice.process(from, to, out->data() + i, ns);
                    i += ns;
                }
            }
            nb::capsule owner(out, [](void* ptr) noexcept { delete static_cast<std::vector<float>*>(ptr); });
            return nb::ndarray<nb::numpy, float, nb::ndim<1>>(out->data(), {out->size()}, owner);
        },
        nb::arg("pressure"),
        nb::arg("beta"),
        nb::arg("gamma") = 24000.0,
        nb::arg("sample_rate") = 48000.0,
        nb::arg("oversampling") = 0,
        nb::arg("gain_db") = 0.0,
        nb::arg("alpha_offset") = 0.0,
        nb::arg("use_trachea") = true,
        nb::arg("trachea_length_mm") = 33.0,
        nb::arg("trachea_reflection") = 0.5,
        nb::arg("trachea_loss") = 0.3,
        nb::arg("trachea_input_gain") = 1.0,
        nb::arg("use_uvt") = true,
        nb::arg("oec_freq_hz") = 3000.0,
        nb::arg("oec_damping") = 0.1,
        nb::arg("beak_gape") = 0.5,
        nb::arg("beak_damping") = 0.3,
        nb::arg("out_radiation") = 1.0,
        nb::arg("seed") = 1,
        nb::arg("noise_beta_sigma") = 0.0,
        nb::arg("noise_aspiration") = 0.0,
        nb::arg("resp_attack_ms") = 8.0,
        nb::arg("resp_release_ms") = 15.0,
        nb::arg("noise_beta_bandwidth_hz") = 300.0,
        "Render the labial source through BirdVoice (optional trachea + UVT). Releases the GIL.");

    m.def(
        "bilateral_source_only",
        [](double alpha_l, double beta_l, double alpha_r, double beta_r, double gamma, double internal_rate,
           double coupling, int n, double xL0, double yL0, double xR0, double yR0) {
            if (n < 0) {
                throw std::invalid_argument("bilateral_source_only: n must be >= 0");
            }
            auto* xL = new std::vector<double>(static_cast<std::size_t>(n));
            auto* xR = new std::vector<double>(static_cast<std::size_t>(n));
            auto* mix = new std::vector<double>(static_cast<std::size_t>(n));
            {
                nb::gil_scoped_release release;
                cuicui::model::BilateralSource src;
                src.prepare(internal_rate);
                src.left().reset(xL0, yL0);
                src.right().reset(xR0, yR0);
                src.resetNoise(1);
                cuicui::model::BilateralFrame fr;
                fr.alphaBase = 0.0;
                fr.alphaOffL = alpha_l;
                fr.alphaOffR = alpha_r;
                fr.betaL = beta_l;
                fr.betaR = beta_r;
                fr.gateL = 1.0;
                fr.gateR = 1.0;
                fr.gamma = gamma;
                fr.coupling = coupling;
                fr.sigmaBeta = 0.0;
                // process() uses α = alphaBase + offset − (1−g)*0.6. With gates 1:
                // α_s = offset, so pass the desired α as the offset.
                for (int i = 0; i < n; ++i) {
                    const auto o = src.process(fr);
                    (*xL)[static_cast<std::size_t>(i)] = src.left().x();
                    (*xR)[static_cast<std::size_t>(i)] = src.right().x();
                    (*mix)[static_cast<std::size_t>(i)] = o.mixTilde;
                }
            }
            nb::capsule cL(xL, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            nb::capsule cR(xR, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            nb::capsule cM(mix, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
            const auto nn = static_cast<std::size_t>(n);
            return nb::make_tuple(nb::ndarray<nb::numpy, double, nb::ndim<1>>(xL->data(), {nn}, cL),
                                  nb::ndarray<nb::numpy, double, nb::ndim<1>>(xR->data(), {nn}, cR),
                                  nb::ndarray<nb::numpy, double, nb::ndim<1>>(mix->data(), {nn}, cM));
        },
        nb::arg("alpha_l"),
        nb::arg("beta_l"),
        nb::arg("alpha_r"),
        nb::arg("beta_r"),
        nb::arg("gamma") = 24000.0,
        nb::arg("internal_rate") = 192000.0,
        nb::arg("coupling") = 0.0,
        nb::arg("n") = 19200,
        nb::arg("xL0") = 0.0,
        nb::arg("yL0") = 0.0,
        nb::arg("xR0") = 0.0,
        nb::arg("yR0") = 0.0,
        "Two coupled labial oscillators (DC-blocked mix). Releases the GIL.");

    nb::class_<cuicui::control::SideSwitcher>(m, "SideSwitcher")
        .def(nb::init<>())
        .def("prepare", &cuicui::control::SideSwitcher::prepare, nb::arg("control_rate"))
        .def("set_crossfade_ms", &cuicui::control::SideSwitcher::setCrossfadeMs, nb::arg("ms"))
        .def("set_active", &cuicui::control::SideSwitcher::setActive, nb::arg("side"))
        .def(
            "next",
            [](cuicui::control::SideSwitcher& sw, double beta_l, double beta_r) {
                const auto t = sw.next(beta_l, beta_r);
                return nb::make_tuple(t.betaL, t.betaR, t.gateL, t.gateR);
            },
            nb::arg("beta_l"),
            nb::arg("beta_r"),
            "Returns (beta_l, beta_r, gate_l, gate_r).");

    m.def(
        "decimator_stage_taps",
        [](double host_rate, int requested, int stage) {
            cuicui::ProcessSpec spec;
            spec.hostSampleRate = host_rate;
            spec.maxBlockSize = 64;
            spec.oversampling = requested;
            cuicui::dsp::Oversampling os;
            os.prepare(spec);
            if (stage < 0 || stage >= os.numStages()) {
                throw std::invalid_argument("decimator stage out of range");
            }
            const auto& st = os.stage(stage);
            nb::dict d;
            d["taps"] = st.taps();
            d["input_rate"] = st.inputRate();
            d["num_taps"] = st.numTaps();
            d["group_delay"] = st.groupDelayInputSamples();
            return d;
        },
        nb::arg("host_rate"),
        nb::arg("requested") = 4,
        nb::arg("stage") = 0,
        "FIR taps of one half-band stage (for plots / tests).");

    m.def(
        "render_controls",
        [](const nb::dict& settings, const nb::dict& controls, double control_rate_hz, double sample_rate,
           std::uint64_t seed, int oversampling, nb::object state) {
            auto st = settings_from_dict(settings);
            std::unordered_map<std::string, std::vector<float>> ctrls;
            for (nb::handle k : controls.keys()) {
                const std::string id = nb::cast<std::string>(k);
                ctrls[id] = copy_f32_1d(controls[k]);
            }
            cuicui::VoiceState in{};
            const cuicui::VoiceState* pin = nullptr;
            if (!state.is_none()) {
                const auto b = nb::cast<nb::bytes>(state);
                if (b.size() != sizeof(cuicui::VoiceState)) {
                    throw std::invalid_argument("render_controls: state bytes have the wrong size");
                }
                std::memcpy(&in, b.c_str(), sizeof(in));
                pin = &in;
            }
            cuicui::VoiceState out{};
            std::vector<float> buf;
            {
                nb::gil_scoped_release release;
                buf = cuicui::io::render_controls(st, ctrls, control_rate_hz, sample_rate, seed, oversampling,
                                                  pin, &out);
            }
            auto* heap = new std::vector<float>(std::move(buf));
            nb::capsule owner(heap, [](void* p) noexcept { delete static_cast<std::vector<float>*>(p); });
            nb::bytes stateOut(reinterpret_cast<const char*>(&out), sizeof(out));
            return nb::make_tuple(
                nb::ndarray<nb::numpy, float, nb::ndim<1>>(heap->data(), {heap->size()}, owner), stateOut);
        },
        nb::arg("settings"),
        nb::arg("controls"),
        nb::arg("control_rate_hz"),
        nb::arg("sample_rate"),
        nb::arg("seed") = 1,
        nb::arg("oversampling") = 0,
        nb::arg("state") = nb::none(),
        "Render control arrays through BirdVoice. Returns (audio float32, state bytes). Releases the GIL.");

    m.def(
        "archetype_names",
        []() {
            nb::list out;
            for (const auto& n : cuicui::control::archetypeNames()) {
                out.append(n);
            }
            return out;
        },
        "Names of the hand-designed C4 archetypes.");

    m.def(
        "archetype_params",
        [](const std::string& name) {
            const auto* info = cuicui::control::findArchetype(name);
            if (info == nullptr) {
                throw std::invalid_argument("unknown archetype: " + name);
            }
            nb::list out;
            for (int i = 0; i < info->nParams; ++i) {
                const auto& p = info->params[i];
                nb::dict d;
                d["name"] = p.name;
                d["default"] = p.def;
                d["min"] = p.min;
                d["max"] = p.max;
                d["choice"] = p.isChoice;
                out.append(d);
            }
            return out;
        },
        nb::arg("name"),
        "Parameter specs for one archetype.");

    m.def(
        "generate_archetype",
        [](const std::string& name, const nb::dict& params, std::uint64_t seed) {
            if (cuicui::control::findArchetype(name) == nullptr) {
                throw std::invalid_argument("unknown archetype: " + name);
            }
            cuicui::control::ArchetypeParamBag bag;
            for (nb::handle k : params.keys()) {
                const std::string key = nb::cast<std::string>(k);
                nb::handle v = params[k];
                if (nb::isinstance<nb::str>(v)) {
                    bag.str[key] = nb::cast<std::string>(v);
                } else {
                    bag.num[key] = nb::cast<double>(v);
                }
            }
            const auto g = cuicui::control::generateArchetype(name, bag, seed);
            nb::dict curves;
            curves["samples"] = cuicui::control::GestureCurves::kN;
            auto arr = [](const std::array<double, cuicui::control::GestureCurves::kN>& a) {
                nb::list L;
                for (double x : a) {
                    L.append(x);
                }
                return L;
            };
            curves["pitch_cents"] = arr(g.curves.pitchCents);
            curves["amp_db"] = arr(g.curves.ampDb);
            curves["tone"] = arr(g.curves.tone);
            curves["side"] = arr(g.curves.side);
            nb::dict hints;
            auto hint = [](double x) -> nb::object {
                if (!std::isfinite(x)) {
                    return nb::none();
                }
                return nb::cast(x);
            };
            hints["oec_ratio"] = hint(g.hints.oecRatio);
            hints["gape"] = hint(g.hints.gape);
            hints["second_voice_cents"] = hint(g.hints.secondVoiceCents);
            nb::dict out;
            out["name"] = name;
            out["duration_ms"] = g.durationMs;
            out["movement_score"] = cuicui::control::movementScore(g.curves);
            out["curves"] = curves;
            out["hints"] = hints;
            return out;
        },
        nb::arg("name"),
        nb::arg("params") = nb::dict(),
        nb::arg("seed") = 1,
        "Generate gesture curves for a named archetype.");

    m.def(
        "render_score",
        [](const nb::dict& score, double sample_rate) {
            nb::object dumps = nb::module_::import_("json").attr("dumps");
            const std::string text = nb::cast<std::string>(dumps(score));
            const auto parsed = cuicui::io::parse_gesture_score_string(text);
            std::vector<float> buf;
            {
                nb::gil_scoped_release release;
                cuicui::control::ScorePlayer player;
                player.load(parsed);
                buf = player.render(sample_rate, 0);
            }
            auto* heap = new std::vector<float>(std::move(buf));
            nb::capsule owner(heap, [](void* p) noexcept { delete static_cast<std::vector<float>*>(p); });
            return nb::ndarray<nb::numpy, float, nb::ndim<1>>(heap->data(), {heap->size()}, owner);
        },
        nb::arg("score"),
        nb::arg("sample_rate") = 48000.0,
        "Render a gesture-score dict to float32 mono audio. Releases the GIL.");

    nb::class_<cuicui::BirdVoice>(m, "Voice")
        .def(
            "__init__",
            [](cuicui::BirdVoice* self, double sample_rate, int oversampling) {
                new (self) cuicui::BirdVoice();
                cuicui::ProcessSpec spec;
                spec.hostSampleRate = sample_rate;
                spec.maxBlockSize = 512;
                spec.oversampling = oversampling;
                self->prepare(spec);
                self->setSource(cuicui::VoiceSource::Labial);
                self->reset(1);
            },
            nb::arg("sample_rate"),
            nb::arg("oversampling") = 0)
        .def(
            "set_settings",
            [](cuicui::BirdVoice& v, const nb::dict& d) { v.setSettings(settings_from_dict(d)); },
            nb::arg("settings"))
        .def(
            "reset",
            [](cuicui::BirdVoice& v, std::uint64_t seed) { v.reset(seed); },
            nb::arg("seed") = 1)
        .def(
            "prime",
            [](cuicui::BirdVoice& v, double alpha, double beta, double oec_hz, double gape) {
                v.primeLabial(alpha, beta, oec_hz, gape);
            },
            nb::arg("alpha") = -0.1,
            nb::arg("beta") = 0.3,
            nb::arg("oec_hz") = 3000.0,
            nb::arg("gape") = 0.5)
        .def(
            "process",
            [](cuicui::BirdVoice& v, const nb::dict& frame_from, const nb::dict& frame_to, int n) {
                if (n < 0) {
                    throw std::invalid_argument("Voice.process: n must be >= 0");
                }
                auto* heap = new std::vector<float>(static_cast<std::size_t>(n), 0.0f);
                const auto from = frame_from_dict(frame_from);
                const auto to = frame_from_dict(frame_to);
                {
                    nb::gil_scoped_release release;
                    v.setOutGainRamp(0.0f, 0.0f);
                    v.process(from, to, heap->data(), n);
                }
                nb::capsule owner(heap, [](void* p) noexcept { delete static_cast<std::vector<float>*>(p); });
                return nb::ndarray<nb::numpy, float, nb::ndim<1>>(heap->data(), {heap->size()}, owner);
            },
            nb::arg("frame_from"),
            nb::arg("frame_to"),
            nb::arg("n"),
            "Render n host samples. Releases the GIL.")
        .def("save_state",
             [](const cuicui::BirdVoice& v) {
                 cuicui::VoiceState s{};
                 v.saveState(s);
                 return nb::bytes(reinterpret_cast<const char*>(&s), sizeof(s));
             })
        .def("load_state",
             [](cuicui::BirdVoice& v, nb::bytes b) {
                 if (b.size() != sizeof(cuicui::VoiceState)) {
                     throw std::invalid_argument("Voice.load_state: wrong size");
                 }
                 cuicui::VoiceState s{};
                 std::memcpy(&s, b.c_str(), sizeof(s));
                 v.loadState(s);
             },
             nb::arg("state"))
        .def("telemetry", [](const cuicui::BirdVoice& v) { return telemetry_to_dict(v.telemetry()); })
        .def("latency_samples", &cuicui::BirdVoice::latencySamples);
}
