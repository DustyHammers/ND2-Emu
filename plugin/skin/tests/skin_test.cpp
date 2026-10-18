// Skin runtime tests (no JUCE, no OS image): the text-only test skin with a recording fake instrument.
#include <RmlUi/Core.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "skin_discovery.h"
#include "skin_elements.h"
#include "skin_system.h"
#include "skin_view.h"

namespace fs = std::filesystem;
using namespace skin;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct FakePort : PanelPort {
    std::vector<std::string> log;
    bool banksOpen = false;
    void action(const std::string& name) override { log.push_back("action " + name); }
    bool actionLit(const std::string& name) override { return name == "banks" && banksOpen; }
    int masterRaw = 1023, detents = 0;
    std::array<int, kLeds> levels{};
    std::array<uint8_t, 4> segs{0xFF, 0xFF, 0xFF, 0xFF};
    void sendButton(int i, bool down) override { log.push_back("button " + std::to_string(i) + (down ? " down" : " up")); }
    void turnEncoder(int d) override { detents += d; log.push_back("encoder " + std::to_string(d)); }
    void setMaster(int raw) override { masterRaw = raw; log.push_back("master " + std::to_string(raw)); }
    int master() override { return masterRaw; }
    int ledLevel(int led) override { return led >= 0 && led < kLeds ? levels[size_t(led)] : 0; }
    std::array<uint8_t, 4> segments() override { return segs; }
    std::array<int, kBendPots> bends{};
    bool bent = false;
    int bend(int pot) override { return pot >= 0 && pot < kBendPots ? bends[size_t(pot)] : 0; }
    int bendSteps(int pot) override { return pot == 3 || pot == 5 ? kBendSteps : 0; }   // as the plugin: pots 4 and 6 stepped
    void setBend(int pot, int value) override { bends[size_t(pot)] = value; log.push_back("bend " + std::to_string(pot) + " " + std::to_string(value)); }
    void bendGesture(int pot, bool begin) override { log.push_back("gesture " + std::to_string(pot) + (begin ? " begin" : " end")); }
    bool bentView() override { return bent; }
    void setBentView(bool on) override { bent = on; log.push_back(on ? "bent view on" : "bent view off"); }
};

static Rml::Element* byId(SkinView& v, const char* id) {
    for (int i = 0; i < v.context()->GetNumDocuments(); ++i)
        if (Rml::Element* e = v.context()->GetDocument(i)->GetElementById(id)) return e;
    return nullptr;
}
static std::string last(const FakePort& p) { return p.log.empty() ? std::string() : p.log.back(); }

int main() {
    const std::string skins = ND2_TEST_SKINS;
    // ---- discovery
    {
        const auto found = discoverSkins(skins);
        CHECK(found.size() == 1 && found[0].name == "test", "discovered %zu skins", found.size());
    }
    FakePort port;
    SkinView v(port, PluginInfo{"ND2", "DHammers", "test"}, nullptr);
    v.setViewport(1725, 1035, 1.0f);
    std::string err;
    CHECK(v.load(skins + "/test", "test", err), "load: %s", err.c_str());
    double t = 1.0;
    v.update(t);
    CHECK(v.evalForTest("shownCount") == "1", "onshow ran %s times", v.evalForTest("shownCount").c_str());
    CHECK(v.evalForTest("plugin.name .. ' ' .. plugin.vendor") == "ND2 DHammers", "plugin table");

    // ---- buttons: press and release edges reach the instrument and Lua
    v.mouseMove(110, 210, 0); v.mouseDown(0, 0);
    CHECK(last(port) == "button 8 down", "press: %s", last(port).c_str());
    CHECK(byId(v, "bParam1")->IsClassSet("pressed"), "pressed class");
    v.mouseMove(600, 600, 0); v.mouseUp(0, 0);                       // released outside the button
    CHECK(last(port) == "button 8 up", "release: %s", last(port).c_str());
    CHECK(v.evalForTest("lastEvent()") == "button8 false", "Lua button: %s", v.evalForTest("lastEvent()").c_str());
    v.mouseMove(10, 210, 0); v.mouseDown(0, ModAlt); v.mouseUp(0, 0);   // Alt-click: latched
    CHECK(last(port) == "button 1 down" && v.isHeld(1), "latch: %s", last(port).c_str());
    v.releaseHeldButtons();
    CHECK(last(port) == "button 1 up" && !v.isHeld(1), "Esc releases: %s", last(port).c_str());

    // ---- encoder: drag up 3 detents (12 dp each), wheel 2 notches down; Lua sees the same
    v.mouseMove(250, 50, 0); v.mouseDown(0, 0);
    v.mouseMove(250, 50 - 37, 0); v.mouseUp(0, 0);
    CHECK(port.detents == 3, "drag: %d detents", port.detents);
    v.mouseMove(250, 50, 0); v.mouseWheel(2.0f, 0);
    CHECK(port.detents == 1, "wheel: %d detents", port.detents);
    v.evalForTest("panel.turn(5)");
    CHECK(port.detents == 6 && v.evalForTest("lastEvent()") == "encoder 5", "panel.turn: %d", port.detents);
    v.update(t += 0.02);
    {
        auto* e = dynamic_cast<SpriteElement*>(byId(v, "value"));
        // 6 detents of 24 per turn = a quarter turn = frame 18 of 72
        CHECK(e && e->shownSprite() == "image(enc_018)", "encoder frame %s", e ? e->shownSprite().c_str() : "?");
    }

    // ---- master knob: drag down 64 dp = a quarter of the range; the sprite follows
    v.mouseMove(50, 50, 0); v.mouseDown(0, 0); v.mouseMove(50, 114, 0); v.mouseUp(0, 0);
    CHECK(port.masterRaw == 1023 - 256 && last(port) == "master 767", "knob drag: %d", port.masterRaw);
    v.update(t += 0.02);
    {
        auto* k = dynamic_cast<SpriteElement*>(byId(v, "master"));
        CHECK(k && k->shownSprite() == "image(master_095)", "knob frame %s", k ? k->shownSprite().c_str() : "?");
    }
    port.masterRaw = 100;                                              // a project load moved it
    v.update(t += 0.02);
    CHECK(v.evalForTest("panel.master()") == "100", "panel.master %s", v.evalForTest("panel.master()").c_str());

    // ---- circuit-bent mode: Cmd-click shows the bend pots and LED; a smooth pot follows the drag, a stepped one selects
    // the nearest of its 9 positions and settles on it
    {
        Rml::Element* body = v.context()->GetDocument(0);
        v.mouseMove(30, 630, 0); v.mouseDown(0, 0); v.mouseUp(0, 0);    // hidden: nothing there to turn
        CHECK(port.bends[0] == 0 && !body->IsClassSet("bent"), "pots hidden at first");
        v.mouseMove(900, 900, 0); v.mouseDown(0, ModMeta); v.mouseUp(0, ModMeta);
        v.update(t += 0.02);
        CHECK(port.bent && body->IsClassSet("bent") && last(port) == "bent view on", "Cmd-click shows the bent panel");
        // smooth pot (pot 1): its whole travel over 256 dp
        size_t before = port.log.size();
        v.mouseMove(30, 630, 0); v.mouseDown(0, 0); v.mouseMove(30, 630 - 20, 0); v.mouseMove(30, 630 - 64, 0);
        auto* pot = dynamic_cast<BendPotElement*>(byId(v, "pot1"));
        CHECK(port.bends[0] >= 250 && port.bends[0] <= 262, "smooth: drag up 64 dp: %d", port.bends[0]);
        CHECK(port.log.size() >= before + 3 && port.log[before] == "gesture 0 begin" && port.log[before + 1].rfind("bend 0 ", 0) == 0,
              "smooth: sends as it turns: %s", port.log.size() > before ? port.log[before].c_str() : "-");
        const int at64 = port.bends[0];
        v.mouseMove(30, 630 - 70, 0);
        CHECK(port.bends[0] > at64, "smooth: every move sends (%d)", port.bends[0]);
        v.update(t += 0.05);
        auto* led = byId(v, "bled");
        auto* lamp = led ? led->GetChild(0) : nullptr;
        auto opacity = [&] { return lamp ? lamp->GetProperty<float>("opacity") : -1.0f; };
        const float o1 = opacity();
        v.update(t += 0.04);
        const float o2 = opacity();
        CHECK(o1 >= 0.35f && o1 <= 1.0f && o2 >= 0.35f && o2 <= 1.0f && (o1 < 1.0f || o2 < 1.0f), "LED flickers while turning: %.2f %.2f", o1, o2);
        const std::string sprite = pot ? pot->shownSprite() : "";
        v.mouseUp(0, 0);
        CHECK(last(port) == "gesture 0 end", "gesture ends: %s", last(port).c_str());
        CHECK(pot && pot->shownSprite() == sprite, "smooth: stays where it was let go: %s", pot ? pot->shownSprite().c_str() : "?");
        v.update(t += 0.5);
        CHECK(opacity() == 1.0f, "LED steady when still: %.2f", opacity());
        // stepped pot (pot 4)
        before = port.log.size();
        v.mouseMove(130, 630, 0); v.mouseDown(0, 0); v.mouseMove(130, 630 - 20, 0); v.mouseMove(130, 630 - 64, 0);
        auto* pot2 = dynamic_cast<BendPotElement*>(byId(v, "pot2"));
        CHECK(port.bends[3] == 256, "stepped: drag up 64 dp: position 2 = %d", port.bends[3]);
        CHECK(port.log.size() >= before + 3 && port.log[before] == "gesture 3 begin" && port.log[before + 1] == "bend 3 128"
              && port.log[before + 2] == "bend 3 256", "stepped: positions on the way: %s", port.log.size() > before + 1 ? port.log[before + 1].c_str() : "-");
        v.mouseMove(130, 630 - 70, 0);                                   // within position 2: turns, sends nothing
        CHECK(last(port) == "bend 3 256", "stepped: no send within a position: %s", last(port).c_str());
        v.mouseUp(0, 0);
        CHECK(pot2 && pot2->shownSprite() == "image(bend_032)", "stepped: settles on position 2's frame: %s", pot2 ? pot2->shownSprite().c_str() : "?");
        v.mouseMove(130, 630, 0); v.mouseWheel(-1.0f, 0);                // one notch up: position 3
        CHECK(port.bends[3] == 384, "stepped wheel: %d", port.bends[3]);
        v.mouseMove(30, 630, 0); const int w0 = port.bends[0]; v.mouseWheel(-1.0f, 0);
        CHECK(port.bends[0] == w0 + 64, "smooth wheel: a sixteenth of the travel (%d -> %d)", w0, port.bends[0]);
        port.bends[0] = kBendMax;                                         // host automation
        v.update(t += 0.02);
        CHECK(pot && pot->shownSprite() == "image(bend_127)", "automation turns the pot: %s", pot ? pot->shownSprite().c_str() : "?");
        v.mouseMove(900, 900, 0); v.mouseDown(0, ModMeta); v.mouseUp(0, ModMeta);
        v.update(t += 0.02);
        CHECK(!port.bent && !body->IsClassSet("bent"), "Cmd-click hides it again");
        port.bends = {};
    }

    // ---- LEDs: brightness to the lamp's opacity, class lit, Lua on edges only
    port.levels[21] = 8; port.levels[6] = 2;
    v.update(t += 0.02);
    CHECK(byId(v, "l21")->IsClassSet("lit") && byId(v, "l6")->IsClassSet("lit"), "lit classes");
    {
        Rml::Element* lamp = byId(v, "l6")->GetChild(0);
        const float op = lamp->GetProperty<float>("opacity");
        CHECK(op > 0.49f && op < 0.51f, "quarter duty lamp opacity %.3f (sqrt)", op);
    }
    CHECK(v.evalForTest("lastEvent()") == "led21 8", "Lua led: %s", v.evalForTest("lastEvent()").c_str());
    port.levels[21] = 6; v.update(t += 0.02);
    CHECK(v.evalForTest("lastEvent()") == "led21 8", "dimming is not an edge: %s", v.evalForTest("lastEvent()").c_str());
    port.levels[21] = 0; v.update(t += 0.02);
    CHECK(!byId(v, "l21")->IsClassSet("lit") && v.evalForTest("lastEvent()") == "led21 0", "off");

    // ---- display: active-low bytes to seg classes
    port.segs = {uint8_t(~0x41), 0xFF, 0xFF, uint8_t(~0x08)};          // digit 0: a and g lit; digit 3: d lit
    v.update(t += 0.02);
    {
        Rml::Element* d0 = byId(v, "d0");
        CHECK(d0->GetChild(0)->IsClassSet("lit") && !d0->GetChild(1)->IsClassSet("lit") && d0->GetChild(2)->IsClassSet("lit") &&
              !d0->GetChild(3)->IsClassSet("lit"), "digit 0 segments");
        Rml::Element* d3 = byId(v, "d3");
        CHECK(!d3->GetChild(0)->IsClassSet("lit") && d3->GetChild(1)->IsClassSet("lit"), "digit 3 segments");
    }
    CHECK(v.evalForTest("lastEvent()") == "display 190 255 255 247", "Lua display: %s", v.evalForTest("lastEvent()").c_str());

    // ---- plugin action: a click asks for it; class lit while its window is open
    v.mouseMove(440, 15, 0); v.mouseDown(0, 0); v.mouseUp(0, 0);
    CHECK(last(port) == "action banks", "action click: %s", last(port).c_str());
    CHECK(!byId(v, "aBanks")->IsClassSet("lit"), "action not lit yet");
    port.banksOpen = true; v.update(t += 0.02);
    CHECK(byId(v, "aBanks")->IsClassSet("lit"), "action lit");
    port.banksOpen = false; v.update(t += 0.02);
    CHECK(!byId(v, "aBanks")->IsClassSet("lit"), "action unlit");

    // ---- a click on the display asks for typed program entry
    v.mouseMove(10, 420, 0); v.mouseDown(0, 0); v.mouseUp(0, 0);
    CHECK(last(port) == "action program_entry", "display click: %s", last(port).c_str());
    CHECK(v.hoverClickable(), "the display counts as clickable");

    // ---- sandbox: no file access outside the skin folder, no io/os
    CHECK(v.evalForTest("io") == "nil" && v.evalForTest("os") == "nil" && v.evalForTest("dofile") == "nil", "sandbox");
    CHECK(v.evalForTest("while true do end").rfind("error:", 0) == 0, "endless loop is stopped");

    // ---- a missing skin and a broken one fail with a reason
    {
        std::string e2;
        CHECK(!v.load(skins + "/nothing", "nothing", e2) && !e2.empty(), "missing skin");
        const fs::path broken = fs::temp_directory_path() / "nd2_broken_skin";
        fs::create_directories(broken);
        std::ofstream(broken / "nd2_broken_skin.rml") << "<rml><body><pbutton button=\"1\"></body>";
        CHECK(!v.load(broken.u8string(), "nd2_broken_skin", e2), "broken skin loaded");
        fs::remove_all(broken);
        CHECK(v.load(skins + "/test", "test", err), "reload: %s", err.c_str());
    }

    // ---- the default skin: loads without warnings; every control is there and follows the instrument
    {
        FakePort p2;
        SkinView d(p2, PluginInfo{"ND2", "DHammers", "test"}, nullptr);
        d.setViewport(1725, 1035, 1.0f);
        std::string e2;
        CHECK(d.load(ND2_DEFAULT_SKIN, "nd2-default", e2), "default skin: %s", e2.c_str());
        p2.levels[29] = 8; p2.levels[6] = 8;
        d.update(1.0); d.update(1.1);
        for (const std::string& l : d.log())
            CHECK(l.rfind("warning", 0) != 0 && l.rfind("error", 0) != 0, "default skin log: %s", l.c_str());
        int n[5] = {};
        Rml::ElementDocument* doc = d.context()->GetDocument(0);
        Rml::ElementList list;
        const char* tags[5] = {"pbutton", "led", "knob", "encoder", "segdigit"};
        for (int i = 0; i < 5; ++i) { list.clear(); doc->GetElementsByTagName(list, tags[i]); n[i] = int(list.size()); }
        CHECK(n[0] == 15 && n[1] == 29 && n[2] == 1 && n[3] == 1 && n[4] == 4, "controls %d %d %d %d %d", n[0], n[1], n[2], n[3], n[4]);
        CHECK(doc->GetElementById("l_29")->IsClassSet("lit") && doc->GetElementById("l_6")->IsClassSet("green"), "LED 29 lit, LED 6 green");
        // the Program button sits where the art has it: 359,271 .. 481,330 dp
        d.mouseMove(420, 300, 0); d.mouseDown(0, 0); d.mouseUp(0, 0);
        CHECK(p2.log.size() >= 2 && p2.log[p2.log.size() - 2] == "button 1 down", "Program button at its place");
        // the display (496,248 .. 758,354 dp) starts typed program entry
        d.mouseMove(620, 300, 0); d.mouseDown(0, 0); d.mouseUp(0, 0);
        CHECK(!p2.log.empty() && p2.log.back() == "action program_entry", "display: %s", p2.log.empty() ? "" : p2.log.back().c_str());
        // no BANKS label any more (everything is in the ND2 window a right-click opens): the top left does nothing
        const size_t before = p2.log.size();
        d.mouseMove(180, 32, 0); d.mouseDown(0, 0); d.mouseUp(0, 0);
        CHECK(p2.log.size() == before, "nothing at the old BANKS label: %s", p2.log.empty() ? "" : p2.log.back().c_str());
        CHECK(doc->GetElementById("a_banks") == nullptr, "no BANKS label in the Default skin");
        // circuit-bent mode: six pots and the LED, hidden until Cmd-click; pot 1 turns at its place (centre 350,118 dp)
        list.clear(); doc->GetElementsByTagName(list, "bendpot");
        Rml::ElementList leds; doc->GetElementsByTagName(leds, "bendled");
        CHECK(list.size() == 6 && leds.size() == 1, "bend pots %zu, bend LED %zu", list.size(), leds.size());
        CHECK(!doc->IsClassSet("bent"), "the bent panel is closed at first");
        d.mouseMove(1600, 950, 0); d.mouseDown(0, ModMeta); d.mouseUp(0, ModMeta);
        d.update(1.2);
        CHECK(p2.bent && doc->IsClassSet("bent"), "Cmd-click opens the bent panel");
        d.mouseMove(350, 118, 0); d.mouseDown(0, 0); d.mouseMove(350, 118 - 260, 0); d.mouseUp(0, 0);
        CHECK(p2.bends[0] == kBendMax, "pot 1 turned fully up: %d", p2.bends[0]);
        d.mouseMove(1375, 118, 0); d.mouseWheel(-1.0f, 0);
        CHECK(p2.bends[5] == 128, "pot 6 (stepped) one notch: %d", p2.bends[5]);
        d.update(1.3);
        for (const std::string& l : d.log())
            CHECK(l.rfind("warning", 0) != 0 && l.rfind("error", 0) != 0, "default skin log (bent): %s", l.c_str());
    }

    std::printf(fails ? "%d failure(s)\n" : "all skin tests passed\n", fails);
    return fails ? 1 : 0;
}
