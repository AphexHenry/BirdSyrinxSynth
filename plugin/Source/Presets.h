#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <utility>
#include <vector>

/// WP-R4: one preset (file format `cuicui.preset` v1, D-028).
/// A preset sets every parameter: the ones it lists to their values, all others to their defaults,
/// so presets written before a parameter existed still load the same sound.
struct Preset {
    juce::String name;
    juce::String category;
    juce::String author;
    juce::String description;
    juce::String voiceBank;
    std::vector<std::pair<juce::String, float>> values; ///< parameter ID → plain value (choice index, bool 0/1)
    bool factory = false;
    juce::File file; ///< user presets only
};

/// Factory presets (compiled in, `assets/presets/*.preset.json`) and user presets (one JSON file each in
/// the user folder). Message thread only: presets are never read, parsed or applied on the audio thread.
/// Applying a preset sets parameters through the host (setValueNotifyingHost); the audio thread only sees
/// parameter values change, as with automation.
class PresetManager {
public:
    static constexpr const char* kFormat = "cuicui.preset";
    static constexpr int kVersion = 1;
    static constexpr const char* kDefaultName = "Finch"; ///< the factory preset that equals the defaults

    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    // --- library ----------------------------------------------------------------------------------
    [[nodiscard]] const std::vector<Preset>& factory() const noexcept { return factory_; }
    [[nodiscard]] const std::vector<Preset>& user() const noexcept { return user_; }
    /// `~/Library/Application Support/Cuicui Instruments/Syrinx/Presets` on macOS (created when saving).
    [[nodiscard]] static juce::File defaultUserFolder();
    void setUserFolder(const juce::File& folder); ///< tests
    [[nodiscard]] const juce::File& userFolder() const noexcept { return userFolder_; }
    void rescanUser();
    /// Factory presets first (in file order), then user presets (by name).
    [[nodiscard]] int size() const noexcept { return static_cast<int>(factory_.size() + user_.size()); }
    [[nodiscard]] const Preset* at(int index) const noexcept;
    [[nodiscard]] int indexOfCurrent() const;
    [[nodiscard]] const Preset* findFactory(const juce::String& name) const noexcept;

    // --- loading and saving ---------------------------------------------------------------------
    void load(const Preset& preset);
    bool loadIndex(int index);
    void step(int delta); ///< next (+1) / previous (−1), wrapping
    /// The current parameter values as a preset (every parameter).
    [[nodiscard]] Preset capture(const juce::String& name, const juce::String& category,
                                 const juce::String& description) const;
    [[nodiscard]] juce::File userFileFor(const juce::String& name) const;
    /// Save the current sound as a user preset. Fails (with a message) if the file exists and !overwrite.
    bool saveUser(const juce::String& name, const juce::String& description, bool overwrite, juce::String& error);

    // --- JSON ---------------------------------------------------------------------------------------
    [[nodiscard]] std::string toJson(const Preset& preset) const;
    /// Parse a preset file. Unknown parameter IDs are ignored (counted in `ignored`); a wrong format,
    /// version or value type is an error.
    bool fromJson(const std::string& text, Preset& out, juce::String& error, int* ignored = nullptr) const;

    // --- the current preset (stored in the plugin state) -------------------------------------------
    [[nodiscard]] juce::String currentName() const;
    [[nodiscard]] bool currentIsUser() const;
    /// Some parameter differs from the values the current preset set.
    [[nodiscard]] bool isModified();
    /// Plugin state: any thread (the host may save or restore off the message thread).
    void writeState(juce::XmlElement& xml) const;
    void readState(const juce::XmlElement& xml);

    /// The normalised value a preset gives each parameter (registry order).
    [[nodiscard]] std::vector<float> targetsOf(const Preset& preset) const;
    [[nodiscard]] const std::vector<juce::RangedAudioParameter*>& parameters() const noexcept { return params_; }
    /// The syllable bank saved presets name (`voice_bank`).
    void setVoiceBankName(const juce::String& name) { bankName_ = name; }

private:
    void loadFactory();
    [[nodiscard]] juce::RangedAudioParameter* param(const juce::String& id) const noexcept;
    void setCurrent(const juce::String& name, bool user);

    juce::AudioProcessorValueTreeState& apvts_;
    std::vector<juce::RangedAudioParameter*> params_;
    std::vector<Preset> factory_;
    std::vector<Preset> user_;
    juce::File userFolder_;
    juce::String bankName_{"hf_22938+hf_22937"};

    mutable juce::CriticalSection lock_; // the current name / flag (state save/restore may run on any thread)
    juce::String currentName_{kDefaultName};
    bool currentUser_ = false;
    bool needReference_ = true; ///< recompute reference_ on the message thread (after a state restore)
    std::vector<float> reference_;
};
