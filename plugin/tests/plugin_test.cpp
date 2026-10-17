// The plugin end to end (the processor and editor sources with JUCE, without a plugin wrapper).
//
//   plugin_test                 always: construction, bus layouts, silence without the OS, state with no machine.
//                               With the OS in ~/Documents/ND2/os/ (or ND2_OS_SOURCE=any file holding it, which is
//                               then located as the plugin does): boot, MIDI through the main and channel outputs
//                               (they add up), a piezo hit on Trigger In (and its note on the MIDI out to the host),
//                               the state round trip; with ND2_BANKS=bank files, the factory memory is built first.
//                               With the factory memory: the Banks window's work (save bank 1 as a file, load it
//                               into bank 6, check the banks, the names in the project state).
//   plugin_test --editor DIR    also opens the editor (needs a display) and, with DIR, saves screenshots there
//                               (ImageMagick `import`, X11 only): the panel, after clicking FILTER, the ND2 window
//                               (opened with a right-click) on each tab, a bank file dragged onto bank 5.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include "BanksPanel.h"
#include "ND2Window.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BankFiles.h"
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

struct Run { std::vector<float> main, stems[6]; double maxErr = 0; int notesOut = 0; };

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
        for (const auto meta : midi) if (meta.getMessage().isNoteOn()) ++r.notesOut;   // the OS's MIDI out
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
        juce::AudioProcessor::BusesLayout stereoIn = fullLayout();
        stereoIn.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        CHECK(!p.checkBusesLayoutSupported(stereoIn), "a stereo Trigger In is refused (all 6 pads or none)");
        juce::AudioProcessor::BusesLayout noIn = fullLayout();
        noIn.inputBuses.getReference(0) = juce::AudioChannelSet::disabled();
        CHECK(p.checkBusesLayoutSupported(noIn), "Trigger In off is accepted");
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
            CHECK(r.notesOut == 1, "the OS's MIDI out reaches the host: the piezo hit's note, not the incoming one (%d)", r.notesOut);
            if (res.hasFactory()) {          // the Banks window's work, without the window
                BankService& bs = p.banks();
                bs.save(0);
                CHECK(pumpUntil([&] { return !bs.busy(); }, 60000), "save finishes");
                bankfiles::BankFile f;
                juce::String err;
                CHECK(bankfiles::readBankFile(bs.lastSaved(), f, err) && f.count == 50, "bank 1 saved as %s (%s)",
                      bs.lastSaved().getFileName().toRawUTF8(), bs.message().toRawUTF8());
                std::printf("%s\n", bs.message().toRawUTF8());
                bs.load(5, f);
                CHECK(pumpUntil([&] { return !bs.busy(); }, 60000), "load finishes");
                std::printf("%s\n", bs.message().toRawUTF8());
                CHECK(!bs.messageIsError(), "load: %s", bs.message().toRawUTF8());
                CHECK(pumpUntil([&] { return p.engine().state() == nd2::Engine::State::Running; }, 15000), "the machine runs after the load");
                pump(300);
                bs.check();
                const auto t0 = juce::Time::getMillisecondCounter();
                CHECK(pumpUntil([&] { bool all = true; for (int b = 0; b < 8; ++b) all = all && bs.status(b).known; return all && !bs.checking(); }, 120000),
                      "check finishes");
                std::printf("check of the 8 banks: %.1f s; programs per bank:", (juce::Time::getMillisecondCounter() - t0) / 1000.0);
                for (int b = 0; b < 8; ++b) std::printf(" %d", bs.status(b).count);
                std::printf("\n");
                CHECK(bs.status(5).crcs == f.crcs && bs.status(5).crcs == bs.status(0).crcs, "bank 6 holds bank 1's programs");
                CHECK(bs.changedSinceLoading(5) == 0 && bs.records()[5].name == f.name, "bank 6 named %s", bs.records()[5].name.toRawUTF8());
                CHECK(bs.status(7).known && bs.status(7).count == 0, "bank 8 empty");
                // these banks for new instances: the factory memory and its names are replaced
                std::array<juce::String, 8> names;
                for (int b = 0; b < 8; ++b) names[size_t(b)] = bs.records()[size_t(b)].name;
                bs.useForNewInstances(names);
                CHECK(pumpUntil([&] { return !bs.busy(); }, 30000) && !bs.messageIsError(), "use for new instances: %s", bs.message().toRawUTF8());
                CHECK(res.factoryRecords()[5].set && res.factoryRecords()[5].name == f.name, "new instances get the names");
                ND2Processor fresh;
                CHECK(pumpUntil([&] { return fresh.engine().state() == nd2::Engine::State::Running; }, 15000), "a new instance runs");
                CHECK(fresh.banks().records()[5].name == f.name, "a new instance names bank 6 %s", fresh.banks().records()[5].name.toRawUTF8());
            }
            CHECK(p.engine().globalMidiChannel() == 9, "the OS's global MIDI channel is shown as 10 (%d)", p.engine().globalMidiChannel() + 1);
            for (int c = 0; c < 6; ++c)
                CHECK(p.engine().channelMidiChannel(c) == c, "drum channel %d on MIDI channel %d", c + 1, p.engine().channelMidiChannel(c) + 1);
            // state round trip: a second instance restores the machine and plays (and MIDI out to the host, off here)
            p.setMidiToHost(false);
            juce::MemoryBlock st;
            p.getStateInformation(st);
            p.setMidiToHost(true);
            std::printf("state %zu bytes\n", st.getSize());
            ND2Processor q;
            q.setBusesLayout(fullLayout());
            q.prepareToPlay(48000, 256);
            q.setStateInformation(st.getData(), int(st.getSize()));
            CHECK(pumpUntil([&] { return q.engine().state() == nd2::Engine::State::Running; }, 15000), "restored machine runs");
            play(q, 0.3, -1, -1);
            const Run r2 = play(q, 1.0, 0.2, -1);
            CHECK(energy(r2.main) > 1.0, "the restored machine plays");
            CHECK(!q.midiToHost(), "MIDI out to the host (off) is saved with the project");
            if (res.hasFactory())
                CHECK(q.banks().records()[5].name == p.banks().records()[5].name && q.banks().records()[5].set, "bank names in the project state");
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
            if (display == nullptr || (display->userArea.getWidth() >= 1294 && display->userArea.getHeight() >= 776))
                CHECK(ed->getWidth() == 1294 && ed->getHeight() == 776, "100 %% size");
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
                click(418, 572, false);                                 // parameter button 1 (FILTER) at 357..479, 543..602 dp
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
                    peer->handleMouseEvent(src, {901 * k, 821 * k}, {}, juce::MouseInputSource::defaultPressure, 0, t);
                    peer->handleMouseEvent(src, {901 * k, 821 * k}, L, juce::MouseInputSource::defaultPressure, 0, ++t);
                    for (int dy = 4; dy <= 80; dy += 4) {
                        pump(30);
                        peer->handleMouseEvent(src, {901 * k, (821 - float(dy)) * k}, L, juce::MouseInputSource::defaultPressure, 0, t += 30);
                    }
                    pump(30);
                    peer->handleMouseEvent(src, {901 * k, 741 * k}, {}, juce::MouseInputSource::defaultPressure, 0, t += 30);
                }
                pump(1200);
                const auto seg2 = p.engine().panel().seg;
                if (res.hasOs()) CHECK(seg2 != seg1, "dragging the encoder changes the value");
                shot("3_after_encoder");
                auto findWindow = [&]() -> ND2Window* {
                    ND2Window* found = nullptr;
                    std::function<void(juce::Component*)> f = [&](juce::Component* c) {
                        if (auto* x = dynamic_cast<ND2Window*>(c)) found = x;
                        for (auto* ch : c->getChildren()) f(ch);
                    };
                    f(ed.get());
                    return found;
                };
                click(900, 520, true);                                  // a right-click opens the ND2 window (first: BANKS)
                pump(1200);
                if (ND2Window* win = findWindow()) {
                    CHECK(win->tab() == ND2Window::kBanks, "the window opens on BANKS the first time (%d)", win->tab());
                    const auto wb = win->windowBounds();
                    std::printf("window %.0f,%.0f %.0f x %.0f points in %d x %d\n", wb.getX(), wb.getY(), wb.getWidth(), wb.getHeight(), ed->getWidth(), ed->getHeight());
                    CHECK(std::abs(wb.getCentreX() - ed->getWidth() / 2.0f) < 1.5f && std::abs(wb.getCentreY() - ed->getHeight() / 2.0f) < 1.5f,
                          "the window is centred");
                    CHECK(std::abs(wb.getWidth() - 1300.0f * k) < 2.0f, "the window is 1300 dp of the skin wide");
                    shot("4_window");
                    win->frame().tabs().choose(ND2Window::kSystem);
                    pump(400);
                    std::printf("SYSTEM: %s", win->systemText().toRawUTF8());
                    if (res.hasOs()) CHECK(win->systemText().contains("OS: found"), "SYSTEM shows the OS");
                    shot("4a_system");
                    win->frame().tabs().choose(ND2Window::kSkin);
                    pump(400);
                    CHECK(win->skinItems().size() >= 1 && win->skinItems()[0] == "* Default", "SKIN lists the Default skin, chosen");
                    CHECK(win->zoom().selected() == 2, "SKIN: Zoom 100 %% (%d)", win->zoom().selected());
                    shot("4b_skin");
                } else {
                    CHECK(false, "a right-click opens the ND2 window");
                }
                click(60, 60, false);                                   // a click on the dimmed area closes it
                pump(400);
                CHECK(findWindow() == nullptr, "a click on the dimmed area closes the window");
                if (res.hasOs()) {                                      // typed program entry on the display
                    click(620, 300, false);
                    pump(400);
                    auto type = [&](const juce::KeyPress& k) { peer->handleKeyPress(k); pump(60); };
                    for (const char* s : {"p", "3"}) type(juce::KeyPress(0, {}, juce::juce_wchar(*s)));
                    type(juce::KeyPress(juce::KeyPress::returnKey));            // incomplete: nothing happens
                    pump(600);
                    const int before = p.engine().currentProgram();
                    for (const char* s : {"1", "2"}) type(juce::KeyPress(0, {}, juce::juce_wchar(*s)));
                    shot("4b_typing");
                    CHECK(p.engine().currentProgram() == before, "no change before Return");
                    type(juce::KeyPress(juce::KeyPress::returnKey));
                    CHECK(pumpUntil([&] { return p.engine().currentProgram() == 2 * 50 + 11; }, 3000),
                          "P3.12 typed on the display (current %d)", p.engine().currentProgram());
                    pump(800);
                    shot("4c_typed");
                }
                if (res.hasOs()) {
                click(900, 520, true);                                  // again: on the tab used last (SKIN), then BANKS
                pump(800);
                ND2Window* bw = findWindow();
                CHECK(bw != nullptr && bw->tab() == ND2Window::kSkin, "the window opens on the tab used last");
                if (bw != nullptr) bw->frame().tabs().choose(ND2Window::kBanks);
                CHECK(pumpUntil([&] { return p.banksOpen.load(); }, 3000), "the BANKS tab");
                // a point of the window's layout (dp of the window) as dp of the skin, for click()
                auto at = [&](float lx, float ly) {
                    const auto wb = bw->windowBounds();
                    const float kl = wb.getWidth() / ND2Window::kLayoutW;
                    return juce::Point<float>((wb.getX() + lx * kl) / k, (wb.getY() + ly * kl) / k);
                };
                if (res.hasFactory()) {                                 // the banks are checked (and Library/Factory made)
                    pump(500);
                    pumpUntil([&] {
                        bool all = !p.banks().checking();
                        for (int b = 0; b < 8; ++b) all = all && p.banks().status(b).known;
                        return all;
                    }, 120000);
                    for (int b = 0; b < 4; ++b)                         // every factory bank is in the Library once
                        CHECK(bankfiles::findInLibrary(p.banks().status(b).crcs) != juce::File(), "factory bank %d is in the Library", b + 1);
                }
                pump(2000);
                shot("5_banks");
                if (res.hasFactory()) {                                 // drag the first bank file onto bank 5
                    const auto src = juce::MouseInputSource::InputSourceType::mouse;
                    const auto L = juce::ModifierKeys::leftButtonModifier;
                    auto t = juce::Time::currentTimeMillis();
                    // the first bank file (body at 20, 82 of the window; list from 64; rows 45) onto bank 5 (rows 72 + 8)
                    const juce::Point<float> from = at(20 + 250, 82 + 64 + 22) * k, to = at(20 + 900, 82 + 64 + 4 * 80 + 36) * k;
                    peer->handleMouseEvent(src, from, {}, juce::MouseInputSource::defaultPressure, 0, t);
                    peer->handleMouseEvent(src, from, L, juce::MouseInputSource::defaultPressure, 0, ++t);
                    for (int i = 1; i <= 20; ++i) {
                        pump(30);
                        peer->handleMouseEvent(src, from + (to - from) * (float(i) / 20.0f), L, juce::MouseInputSource::defaultPressure, 0, t += 30);
                    }
                    pump(400);
                    shot("5b_drag");
                    peer->handleMouseEvent(src, to, {}, juce::MouseInputSource::defaultPressure, 0, t += 30);
                    pump(300);
                    CHECK(p.banks().busy() || p.banks().status(4).count == 50, "dropping a bank file onto bank 5 loads it");
                    pumpUntil([&] { return !p.banks().busy(); }, 60000);
                    pump(1500);
                    std::printf("drop: %s\n", p.banks().message().toRawUTF8());
                    CHECK(p.banks().status(4).count == 50 && p.banks().status(4).crcs == p.banks().status(0).crcs, "bank 5 holds the dropped file");
                    shot("5c_loaded");
                }
                ND2Window* win = findWindow();                          // the MIDI tab
                CHECK(win != nullptr, "the ND2 window is open");
                if (win != nullptr) {
                    win->frame().tabs().choose(ND2Window::kMidi);
                    pump(600);
                    CHECK(!p.banksOpen.load(), "the MIDI tab: the BANKS label is not lit");
                    std::printf("MIDI tab: %s; footer: %s\n", win->midiChannelsText().toRawUTF8(), win->frame().footer().text().toRawUTF8());
                    CHECK(win->midiChannelsText() == "global 10; MIDI channels 1-6", "the MIDI tab shows the OS's channels");
                    shot("5d_midi");
                    win->midiSwitch().toggle();
                    CHECK(!p.midiToHost(), "the switch turns MIDI out to the host off");
                    CHECK(win->frame().footer().text().startsWith("MIDI out to the host is off"), "the footer follows the switch");
                    win->midiSwitch().toggle();
                    CHECK(p.midiToHost(), "and on again");
                }
                {
                    const auto c = at(ND2Window::kLayoutW - 20 - 17, 32);  // the round close button
                    click(c.x, c.y, false);
                }
                CHECK(pumpUntil([&] { return findWindow() == nullptr; }, 3000), "the round button closes the window");
                pump(600);
                shot("6_banks_closed");
                }
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
