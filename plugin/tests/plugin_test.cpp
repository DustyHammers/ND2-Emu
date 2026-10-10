// The plugin end to end (the processor and editor sources with JUCE, without a plugin wrapper).
//
//   plugin_test                 always: construction, bus layouts, silence without the OS, state with no machine.
//                               With the OS in ~/Documents/ND2/os/ (or ND2_OS_SOURCE=any file holding it, which is
//                               then located as the plugin does): boot, MIDI through the main and channel outputs
//                               (they add up), a piezo hit on Trigger In, the state round trip; with ND2_BANKS=bank
//                               files, the factory memory is built first.
//   plugin_test --editor DIR    also opens the editor (needs a display) and, with DIR, saves screenshots there
//                               (ImageMagick `import`, X11 only): the panel, after clicking PROGRAM, the menu.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Resources.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
template <class F> static bool pumpUntil(F done, int maxMs) {
    for (int t = 0; t < maxMs && !done(); t += 20) pump(20);
    return done();
}

static juce::AudioProcessor::BusesLayout fullLayout() {
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::discreteChannels(6));
    for (int b = 0; b < 7; ++b) l.outputBuses.add(juce::AudioChannelSet::stereo());
    return l;
}

struct Run { std::vector<float> main, stems[6]; double maxErr = 0; };

// Plays `seconds`, with a note (pad 1) at `noteAt` and a piezo hit on Trigger In 1 at `hitAt` (negative: none).
static Run play(ND2Processor& p, double seconds, double noteAt, double hitAt) {
    Run r;
    const int block = 256, rate = 48000;
    juce::AudioBuffer<float> buf(14, block);
    for (int pos = 0; pos < int(seconds * rate); pos += block) {
        buf.clear();
        juce::MidiBuffer midi;
        const int noteSample = int(noteAt * rate);
        if (noteAt >= 0 && noteSample >= pos && noteSample < pos + block) midi.addEvent(juce::MidiMessage::noteOn(10, 60, uint8_t(110)), noteSample - pos);
        for (int i = 0; i < block && hitAt >= 0; ++i) {            // input channel 0 = Trigger In 1 (in place)
            const double t = double(pos + i) / rate - hitAt;
            buf.setSample(0, i, t < 0 ? 0.0f : float(0.7 * std::sin(2 * M_PI * 400 * t) * std::exp(-t / 0.004)));
        }
        p.processBlock(buf, midi);
        for (int i = 0; i < block; ++i) {
            const float m = buf.getSample(0, i);
            r.main.push_back(m);
            float sum = 0;
            for (int c = 0; c < 6; ++c) { const float s = buf.getSample(2 + 2 * c, i); r.stems[c].push_back(s); sum += s; }
            r.maxErr = std::max(r.maxErr, double(std::fabs(sum - m)));
        }
    }
    return r;
}
static double energy(const std::vector<float>& v, size_t from = 0) { double e = 0; for (size_t i = from; i < v.size(); ++i) e += double(v[i]) * v[i]; return e; }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const bool editor = argc > 1 && juce::String(argv[1]) == "--editor";
    const juce::String shots = argc > 2 ? juce::String(argv[2]) : juce::String();
    Resources& res = Resources::get();
    if (!res.hasOs()) if (const char* src = std::getenv("ND2_OS_SOURCE")) {
        res.locateOs(juce::File(src), [](bool ok, juce::String m) { std::printf("locate: %s %s\n", ok ? "ok" : "failed", m.toRawUTF8()); });
        pumpUntil([&] { return !res.locating(); }, 120000);
    }
    std::printf("OS: %s\n", res.hasOs() ? "found" : "not found (OS-dependent tests skipped)");
    if (res.hasOs() && !res.hasFactory()) if (const char* banks = std::getenv("ND2_BANKS")) {
        res.buildFactory({juce::File(banks)}, [](bool ok, juce::String m) { std::printf("factory: %s %s\n", ok ? "ok" : "failed", m.toRawUTF8()); });
        pumpUntil([&] { return !res.building(); }, 120000);
        CHECK(res.hasFactory(), "factory memory");
    }

    {
        ND2Processor p;
        CHECK(p.setBusesLayout(fullLayout()), "the full layout (6 trigger inputs, main + 6 channel outputs) is accepted");
        juce::AudioProcessor::BusesLayout bad = fullLayout();
        bad.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
        CHECK(!p.checkBusesLayoutSupported(bad), "a mono main output is refused");
        p.prepareToPlay(48000, 256);
        std::printf("latency %d samples\n", p.getLatencySamples());
        if (!res.hasOs()) {
            const Run r = play(p, 0.2, -1, -1);
            CHECK(energy(r.main) == 0, "silent without the OS");
            juce::MemoryBlock st;
            p.getStateInformation(st);
            ND2Processor q;
            q.setStateInformation(st.getData(), int(st.getSize()));
        } else {
            CHECK(pumpUntil([&] { return p.engine().state() == nd2::Engine::State::Running; }, 15000), "the machine runs");
            if (res.hasFactory()) {          // started with the factory memory: bank 1 program 1 (P1. 1)
                pumpUntil([&] { return !p.bootedWithoutFactory(); }, 3000);
            }
            play(p, 0.5, -1, -1);
            const Run r = play(p, 1.5, 0.2, 0.9);
            std::printf("main energy %.3g; channel outputs: ", energy(r.main));
            for (int c = 0; c < 6; ++c) std::printf("%.3g ", energy(r.stems[c]));
            std::printf("; sum vs main %.2e\n", r.maxErr);
            CHECK(energy(r.main) > 1.0, "the note plays");
            CHECK(r.maxErr < 1e-5, "the channel outputs add up to the main output (%.2e)", r.maxErr);
            CHECK(energy(r.stems[0]) > 0.5 && energy(r.stems[1]) == 0 && energy(r.stems[5]) == 0, "pad 1 only on Ch 1");
            CHECK(energy(r.stems[0], size_t(0.95 * 48000)) > 0.1, "the piezo hit on Trigger In 1 plays pad 1");
            // state round trip: a second instance restores the machine and plays
            juce::MemoryBlock st;
            p.getStateInformation(st);
            std::printf("state %zu bytes\n", st.getSize());
            ND2Processor q;
            q.setBusesLayout(fullLayout());
            q.prepareToPlay(48000, 256);
            q.setStateInformation(st.getData(), int(st.getSize()));
            CHECK(pumpUntil([&] { return q.engine().state() == nd2::Engine::State::Running; }, 15000), "restored machine runs");
            play(q, 0.3, -1, -1);
            const Run r2 = play(q, 1.0, 0.2, -1);
            CHECK(energy(r2.main) > 1.0, "the restored machine plays");
        }

        if (editor) {
            // The host's audio thread: the machine runs (and takes panel input) only while audio is processed.
            std::atomic<bool> audioOn{true};
            std::thread audio([&] {
                juce::AudioBuffer<float> buf(14, 256);
                juce::MidiBuffer midi;
                auto next = std::chrono::steady_clock::now();
                while (audioOn) {
                    buf.clear(); midi.clear();
                    p.processBlock(buf, midi);
                    next += std::chrono::microseconds(5333);
                    std::this_thread::sleep_until(next);
                }
            });
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            juce::DocumentWindow w("ND2", juce::Colours::black, 0, true);
            w.setUsingNativeTitleBar(false);
            w.setTitleBarHeight(0);
            w.setContentNonOwned(ed.get(), true);
            w.setTopLeftPosition(0, 0);
            w.setVisible(true);
            pump(4000);
            std::printf("editor %d x %d\n", ed->getWidth(), ed->getHeight());
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display == nullptr || (display->userArea.getWidth() >= 1294 && display->userArea.getHeight() >= 806))
                CHECK(ed->getWidth() == 1294 && ed->getHeight() == 806, "100 %% size");
            else                                         // a small screen: the window is never larger than it
                CHECK(ed->getWidth() <= display->userArea.getWidth() && ed->getHeight() <= display->userArea.getHeight(),
                      "the window fits the screen");
            auto shot = [&](const char* name) {
                if (shots.isEmpty()) return;
                const juce::String cmd = "import -window root " + shots + "/" + name + ".png";
                std::system(cmd.toRawUTF8());
            };
            shot("1_panel");
            if (auto* peer = ed->getPeer()) {
                const float k = float(ed->getWidth()) / 1725.0f;       // dp -> points
                auto click = [&](float xdp, float ydp, bool right) {
                    const juce::Point<float> pt(xdp * k, ydp * k);
                    const auto t = juce::Time::currentTimeMillis();
                    const auto mods = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
                    const auto src = juce::MouseInputSource::InputSourceType::mouse;
                    peer->handleMouseEvent(src, pt, {}, juce::MouseInputSource::defaultPressure, 0, t);
                    peer->handleMouseEvent(src, pt, mods, juce::MouseInputSource::defaultPressure, 0, t + 1);
                    if (!right) {
                        pump(120);
                        peer->handleMouseEvent(src, pt, {}, juce::MouseInputSource::defaultPressure, 0, t + 130);
                    }
                };
                const auto seg0 = p.engine().panel().seg;
                click(418, 612, false);                                 // parameter button 1 (FILTER) at 357..479, 583..642 dp
                pump(1500);
                const auto seg1 = p.engine().panel().seg;
                std::printf("display before %02x %02x %02x %02x, after FILTER %02x %02x %02x %02x; FILTER LED level %d\n", seg0[0], seg0[1],
                            seg0[2], seg0[3], seg1[0], seg1[1], seg1[2], seg1[3], p.engine().ledLevel(21));
                if (res.hasOs()) CHECK(seg1 != seg0 && p.engine().ledLevel(21) > 0, "a click on FILTER reaches the OS (display and LED)");
                shot("2_after_filter");
                {                                                       // drag the value encoder up 80 dp (6 detents)
                    const auto src = juce::MouseInputSource::InputSourceType::mouse;
                    const auto L = juce::ModifierKeys::leftButtonModifier;
                    auto t = juce::Time::currentTimeMillis();
                    peer->handleMouseEvent(src, {901 * k, 861 * k}, {}, juce::MouseInputSource::defaultPressure, 0, t);
                    peer->handleMouseEvent(src, {901 * k, 861 * k}, L, juce::MouseInputSource::defaultPressure, 0, ++t);
                    for (int dy = 4; dy <= 80; dy += 4) {
                        pump(30);
                        peer->handleMouseEvent(src, {901 * k, (861 - float(dy)) * k}, L, juce::MouseInputSource::defaultPressure, 0, t += 30);
                    }
                    pump(30);
                    peer->handleMouseEvent(src, {901 * k, 781 * k}, {}, juce::MouseInputSource::defaultPressure, 0, t += 30);
                }
                pump(1200);
                const auto seg2 = p.engine().panel().seg;
                if (res.hasOs()) CHECK(seg2 != seg1, "dragging the encoder changes the value");
                shot("3_after_encoder");
                click(900, 560, true);                                  // the menu
                pump(1200);
                shot("4_menu");
            }
            w.setVisible(false);
            w.clearContentComponent();
            ed.reset();
            pump(500);
            audioOn = false;
            audio.join();
        }
    }
    std::printf(fails ? "%d failure(s)\n" : "plugin tests passed\n", fails);
    return fails ? 1 : 0;
}
