#pragma once

#include "PluginProcessor.h"

/// Tests written before WP-R1 exercise the anatomical engine. Since R1 the default voice is the whistle
/// and the default polyphony 4 (D-025); this restores the voice and polyphony those tests were written for.
inline void useAnatomicalVoice(SyrinxAudioProcessor& p) {
    p.setParamPlain("voice.type", 1.0f);
    p.setParamPlain("perf.polyphony", 1.0f);
}
