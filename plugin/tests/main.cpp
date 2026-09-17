#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

#include <cstdlib>

// Catch2 + JUCE: ScopedJuceInitialiser_GUI::~ calls DeletedAtShutdown::deleteAll(),
// which aborts in malloc when this binary also replaces global operator new/delete
// (alloc_counter.hpp). Tests have already finished; skip static destruction.
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    const int result = Catch::Session().run(argc, argv);
    std::_Exit(result);
}
