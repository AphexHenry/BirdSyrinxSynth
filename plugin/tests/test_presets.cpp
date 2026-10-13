// WP-R4: presets (cuicui.preset v1) and the syllable feed to the editor.
#include "BinaryData.h"
#include "PluginProcessor.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

std::vector<float> normalisedValues(PresetManager& pm) {
    std::vector<float> v;
    for (auto* p : pm.parameters()) {
        v.push_back(p->getValue());
    }
    return v;
}

float plainOf(SyrinxAudioProcessor& proc, const char* id) {
    auto* p = proc.apvts.getParameter(id);
    return p->convertFrom0to1(p->getValue());
}

struct TempFolder {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("syrinx_preset_test_" + juce::String::toHexString(juce::Random().nextInt64()));
    ~TempFolder() { dir.deleteRecursively(); }
};

} // namespace

TEST_CASE("presets: every factory preset parses, with only known parameters") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    REQUIRE(pm.factory().size() >= 8);
    CHECK(pm.factory().front().name == PresetManager::kDefaultName);
    for (int i = 0; i < juce::jmin(BinaryData::namedResourceListSize, 64); ++i) {
        const juce::String file{BinaryData::originalFilenames[i]};
        if (!file.endsWith(".preset.json")) {
            continue;
        }
        int bytes = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], bytes);
        Preset p;
        juce::String error;
        int ignored = -1;
        INFO(file.toStdString());
        REQUIRE(pm.fromJson(std::string(data, static_cast<std::size_t>(bytes)), p, error, &ignored));
        CHECK(ignored == 0);
        CHECK(p.description.isNotEmpty());
        CHECK(p.category.isNotEmpty());
    }
}

TEST_CASE("presets: the default preset equals the parameter defaults") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    const auto* def = pm.findFactory(PresetManager::kDefaultName);
    REQUIRE(def != nullptr);
    const auto t = pm.targetsOf(*def);
    const auto now = normalisedValues(pm);
    REQUIRE(t.size() == now.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        CHECK(std::abs(t[i] - now[i]) < 1e-7f);
    }
    CHECK(pm.currentName() == PresetManager::kDefaultName);
    CHECK_FALSE(pm.isModified());
}

TEST_CASE("presets: round trip load -> export JSON -> reload keeps every parameter (criterion 2)") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    for (const auto& preset : pm.factory()) {
        INFO(preset.name.toStdString());
        pm.load(preset);
        CHECK(pm.currentName() == preset.name);
        CHECK_FALSE(pm.isModified());
        const auto a = normalisedValues(pm);
        const auto json = pm.toJson(pm.capture(preset.name, preset.category, preset.description));
        Preset back;
        juce::String error;
        REQUIRE(pm.fromJson(json, back, error));
        pm.load(*pm.findFactory("Big bird")); // move away, so reloading has to set everything again
        pm.load(back);
        const auto b = normalisedValues(pm);
        REQUIRE(a.size() == b.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) {
            worst = std::max(worst, std::abs(a[i] - b[i]));
        }
        CHECK(worst <= 1e-6f);
    }
}

TEST_CASE("presets: a preset sets what it lists and resets everything else to default") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    proc.setParamPlain("perf.pause_s", 1.7f);
    proc.setParamPlain("out.room", 0.0f);
    proc.setParamPlain("trachea.length_mm", 60.0f);
    CHECK(pm.isModified());
    pm.load(*pm.findFactory("Finch, on the note"));
    CHECK(plainOf(proc, "perf.snap") == 1.0f);
    CHECK(plainOf(proc, "perf.first_on_note") == 1.0f);
    CHECK(std::abs(plainOf(proc, "perf.pause_s") - 0.5f) < 1e-6f);
    CHECK(plainOf(proc, "out.room") == 1.0f);
    CHECK(std::abs(plainOf(proc, "trachea.length_mm") - 33.0f) < 1e-4f);
    CHECK_FALSE(pm.isModified());
    proc.setParamPlain("perf.snap", 0.5f);
    CHECK(pm.isModified());
}

TEST_CASE("presets: 'Finch, melody' is for tunes: follow the keyboard, every syllable on the note, one bird (WP-R5)") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    const auto* melody = pm.findFactory("Finch, melody");
    REQUIRE(melody != nullptr);
    REQUIRE(pm.factory().size() >= 5);
    CHECK(pm.factory()[4].name == "Finch, melody"); // with the other Finch presets, after "Finch, follow the keyboard"
    proc.setParamPlain("out.room", 0.0f);
    pm.load(*melody);
    CHECK(plainOf(proc, "perf.mapping") == 1.0f); // follow the keyboard
    CHECK(plainOf(proc, "perf.snap") == 1.0f);
    CHECK(plainOf(proc, "perf.polyphony") == 1.0f);
    CHECK(plainOf(proc, "perf.first_on_note") == 1.0f); // the rest stays at its default
    CHECK(plainOf(proc, "perf.note_rule") == 0.0f);
    CHECK(plainOf(proc, "out.room") == 1.0f);

    // a legato tune of held keys: every syllable lands on its key's note, and each key takes over from the last
    std::vector<cuicui::control::SyllableLogEntry> log(512);
    proc.setSyllableLog(log.data(), static_cast<int>(log.size()));
    proc.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256);
    const int keys[] = {60, 62, 64, 60, 64, 65, 67};
    const int step = 48000 / 2 / 256; // 0.5 s per note, in blocks
    for (int blk = 0; blk < step * 9; ++blk) {
        juce::MidiBuffer midi;
        for (int k = 0; k < 7; ++k) {
            if (blk == 1 + k * step) {
                midi.addEvent(juce::MidiMessage::noteOn(1, keys[k], static_cast<juce::uint8>(100)), 8);
            }
            if (blk == 1 + (k + 1) * step) {
                midi.addEvent(juce::MidiMessage::noteOff(1, keys[k]), 0); // released as the next key goes down
            }
        }
        proc.processBlock(buf, midi);
    }
    const int n = proc.syllableLogCount();
    const auto& bank = proc.syllableBank();
    REQUIRE(n > 14); // held keys sing more than one syllable each
    int lastKeyStart = -1;
    for (int i = 0; i < n; ++i) {
        const auto& r = log[static_cast<std::size_t>(i)].request;
        INFO("syllable " << i << " key " << r.note);
        CHECK(std::abs(bank.anchorCents(r.syllable, cuicui::control::NoteRule::Rule) + r.shiftCents - r.targetCents) < 1e-6);
        CHECK(std::abs(r.targetCents - (r.note + 36) * 100.0) < 1e-9); // three octaves above the key
        if (r.first) {
            CHECK(r.note != lastKeyStart);
            lastKeyStart = r.note;
        } else if (!r.releaseAlarm) {
            CHECK(r.note == lastKeyStart); // only the newest key sings on (one bird)
        }
    }
}

TEST_CASE("presets: choices by name and switches as true/false; bad files are refused") {
    SyrinxAudioProcessor proc;
    auto& pm = proc.presets();
    Preset p;
    juce::String error;
    REQUIRE(pm.fromJson(R"({"format":"cuicui.preset","version":1,"name":"t","parameters":{
        "perf.mapping":"Follow the keyboard","out.room":false,"perf.order":2,"perf.polyphony":3,
        "no.such.param":1}})",
                        p, error));
    pm.load(p);
    CHECK(plainOf(proc, "perf.mapping") == 1.0f);
    CHECK(plainOf(proc, "out.room") == 0.0f);
    CHECK(plainOf(proc, "perf.order") == 2.0f);
    CHECK(plainOf(proc, "perf.polyphony") == 3.0f);
    CHECK_FALSE(pm.fromJson(R"({"format":"cuicui.preset","version":2,"name":"t","parameters":{}})", p, error));
    CHECK_FALSE(pm.fromJson(R"({"format":"other","version":1,"name":"t","parameters":{}})", p, error));
    CHECK_FALSE(pm.fromJson(R"({"format":"cuicui.preset","version":1,"name":"t","parameters":{"perf.mapping":"Sideways"}})",
                            p, error));
    CHECK_FALSE(pm.fromJson("not json", p, error));
}

TEST_CASE("presets: user presets save, list, browse and survive the plugin state") {
    TempFolder tmp;
    juce::MemoryBlock state;
    {
        SyrinxAudioProcessor proc;
        auto& pm = proc.presets();
        pm.setUserFolder(tmp.dir);
        CHECK(pm.user().empty());
        proc.setParamPlain("perf.snap", 0.4f);
        proc.setParamPlain("perf.size_st", 3.0f);
        juce::String error;
        REQUIRE(pm.saveUser("My finch", "test", false, error));
        CHECK(pm.userFileFor("My finch").existsAsFile());
        CHECK_FALSE(pm.saveUser("My finch", "test", false, error)); // exists: needs overwrite
        CHECK(error.contains("already exists"));
        REQUIRE(pm.user().size() == 1);
        CHECK(pm.currentName() == "My finch");
        CHECK(pm.currentIsUser());
        CHECK_FALSE(pm.isModified());
        const int n = pm.size();
        CHECK(pm.indexOfCurrent() == n - 1);
        pm.step(+1); // wraps to the first factory preset
        CHECK(pm.indexOfCurrent() == 0);
        pm.step(-1); // back to the user preset
        CHECK(pm.currentName() == "My finch");
        CHECK(std::abs(plainOf(proc, "perf.snap") - 0.4f) < 1e-6f);
        CHECK(std::abs(plainOf(proc, "perf.size_st") - 3.0f) < 1e-6f);
        proc.getStateInformation(state);
    }
    SyrinxAudioProcessor proc2;
    proc2.presets().setUserFolder(tmp.dir);
    proc2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(proc2.presets().currentName() == "My finch");
    CHECK(proc2.presets().currentIsUser());
    CHECK_FALSE(proc2.presets().isModified());
    CHECK(std::abs(plainOf(proc2, "perf.snap") - 0.4f) < 1e-6f);
}

TEST_CASE("feed: every syllable start reaches the editor feed, with its key and target") {
    SyrinxAudioProcessor p;
    p.setParamPlain("engine.seed", 5.0f);
    std::vector<cuicui::control::SyllableLogEntry> log(512);
    p.setSyllableLog(log.data(), static_cast<int>(log.size()));
    p.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256);
    std::vector<SyllableFeed::Event> got;
    SyllableFeed::Event tmp[64];
    for (int b = 0; b < 48000 * 3 / 256; ++b) {
        juce::MidiBuffer midi;
        if (b == 2) {
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 17);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90)), 100);
        }
        if (b == 48000 * 2 / 256) {
            midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        }
        p.processBlock(buf, midi);
        for (int k; (k = p.feed().pop(tmp, 64)) > 0;) {
            got.insert(got.end(), tmp, tmp + k);
        }
    }
    const int n = p.syllableLogCount();
    REQUIRE(n > 4);
    REQUIRE(static_cast<int>(got.size()) == n);
    for (int i = 0; i < n; ++i) {
        const auto& e = log[static_cast<std::size_t>(i)];
        CHECK(got[static_cast<std::size_t>(i)].sample == e.sample);
        CHECK(got[static_cast<std::size_t>(i)].syllable == e.request.syllable);
        CHECK((got[static_cast<std::size_t>(i)].note == 69 || got[static_cast<std::size_t>(i)].note == 64));
        CHECK(e.request.note == got[static_cast<std::size_t>(i)].note);
    }
    CHECK(got[0].sample == 2 * 256 + 17);
    CHECK(got[0].first);
    CHECK(p.feed().clock() == 48000 * 3 / 256 * 256);
    CHECK(p.feed().externalNotes() == 2u);
    CHECK(p.feed().dropped() == 0);
}

TEST_CASE("feed: a full feed drops new starts and never blocks the audio thread") {
    SyllableFeed f;
    cuicui::control::SyllableLogEntry e{};
    for (int i = 0; i < SyllableFeed::kCapacity + 100; ++i) {
        e.sample = i;
        f.push(e);
    }
    CHECK(f.dropped() == 101); // AbstractFifo keeps capacity − 1 items
    SyllableFeed::Event out[SyllableFeed::kCapacity];
    CHECK(f.pop(out, SyllableFeed::kCapacity) == SyllableFeed::kCapacity - 1);
    CHECK(out[0].sample == 0);
}
