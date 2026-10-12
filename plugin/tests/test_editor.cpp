// WP-R4: the editor — help texts, the look reaching every control, pages, and the audio thread while the
// editor is open (criteria 4, 5, 6).
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ParamHelp.h"
#include "alloc_count.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

namespace {

void forEachChild(juce::Component& c, const std::function<void(juce::Component&)>& f) {
    f(c);
    for (auto* k : c.getChildren()) {
        forEachChild(*k, f);
    }
}

std::unique_ptr<SyrinxAudioProcessorEditor> openEditor(SyrinxAudioProcessor& proc) {
    return std::unique_ptr<SyrinxAudioProcessorEditor>(dynamic_cast<SyrinxAudioProcessorEditor*>(proc.createEditor()));
}

} // namespace

TEST_CASE("editor: every parameter has a help sentence (criterion 6)") {
    for (const auto& spec : cuicui::kParamTable) {
        INFO(spec.id);
        CHECK(syrinx::paramHelp(spec.id).size() > 10);
    }
}

TEST_CASE("editor: every control on both pages carries help text") {
    SyrinxAudioProcessor proc;
    auto ed = openEditor(proc);
    REQUIRE(ed != nullptr);
    int controls = 0;
    int missing = 0;
    forEachChild(*ed, [&](juce::Component& c) {
        const bool control = dynamic_cast<juce::Slider*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr ||
                             dynamic_cast<juce::Button*>(&c) != nullptr ||
                             dynamic_cast<syrinx::ui::Segmented*>(&c) != nullptr ||
                             dynamic_cast<syrinx::ui::Switch*>(&c) != nullptr;
        if (!control) {
            return;
        }
        ++controls;
        bool found = false;
        for (auto* k = &c; k != nullptr && k != ed.get(); k = k->getParentComponent()) {
            found = found || k->getHelpText().isNotEmpty();
        }
        if (!found) {
            ++missing;
            UNSCOPED_INFO("no help: " << c.getName() << " / " << c.getTitle() << " / " << typeid(c).name());
        }
    });
    CHECK(controls > 60);
    CHECK(missing == 0);
}

TEST_CASE("editor: every control takes the Syrinx look, even text boxes made before the editor existed") {
    SyrinxAudioProcessor proc;
    auto ed = openEditor(proc);
    int boxes = 0;
    forEachChild(*ed, [&](juce::Component& c) {
        if (auto* l = dynamic_cast<juce::Label*>(&c); l != nullptr && dynamic_cast<juce::Slider*>(l->getParentComponent())) {
            ++boxes;
            CHECK(l->findColour(juce::Label::outlineColourId) == syrinx::ui::colours::insetBorder);
            CHECK(&l->getLookAndFeel() == &ed->getLookAndFeel());
        }
    });
    CHECK(boxes > 20);
}

TEST_CASE("editor: the page is remembered in the plugin state") {
    juce::MemoryBlock state;
    {
        SyrinxAudioProcessor proc;
        auto ed = openEditor(proc);
        ed->showPage(1);
        ed.reset();
        proc.getStateInformation(state);
    }
    SyrinxAudioProcessor proc2;
    proc2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(proc2.uiPage.load() == 1);
}

TEST_CASE("editor: open, play, switch presets and pages, close — the audio thread never allocates (criterion 4)") {
    // One thread plays both roles in turn: the audio callback (processBlock, counted) and the message
    // thread (editor timer, preset loads, parameter changes, page switches, editor open/close; not counted).
    SyrinxAudioProcessor p;
    p.setParamPlain("perf.polyphony", 6.0f);
    p.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    auto ed = openEditor(p);
    juce::Random rnd(3);
    int held[6] = {-1, -1, -1, -1, -1, -1};
    p.processBlock(buf, midi); // warm-up
    int allocs = 0;
    double worst = 0.0;
    const double blockS = 256.0 / 48000.0;
    auto& pm = p.presets();
    const int blocks = 60 * 48000 / 256;
    for (int blk = 0; blk < blocks; ++blk) {
        midi.clear();
        if (rnd.nextFloat() < 0.1f) {
            const int k = rnd.nextInt(6);
            if (held[k] >= 0) {
                midi.addEvent(juce::MidiMessage::noteOff(1, held[k]), rnd.nextInt(256));
                held[k] = -1;
            } else {
                held[k] = 48 + rnd.nextInt(40);
                midi.addEvent(juce::MidiMessage::noteOn(1, held[k], static_cast<juce::uint8>(1 + rnd.nextInt(127))),
                              rnd.nextInt(256));
            }
        }
        const int before = cuicui::test::alloc_count();
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock(buf, midi);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        allocs += cuicui::test::alloc_count() - before;
        worst = std::max(worst, dt);

        // the message thread's turn
        if (blk % 2 == 0) {
            ed->tick();
        }
        if (blk % 37 == 0) { // rapid preset switching
            pm.loadIndex(rnd.nextInt(pm.size()));
        }
        if (blk % 11 == 0) { // knob automation
            p.setParamPlain("perf.snap", rnd.nextFloat());
            p.setParamPlain("perf.size_st", static_cast<float>(rnd.nextInt(25) - 12));
            p.setParamPlain("out.stereo_spread", rnd.nextFloat());
        }
        if (blk % 997 == 0) {
            ed->showPage(rnd.nextInt(2));
        }
        if (blk % 2501 == 0) { // close and reopen the window
            ed.reset();
            ed = openEditor(p);
        }
    }
    INFO("worst block " << worst * 1000.0 << " ms of " << blockS * 1000.0 << " ms");
    std::printf("stress: %d blocks, %d allocations in processBlock, worst block %.3f ms of %.3f ms (%.1f %%)\n",
                blocks, allocs, worst * 1000.0, blockS * 1000.0, 100.0 * worst / blockS);
    CHECK(allocs == 0);
    CHECK(worst < 0.5 * blockS);
    CHECK(p.feed().dropped() == 0);
}

// Hidden (run by hand: syrinx_plugin_tests "[.ui-cpu]"): the editor's CPU on the message thread, with a real
// window on screen, at rest and while four birds sing (an audio thread renders in real time). Criterion 5.
#include <atomic>
#include <ctime>
#include <thread>

namespace {
double threadCpuSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + 1e-9 * static_cast<double>(ts.tv_nsec);
}
} // namespace

TEST_CASE("editor: message-thread CPU, window on screen (report)", "[.ui-cpu]") {
    SyrinxAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    auto ed = openEditor(p);
    ed->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
    ed->setTopLeftPosition(80, 80);
    ed->setVisible(true);
    std::atomic<bool> run{true};
    std::atomic<bool> play{false};
    std::thread audio([&] { // a real-time "audio callback": one 256-sample block every 5.33 ms
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        bool held = false;
        auto next = std::chrono::steady_clock::now();
        while (run.load()) {
            midi.clear();
            if (play.load() != held) {
                held = !held;
                for (const int n : {64, 69, 72, 76}) {
                    midi.addEvent(held ? juce::MidiMessage::noteOn(1, n, static_cast<juce::uint8>(100))
                                       : juce::MidiMessage::noteOff(1, n),
                                  0);
                }
            }
            p.processBlock(buf, midi);
            next += std::chrono::microseconds(5333);
            std::this_thread::sleep_until(next);
        }
    });
    auto measure = [&](const char* what, bool singing) {
        play.store(singing);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1500); // settle
        const double c0 = threadCpuSeconds();
        const auto w0 = std::chrono::steady_clock::now();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(8000);
        const double cpu = threadCpuSeconds() - c0;
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
        std::printf("UI CPU %-28s %.2f %% of one core (message thread, %.1f s)\n", what, 100.0 * cpu / wall, wall);
        return 100.0 * cpu / wall;
    };
    const double idle = measure("at rest", false);
    const double singing = measure("four birds singing", true);
    run.store(false);
    audio.join();
    ed->removeFromDesktop();
    CHECK(idle <= 5.0);
    CHECK(singing > 0.0);
}

// Hidden (by hand: syrinx_plugin_tests "[.shots]"): the save dialog drawn offscreen, for the report.
TEST_CASE("editor: save dialog picture", "[.shots]") {
    SyrinxAudioProcessor proc;
    auto ed = openEditor(proc);
    juce::AlertWindow w("Save preset", "Save the current sound as your own preset.", juce::MessageBoxIconType::NoIcon);
    w.setLookAndFeel(&ed->getLookAndFeel());
    w.addTextEditor("name", "Evening finch", "Name");
    w.addButton("Save", 1);
    w.addButton("Cancel", 0);
    ed->addAndMakeVisible(w);
    w.setCentrePosition(ed->getLocalBounds().getCentre());
    juce::Image img(juce::Image::RGB, ed->getWidth() * 2, ed->getHeight() * 2, true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        ed->paintEntireComponent(g, true);
    }
    juce::File f(juce::String(CUICUI_BINARY_DIR) + "/../shots/save_dialog.png");
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat().writeImageToStream(img, os);
    ed->removeChildComponent(&w);
    w.setLookAndFeel(nullptr);
    CHECK(f.existsAsFile());
}
