#pragma once

#include "cuicui/control/phrase_order.hpp"
#include "cuicui/control/placement.hpp"
#include "cuicui/control/syllable_bank.hpp"

#include <cstdint>

namespace cuicui::control {

/// One input event for a playing mode, at a sample offset inside the current block (D-021).
struct PlayEvent {
    enum class Type : std::uint8_t { NoteOn, NoteOff, Sustain, ModWheel, AllNotesOff, AllSoundOff };
    Type type = Type::NoteOn;
    int offset = 0;     ///< sample inside the block
    int note = 60;      ///< MIDI note (NoteOn / NoteOff)
    float value = 1.0f; ///< velocity 0–1 (NoteOn), controller 0–1 (Sustain ≥ 0.5 = down, ModWheel)
};

/// One syllable to sing, produced by a playing mode for the voices.
struct SyllableRequest {
    int syllable = 0;        ///< bank index
    int offset = 0;          ///< start sample inside the block
    double shiftCents = 0.0; ///< pitch shift applied to the whole syllable
    float gain = 0.3f;       ///< from velocity
    float pan = 0.0f;        ///< −1 … 1 (equal power)
    int singer = 0;          ///< who asked (slot index of the mode)
    std::uint32_t serial = 0; ///< running count of requests since reset (logs, tests)
    bool first = false;      ///< first syllable of a key press
    int note = -1;           ///< MIDI note of the key that asked (−1 = none); displays (WP-R4)
    double targetCents = 0.0; ///< that key's target pitch, MIDI cents (placementTarget); displays (WP-R4)
    bool releaseAlarm = false; ///< the call that ends a long key press (D-030)
};

/// Live settings a playing mode reads every block (parameters of the plugin, WP-R1 §3.4).
struct PlayContext {
    NoteRule rule = NoteRule::Rule;
    KeyMapping mapping = KeyMapping::OwnHeight;
    double sizeSt = 0.0;     ///< transposes the whole bird (semitones)
    bool firstOnNote = true; ///< first syllable of a key press lands exactly on the note
    double snap = 0.0;       ///< following syllables: 0 natural melody … 1 all on the note
    OrderMode order = OrderMode::RandomStart;
    double pauseS = 0.5; ///< pause between phrases while a key is held
    int polyphony = 4;   ///< held keys that sing at once; over the limit the oldest is released
    double spread = 0.3; ///< stereo spread of the singers (0 = centre)
    /// D-030 (human, 2026-10-12): the bird's long rising call (the "alarm" syllables) is kept for the end
    /// of a long key press. It never starts or continues a phrase; when a key that sang at least
    /// `releaseAlarmHoldS` is released, the bird ends with one, after the syllable still sounding.
    /// false = the R1 / PROTO1 behaviour (reference comparisons).
    bool releaseAlarm = true;
    double releaseAlarmHoldS = 0.6;
};

/// Input → syllable requests. Modes A + B (ModeAB) today; C (conductor/flock), D (drawn gestures) and
/// E (whistle-in) implement the same interface later (D-021). Real-time rules: process() and reset()
/// must not allocate, lock or throw.
class PlayingMode {
public:
    virtual ~PlayingMode() = default;
    virtual void prepare(double sampleRate) = 0; ///< may allocate
    virtual void reset(std::uint64_t seed) noexcept = 0;
    virtual void setBank(const SyllableBank* bank) noexcept = 0;
    /// One block: events sorted by offset in, requests sorted by offset out (at most maxOut).
    /// Returns the number of requests written.
    virtual int process(const PlayEvent* events, int numEvents, int numSamples, const PlayContext& ctx,
                        SyllableRequest* out, int maxOut) noexcept = 0;
    /// Stop scheduling for every key (voices finish their current syllable).
    virtual void releaseAll() noexcept = 0;
    /// Keys still scheduling syllables (held or sustained).
    [[nodiscard]] virtual int activeSingers() const noexcept = 0;
};

} // namespace cuicui::control
