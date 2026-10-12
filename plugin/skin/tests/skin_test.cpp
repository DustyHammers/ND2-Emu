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
        // the BANKS label (134,15 .. 226,49 dp) opens the Banks window and is lit while it is open
        d.mouseMove(180, 32, 0); d.mouseDown(0, 0); d.mouseUp(0, 0);
        CHECK(!p2.log.empty() && p2.log.back() == "action banks", "BANKS label: %s", p2.log.empty() ? "" : p2.log.back().c_str());
        p2.banksOpen = true; d.update(1.2);
        Rml::Element* banks = doc->GetElementById("a_banks");
        CHECK(banks && banks->IsClassSet("lit"), "BANKS lit");
    }

    std::printf(fails ? "%d failure(s)\n" : "all skin tests passed\n", fails);
    return fails ? 1 : 0;
}
