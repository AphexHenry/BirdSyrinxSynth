#include "cuicui/dsp/oversampling.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/params.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace nb = nanobind;

NB_MODULE(cuicui_core, m) {
    m.doc() = "Syrinx C++ engine (parameter registry, offline render, oversampling)";

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
