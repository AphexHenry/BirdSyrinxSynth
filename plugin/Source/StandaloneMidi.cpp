#include "StandaloneMidi.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if defined(JUCE_SHARED_CODE) && defined(JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#define SYRINX_HAS_STANDALONE_HOLDER 1
#endif

namespace syrinx {

int enableMidiInputsInStandalone() {
#ifdef SYRINX_HAS_STANDALONE_HOLDER
    if (!juce::JUCEApplicationBase::isStandaloneApp()) {
        return 0;
    }
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr) {
        return 0;
    }
    auto& dm = holder->deviceManager;
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& d : devices) {
        if (dm.isMidiInputDeviceEnabled(d.identifier)) {
            return 0; // the user already chose their inputs (Options > Audio/MIDI Settings)
        }
    }
    int n = 0;
    for (const auto& d : devices) {
        dm.setMidiInputDeviceEnabled(d.identifier, true);
        ++n;
    }
    return n;
#else
    return 0;
#endif
}

} // namespace syrinx
