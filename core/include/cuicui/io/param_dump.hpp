#pragma once

#include "cuicui/params.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace cuicui::io {

inline nlohmann::json param_spec_to_json(const ParamSpec& p) {
    return nlohmann::json{
        {"id", p.id},
        {"name", p.name},
        {"unit", p.unit},
        {"min", p.min},
        {"max", p.max},
        {"default", p.def},
        {"skew", p.skew},
        {"smoothing_ms", p.smoothingMs},
        {"layer", layerName(p.layer)},
        {"fast", p.fast},
        {"since", p.sinceWP},
    };
}

inline nlohmann::json param_specs_json() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : kParamTable) {
        arr.push_back(param_spec_to_json(p));
    }
    return arr;
}

inline std::string dump_params_json_string() { return param_specs_json().dump(2); }

} // namespace cuicui::io
