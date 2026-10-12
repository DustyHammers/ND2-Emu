// What a skin needs from the instrument. The plugin implements it over the Engine; tests use a recording fake.
// No JUCE and no emulator types here: the skin runtime (plugin/skin) builds and tests on its own.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace skin {

constexpr int kButtons = 32;        // panel button index = row * 8 + bit (see source/core/nd2/panel_map.h)
constexpr int kLeds = 32;           // LED index (column - 4) * 8 + bit
constexpr int kMasterMax = 1023;    // MASTER LEVEL knob position, 0 ... 1023

class PanelPort {
public:
    virtual ~PanelPort() = default;
    virtual void sendButton(int index, bool down) = 0;        // a front-panel button pressed / released
    virtual void turnEncoder(int detents) = 0;                // the value encoder: + clockwise, - anticlockwise
    virtual void setMaster(int raw) = 0;                      // MASTER LEVEL knob position, 0 ... kMasterMax
    virtual int master() = 0;
    virtual int ledLevel(int led) = 0;                        // brightness in eighths: 0 off ... 8 full
    // The display: one byte per digit, leftmost first, as the OS drives it (active low: a 0 bit is a lit segment).
    // Bits 0-7 = segments a (top), b (upper right), c (lower right), d (bottom), e (lower left), f (upper left),
    // g (middle), dp.
    virtual std::array<uint8_t, 4> segments() = 0;
    virtual uint32_t serial() { return 0; }                   // changes when the display, an LED or an action's light changed
    // Actions of the plugin itself (not the instrument's panel), clicked on the skin: <action name="banks"> opens the
    // Banks window. actionLit: the action's window is open (the element gets class lit).
    virtual void action(const std::string& name) {}
    virtual bool actionLit(const std::string& name) { return false; }
};

struct PluginInfo {
    std::string name = "ND2", vendor = "DHammers", version;
};

}  // namespace skin
