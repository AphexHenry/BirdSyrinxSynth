#include "cuicui/dsp/oversampling.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/render_spec.hpp"
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
#include <cstring>
#include <stdexcept>
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
           double beak_damping, double out_radiation) {
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
                voice.setSettings(settings);
                voice.reset(1);
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
        "Render the labial source through BirdVoice (optional trachea + UVT). Releases the GIL.");

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
}
