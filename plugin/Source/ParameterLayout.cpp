#include "ParameterLayout.h"

#include "cuicui/params.hpp"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(static_cast<std::size_t>(cuicui::kParamCount));

    for (const auto& spec : cuicui::kParamTable) {
        const int hint = cuicui::versionHintFromSinceWP(spec.sinceWP);
        const juce::ParameterID pid{spec.id, hint};
        const juce::String name{spec.name};
        const juce::String label{spec.unit};

        if (std::string_view(spec.id) == "engine.oversampling") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"auto", "1", "2", "4", "8"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "test.waveform") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"sine", "saw", "noise"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "engine.seed") {
            params.push_back(std::make_unique<juce::AudioParameterInt>(
                pid, name, 0, 2147483647, 1));
            continue;
        }

        const cuicui::ParamSpec captured = spec;
        // JUCE: convertFrom0To1 first, convertTo0To1 second.
        juce::NormalisableRange<float> range{
            static_cast<float>(spec.min),
            static_cast<float>(spec.max),
            [captured](float, float, float u) {
                return static_cast<float>(cuicui::fromNormalised(captured, static_cast<double>(u)));
            },
            [captured](float, float, float x) {
                return static_cast<float>(cuicui::toNormalised(captured, static_cast<double>(x)));
            },
        };

        juce::AudioParameterFloatAttributes attrs;
        if (label.isNotEmpty()) {
            attrs = attrs.withLabel(label);
        }
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            pid, name, range, static_cast<float>(spec.def), attrs));
    }

    return {params.begin(), params.end()};
}
