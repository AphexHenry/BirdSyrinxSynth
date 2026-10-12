#include "ParameterLayout.h"

#include "cuicui/params.hpp"

#include <cmath>

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
        if (std::string_view(spec.id) == "perf.mode") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"perform", "physiology"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.scale") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name,
                juce::StringArray{"chromatic", "major", "minor", "pentatonic_major", "pentatonic_minor",
                                  "whole_tone"},
                0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.sync_division") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"}, 2));
            continue;
        }
        if (std::string_view(spec.id) == "perf.voice_bank") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"archetypes", "learned"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.polyphony") {
            params.push_back(std::make_unique<juce::AudioParameterInt>(
                pid, name, 1, 16, static_cast<int>(std::lround(spec.def))));
            continue;
        }
        // WP-R1 (D-025): whistle voice, note rule, mapping, order; toggles as on/off switches.
        if (std::string_view(spec.id) == "voice.type") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"Whistle", "Anatomical (experimental)"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.note_rule") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"Longest steady part, else ending", "Steady parts", "Landing", "Average"},
                0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.mapping") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"Own height", "Follow the keyboard"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "perf.order") {
            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                pid, name, juce::StringArray{"Random start", "Phrase start", "Shuffled"}, 0));
            continue;
        }
        if (std::string_view(spec.id) == "voice.harmonics" || std::string_view(spec.id) == "perf.first_on_note" ||
            std::string_view(spec.id) == "out.room") {
            params.push_back(std::make_unique<juce::AudioParameterBool>(pid, name, spec.def >= 0.5));
            continue;
        }
        if (std::string_view(spec.id) == "perf.seed") {
            params.push_back(std::make_unique<juce::AudioParameterInt>(pid, name, 0, 9999, 0));
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

        // Readable values for hosts and the editor: 1–3 decimals depending on the size (WP-R1).
        juce::AudioParameterFloatAttributes attrs = juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float v, int) {
                const float a = std::abs(v);
                return juce::String(v, a >= 100.0f ? 1 : (a >= 1.0f ? 2 : 3));
            });
        if (label.isNotEmpty()) {
            attrs = attrs.withLabel(label);
        }
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            pid, name, range, static_cast<float>(spec.def), attrs));
    }

    return {params.begin(), params.end()};
}
