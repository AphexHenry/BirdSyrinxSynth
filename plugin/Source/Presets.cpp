#include "Presets.h"

#include "BinaryData.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char* kPresetSuffix = ".preset.json";

juce::String toJuce(const std::string& s) {
    return juce::String::fromUTF8(s.c_str(), static_cast<int>(s.size()));
}

std::string toStd(const juce::String& s) {
    return s.toStdString();
}

} // namespace

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {
    for (auto* p : apvts_.processor.getParameters()) {
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) {
            params_.push_back(r);
        }
    }
    userFolder_ = defaultUserFolder();
    loadFactory();
    if (const auto* def = findFactory(kDefaultName)) {
        reference_ = targetsOf(*def);
        needReference_ = false;
    }
}

juce::File PresetManager::defaultUserFolder() {
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Cuicui Instruments").getChildFile("Syrinx").getChildFile("Presets");
}

void PresetManager::setUserFolder(const juce::File& folder) {
    userFolder_ = folder;
    rescanUser();
}

void PresetManager::loadFactory() {
    struct Item {
        juce::String file;
        Preset preset;
    };
    std::vector<Item> items;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const juce::String original{BinaryData::originalFilenames[i]};
        if (!original.endsWith(kPresetSuffix)) {
            continue;
        }
        int bytes = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], bytes);
        if (data == nullptr) {
            continue;
        }
        Preset p;
        juce::String error;
        if (fromJson(std::string(data, static_cast<std::size_t>(bytes)), p, error)) {
            p.factory = true;
            items.push_back({original, std::move(p)});
        } else {
            juce::Logger::writeToLog("Syrinx: factory preset " + original + " did not load: " + error);
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.file < b.file; });
    factory_.clear();
    for (auto& it : items) {
        factory_.push_back(std::move(it.preset));
    }
}

void PresetManager::rescanUser() {
    user_.clear();
    if (!userFolder_.isDirectory()) {
        return;
    }
    for (const auto& f : userFolder_.findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetSuffix)) {
        Preset p;
        juce::String error;
        if (fromJson(toStd(f.loadFileAsString()), p, error)) {
            p.factory = false;
            p.file = f;
            user_.push_back(std::move(p));
        }
    }
    std::sort(user_.begin(), user_.end(),
              [](const Preset& a, const Preset& b) { return a.name.compareNatural(b.name) < 0; });
}

const Preset* PresetManager::at(int index) const noexcept {
    const int nf = static_cast<int>(factory_.size());
    if (index < 0 || index >= size()) {
        return nullptr;
    }
    return index < nf ? &factory_[static_cast<std::size_t>(index)] : &user_[static_cast<std::size_t>(index - nf)];
}

const Preset* PresetManager::findFactory(const juce::String& name) const noexcept {
    for (const auto& p : factory_) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

int PresetManager::indexOfCurrent() const {
    const auto name = currentName();
    const bool user = currentIsUser();
    for (int i = 0; i < size(); ++i) {
        const auto* p = at(i);
        if (p->name == name && p->factory != user) {
            return i;
        }
    }
    return -1;
}

juce::RangedAudioParameter* PresetManager::param(const juce::String& id) const noexcept {
    for (auto* p : params_) {
        if (p->getParameterID() == id) {
            return p;
        }
    }
    return nullptr;
}

std::vector<float> PresetManager::targetsOf(const Preset& preset) const {
    std::vector<float> t;
    t.reserve(params_.size());
    for (auto* p : params_) {
        float v = p->getDefaultValue();
        for (const auto& [id, plain] : preset.values) {
            if (id == p->getParameterID()) {
                v = p->convertTo0to1(plain);
                break;
            }
        }
        t.push_back(v);
    }
    return t;
}

void PresetManager::load(const Preset& preset) {
    const auto targets = targetsOf(preset);
    for (std::size_t i = 0; i < params_.size(); ++i) {
        auto* p = params_[i];
        if (std::abs(p->getValue() - targets[i]) > 1e-9f) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(targets[i]);
            p->endChangeGesture();
        }
    }
    // Reference = what the parameters hold now (setValue may round, e.g. choices and integers).
    reference_.clear();
    for (auto* p : params_) {
        reference_.push_back(p->getValue());
    }
    setCurrent(preset.name, !preset.factory);
    const juce::ScopedLock sl(lock_);
    needReference_ = false;
}

bool PresetManager::loadIndex(int index) {
    if (const auto* p = at(index)) {
        load(*p);
        return true;
    }
    return false;
}

void PresetManager::step(int delta) {
    const int n = size();
    if (n == 0) {
        return;
    }
    int i = indexOfCurrent();
    i = i < 0 ? (delta >= 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    loadIndex(i);
}

Preset PresetManager::capture(const juce::String& name, const juce::String& category,
                              const juce::String& description) const {
    Preset p;
    p.name = name;
    p.category = category;
    p.author = "";
    p.description = description;
    p.voiceBank = bankName_;
    for (auto* r : params_) {
        p.values.emplace_back(r->getParameterID(), r->convertFrom0to1(r->getValue()));
    }
    return p;
}

juce::File PresetManager::userFileFor(const juce::String& name) const {
    return userFolder_.getChildFile(juce::File::createLegalFileName(name.trim()) + kPresetSuffix);
}

bool PresetManager::saveUser(const juce::String& name, const juce::String& description, bool overwrite,
                             juce::String& error) {
    const auto clean = name.trim();
    if (clean.isEmpty()) {
        error = "Please give the preset a name.";
        return false;
    }
    const auto file = userFileFor(clean);
    if (file.existsAsFile() && !overwrite) {
        error = "A preset called \"" + clean + "\" already exists.";
        return false;
    }
    if (!userFolder_.isDirectory() && !userFolder_.createDirectory()) {
        error = "Cannot create the presets folder " + userFolder_.getFullPathName();
        return false;
    }
    auto preset = capture(clean, "User", description);
    if (!file.replaceWithText(toJuce(toJson(preset)))) {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }
    rescanUser();
    reference_.clear();
    for (auto* p : params_) {
        reference_.push_back(p->getValue());
    }
    setCurrent(clean, true);
    const juce::ScopedLock sl(lock_);
    needReference_ = false;
    return true;
}

std::string PresetManager::toJson(const Preset& preset) const {
    nlohmann::ordered_json j;
    j["format"] = kFormat;
    j["version"] = kVersion;
    j["name"] = toStd(preset.name);
    j["category"] = toStd(preset.category);
    j["author"] = toStd(preset.author);
    j["description"] = toStd(preset.description);
    j["voice_bank"] = toStd(preset.voiceBank);
    auto values = nlohmann::ordered_json::object();
    for (const auto& [id, plain] : preset.values) {
        auto* p = param(id);
        if (p == nullptr) {
            continue;
        }
        const auto key = toStd(id);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p)) {
            const int idx = std::clamp(static_cast<int>(std::lround(plain)), 0, c->choices.size() - 1);
            values[key] = toStd(c->choices[idx]);
        } else if (dynamic_cast<juce::AudioParameterBool*>(p) != nullptr) {
            values[key] = plain >= 0.5f;
        } else if (dynamic_cast<juce::AudioParameterInt*>(p) != nullptr) {
            values[key] = static_cast<long long>(std::llround(plain));
        } else {
            values[key] = static_cast<double>(plain);
        }
    }
    j["parameters"] = std::move(values);
    return j.dump(2) + "\n";
}

bool PresetManager::fromJson(const std::string& text, Preset& out, juce::String& error, int* ignored) const {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        error = juce::String("not valid JSON: ") + e.what();
        return false;
    }
    if (!j.is_object() || j.value("format", "") != std::string(kFormat)) {
        error = "not a cuicui.preset file";
        return false;
    }
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() != kVersion) {
        error = "unsupported preset version";
        return false;
    }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        error = "the preset has no name";
        return false;
    }
    if (!j.contains("parameters") || !j["parameters"].is_object()) {
        error = "the preset has no parameters object";
        return false;
    }
    Preset p;
    p.name = toJuce(j["name"].get<std::string>());
    p.category = toJuce(j.value("category", ""));
    p.author = toJuce(j.value("author", ""));
    p.description = toJuce(j.value("description", ""));
    p.voiceBank = toJuce(j.value("voice_bank", ""));
    int unknown = 0;
    for (auto it = j["parameters"].begin(); it != j["parameters"].end(); ++it) {
        const auto id = toJuce(it.key());
        auto* prm = param(id);
        if (prm == nullptr) {
            ++unknown;
            continue;
        }
        const auto& v = it.value();
        float plain = 0.0f;
        if (v.is_boolean()) {
            plain = v.get<bool>() ? 1.0f : 0.0f;
        } else if (v.is_number()) {
            plain = v.get<float>();
        } else if (v.is_string()) {
            auto* c = dynamic_cast<juce::AudioParameterChoice*>(prm);
            const int idx = c != nullptr ? c->choices.indexOf(toJuce(v.get<std::string>()), true) : -1;
            if (idx < 0) {
                error = "\"" + id + "\": unknown value \"" + toJuce(v.get<std::string>()) + "\"";
                return false;
            }
            plain = static_cast<float>(idx);
        } else {
            error = "\"" + id + "\": the value must be a number, true/false or a choice name";
            return false;
        }
        if (!std::isfinite(plain)) {
            error = "\"" + id + "\": the value is not finite";
            return false;
        }
        p.values.emplace_back(id, plain);
    }
    if (ignored != nullptr) {
        *ignored = unknown;
    }
    out = std::move(p);
    return true;
}

juce::String PresetManager::currentName() const {
    const juce::ScopedLock sl(lock_);
    return currentName_;
}

bool PresetManager::currentIsUser() const {
    const juce::ScopedLock sl(lock_);
    return currentUser_;
}

void PresetManager::setCurrent(const juce::String& name, bool user) {
    const juce::ScopedLock sl(lock_);
    currentName_ = name;
    currentUser_ = user;
}

bool PresetManager::isModified() {
    bool rebuild = false;
    {
        const juce::ScopedLock sl(lock_);
        rebuild = needReference_;
        needReference_ = false;
    }
    if (rebuild) { // after a state restore: compare against the named preset, if it still exists
        reference_.clear();
        const int i = indexOfCurrent();
        if (i < 0 && currentIsUser()) {
            rescanUser();
        }
        if (const auto* p = at(indexOfCurrent())) {
            reference_ = targetsOf(*p);
        }
    }
    if (reference_.size() != params_.size()) {
        return false; // no reference (an unknown or deleted preset): nothing to compare
    }
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (std::abs(params_[i]->getValue() - reference_[i]) > 1e-6f) {
            return true;
        }
    }
    return false;
}

void PresetManager::writeState(juce::XmlElement& xml) const {
    const juce::ScopedLock sl(lock_);
    xml.setAttribute("presetName", currentName_);
    xml.setAttribute("presetUser", currentUser_ ? 1 : 0);
}

void PresetManager::readState(const juce::XmlElement& xml) {
    const juce::ScopedLock sl(lock_);
    if (xml.hasAttribute("presetName")) {
        currentName_ = xml.getStringAttribute("presetName");
        currentUser_ = xml.getIntAttribute("presetUser", 0) != 0;
    } else { // a session saved before WP-R4: no preset name
        currentName_ = "Untitled";
        currentUser_ = false;
    }
    needReference_ = true;
}
