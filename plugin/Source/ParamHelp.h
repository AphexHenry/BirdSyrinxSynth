#pragma once

#include <string_view>
#include <utility>

/// WP-R4: one sentence of help per parameter, in musician's words. The editor shows it in its help line
/// when the mouse is over a control. Every registry parameter must have an entry (test_editor.cpp).
namespace syrinx {

inline constexpr std::pair<std::string_view, std::string_view> kParamHelp[] = {
    // --- the whistle voice (Play page) ---------------------------------------------------------
    {"voice.type", "Whistle: the bird's real pitch and loudness on a clean whistle (the default). Anatomical: the "
                   "experimental physical model of the syrinx; its controls are on the Advanced page."},
    {"voice.harmonics", "Adds the bird's own 2nd and 3rd harmonics, at the level measured in the recording "
                        "(-30 / -33 dB). Off: a pure whistle."},
    {"perf.note_rule", "What counts as a syllable's note when it is placed on your key: its longest steady part "
                       "(else its ending), its steady parts, where it lands, or its average."},
    {"perf.mapping", "Own height: each key sings its note name at the bird's natural height. Follow keys: the bird "
                     "sings three octaves above your key, so tunes keep their shape (best from C4 to C6)."},
    {"perf.first_on_note", "The first syllable of each key press lands exactly on your note. Off: it keeps its "
                           "natural place in the bird's melody."},
    {"perf.snap", "Melody. Where the bird's next syllables land while you hold a key: 0% keeps the bird's own "
                  "melody around your note, 100% puts every syllable on the note. The mod wheel adds to it."},
    {"perf.order", "Random: each key starts on a random syllable, then follows the real phrase. Phrase: always "
                   "from the start of a phrase. Shuffle: syllables in random order."},
    {"perf.pause_s", "Pause between phrases while you hold a key."},
    {"perf.size_st", "Size. Moves the whole bird up or down, in semitones: down is a bigger, lower bird, up a "
                     "smaller, higher one."},
    {"perf.polyphony", "Birds. How many keys can sing at once: each held key is one bird. Over the limit, the "
                       "oldest key stops singing."},
    {"out.stereo_spread", "Spread. How far apart the birds sit, left to right, when you hold several keys."},
    {"out.room", "A small room around the bird. Off: the dry whistle, for your own reverb."},
    {"out.master_db", "Volume of Syrinx."},
    {"perf.seed", "Fixes the bird's random choices for every key (0 = use the engine seed). The same seed and the "
                  "same playing give the same song."},
    {"engine.seed", "Seed of all random choices: which syllable a key starts on, where each bird sits, the room."},
    // --- the anatomical voice (Advanced page) --------------------------------------------------
    {"resp.pressure", "Anatomical voice: air pressure from the lungs. More pressure, a louder and brighter note."},
    {"resp.attack_ms", "Anatomical voice: how fast the breath rises at the start of a note."},
    {"resp.release_ms", "Anatomical voice: how fast the breath falls at the end of a note."},
    {"src.gamma", "Anatomical voice: time scale of the vibrating labia; it sets the basic pitch range."},
    {"src.L.beta", "Anatomical voice: muscle tension of the left sound source. More tension, higher pitch."},
    {"src.L.alpha_offset", "Anatomical voice: how easily the left sound source starts to vibrate."},
    {"src.R.beta", "Anatomical voice: muscle tension of the right sound source. More tension, higher pitch."},
    {"src.R.alpha_offset", "Anatomical voice: how easily the right sound source starts to vibrate."},
    {"src.L.gate", "Anatomical voice: opens or closes the left sound source."},
    {"src.R.gate", "Anatomical voice: opens or closes the right sound source."},
    {"src.coupling", "Anatomical voice: how strongly the two sound sources pull each other into step."},
    {"noise.beta_sigma", "Anatomical voice: random wobble of the muscle tension, for a rougher sound."},
    {"noise.beta_bandwidth_hz", "Anatomical voice: how fast that wobble moves."},
    {"noise.aspiration", "Anatomical voice: breath noise mixed into the sound."},
    {"trachea.length_mm", "Anatomical voice: length of the windpipe. Longer gives lower resonances."},
    {"trachea.reflection", "Anatomical voice: how much sound bounces back inside the windpipe."},
    {"trachea.loss", "Anatomical voice: damping inside the windpipe."},
    {"trachea.input_gain", "Anatomical voice: how much of the source enters the windpipe."},
    {"oec.freq_hz", "Anatomical voice: resonance of the cavity in the throat (OEC)."},
    {"oec.damping", "Anatomical voice: how sharp that throat resonance is."},
    {"beak.gape", "Anatomical voice: how wide the beak opens. Wider is brighter."},
    {"beak.damping", "Anatomical voice: damping at the beak."},
    {"out.radiation", "Anatomical voice: how the sound leaves the beak. More favours the high frequencies."},
    {"out.gain_db", "Anatomical voice: output level of the engine."},
    {"body.size", "Anatomical voice: body size of the bird. Bigger is lower."},
    {"perf.range_fold", "Anatomical voice: folds notes outside the playable range back into it."},
    {"perf.mode", "Anatomical voice: Perform plays the bird from the keyboard; Physiology lets you set its body "
                  "directly."},
    {"perf.breath", "Anatomical voice: how much breath the bird uses."},
    {"perf.tone", "Anatomical voice: from soft and pure to bright and rich."},
    {"perf.physics", "Anatomical voice: how freely the physics may wander from the target pitch."},
    {"perf.motion", "Anatomical voice: how much the pitch moves on its own. The mod wheel adds to it."},
    {"perf.range", "Anatomical voice: how far the generated melodies range."},
    {"perf.speed", "Anatomical voice: speed of the generated gestures."},
    {"perf.glide_ms", "Anatomical voice: glide time between notes."},
    {"perf.bend_range_st", "Anatomical voice: pitch-bend range, in semitones."},
    {"perf.scale", "Anatomical voice: the scale generated notes snap to."},
    {"perf.root", "Anatomical voice: the root note of that scale."},
    {"perf.oec_tracking", "Anatomical voice: how much the throat resonance follows the pitch."},
    {"perf.gape_tracking", "Anatomical voice: how much the beak opens as the pitch rises."},
    {"perf.side_switch_cents", "Anatomical voice: the interval at which the bird switches between its two sound "
                               "sources."},
    {"perf.leash", "Anatomical voice: how closely the old generator stays on your note (0 = exactly your note)."},
    {"perf.variation", "Anatomical voice: variation of the old generator."},
    {"perf.density", "Anatomical voice: how busy the old generator is."},
    {"perf.tempo_sync", "Anatomical voice: locks the old generator to the host tempo."},
    {"perf.sync_division", "Anatomical voice: the note length used when locked to the tempo."},
    {"perf.voice_bank", "Anatomical voice: gesture source of the old generator (a placeholder)."},
    {"perf.freeze", "Anatomical voice: repeats the old generator's first motif."},
    {"engine.oversampling", "Anatomical voice: internal rate of the engine. Auto is best."},
    {"test.freq_hz", "Engine test tone: frequency (for development)."},
    {"test.gain_db", "Engine test tone: level (for development)."},
    {"test.waveform", "Engine test tone: waveform (for development)."},
};

/// The help sentence for a parameter ID ("" if there is none).
inline std::string_view paramHelp(std::string_view id) noexcept {
    for (const auto& [key, text] : kParamHelp) {
        if (key == id) {
            return text;
        }
    }
    return {};
}

} // namespace syrinx
