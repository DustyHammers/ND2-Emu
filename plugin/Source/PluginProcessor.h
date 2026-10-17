// ND2 audio processor: the emulated machine (nd2::Engine) as a VST3/AU instrument and a standalone app.
//
// Buses: "Main" stereo out; "Ch 1" ... "Ch 6" stereo outs (each channel's exact share of the main output; off until
// the host enables them); "Trigger In" (up to 6 channels = pads 1-6: piezo audio from the host); MIDI in and out.
// State: the complete machine (presets stored on the panel included), the MASTER LEVEL knob, the window size, the
// names of the bank files loaded into the 8 banks (Banks window), MIDI out to the host, and the six bends.
// Parameters (host automation): Bend 1 ... Bend 6, the circuit-bent mode's bend for each pad (Off and 8 bends; IDs
// bend1 ... bend6, fixed: hosts store automation by them). The only parameters; the panel's knob and buttons are not.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "BankService.h"
#include "engine.h"
#include "skin_port.h"

class ND2Processor : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorParameter::Listener {
public:
    static constexpr int kChannelOuts = 6;
    static constexpr int kBendPads = 6;

    ND2Processor();
    ~ND2Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- for the editor (message thread)
    nd2::Engine& engine() { return engine_; }
    skin::PanelPort& panelPort() { return port_; }              // thread-safe: used by the skin render thread
    // Booted without factory presets (none were loaded yet): the editor offers to load them.
    bool bootedWithoutFactory() const { return bootedWithoutFactory_; }
    std::atomic<float> editorScale{0};               // window size of this project (0: not set); saved with it
    BankService& banks() { return banks_; }
    // Skin actions (<action name="banks">): a click on the panel, handled by the open editor (message thread).
    std::function<void(const juce::String&)> onSkinAction;
    std::atomic<bool> banksOpen{false};              // the window is open on its BANKS tab (the BANKS label is lit)
    // The OS's MIDI out to the host (panel moves as CCs, pad hits as notes, panel program changes); on by default,
    // saved with the project.
    void setMidiToHost(bool on) { midiToHost_ = on; engine_.setMidiToHost(on); }
    bool midiToHost() const { return midiToHost_.load(); }
    // Circuit-bent mode: a bend per pad (0 = Off, 1-8), as host parameters; the bent panel (Cmd-click) is shown while
    // bentView is set (not saved: it opens by itself when a bend is turned on, also by automation or a project).
    juce::AudioParameterChoice* bendParam(int pad) const { return pad >= 0 && pad < kBendPads ? bends_[size_t(pad)] : nullptr; }
    std::atomic<bool> bentView{false};
    static const juce::StringArray& bendNames();
    // Typed program entry: what the display shows instead of the OS's display while it runs (any thread reads it).
    void setEntryDisplay(bool on, std::array<uint8_t, 4> seg) {
        entrySegs_.store(uint32_t(seg[0]) | uint32_t(seg[1]) << 8 | uint32_t(seg[2]) << 16 | uint32_t(seg[3]) << 24);
        entryOn_.store(on);
        entrySerial_.fetch_add(1);
    }

private:
    std::atomic<bool> midiToHost_{true};
    class Port : public skin::PanelPort {
    public:
        explicit Port(nd2::Engine& e) : engine(e) {}
        void sendButton(int index, bool down) override { engine.pressButton(index, down); }
        void turnEncoder(int detents) override { engine.turnEncoder(detents); }
        void setMaster(int raw) override { engine.setMasterLevel(float(raw) / float(skin::kMasterMax)); }
        int master() override { return juce::roundToInt(engine.masterLevel() * float(skin::kMasterMax)); }
        int ledLevel(int led) override { return engine.ledLevel(led); }
        std::array<uint8_t, 4> segments() override {
            if (owner->entryOn_.load()) {                // typed program entry (ProgramEntry) instead of the OS's display
                const uint32_t w = owner->entrySegs_.load();
                return {uint8_t(w), uint8_t(w >> 8), uint8_t(w >> 16), uint8_t(w >> 24)};
            }
            return engine.panel().seg;
        }
        uint32_t serial() override {
            return engine.uiSerial() + owner->entrySerial_.load() * 7919u + owner->bendSerial_.load() * 104729u
                 + (owner->bentView.load() ? 0x40000000u : 0u) + (owner->banksOpen.load() ? 0x80000000u : 0u);
        }
        int bend(int pad) override { return engine.bend(pad); }
        void setBend(int pad, int value) override;
        void bendGesture(int pad, bool begin) override;
        bool bentView() override { return owner->bentView.load(); }
        void setBentView(bool on) override { owner->bentView.store(on); owner->bendSerial_.fetch_add(1); }
        void action(const std::string& name) override;
        bool actionLit(const std::string& name) override { return name == "banks" && owner->banksOpen.load(); }
        nd2::Engine& engine;
        ND2Processor* owner = nullptr;
        std::shared_ptr<int> alive = std::make_shared<int>(0);
    };

    static BusesProperties buses();
    static bool acceptsFewerTriggerChannels();       // the host takes no 6-channel Trigger In (Ableton Live)
    void timerCallback() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    void startMachine();                             // the OS (and the factory memory) are there: boot or restore
    bool channelOutsEnabled() const;

    nd2::Engine engine_;
    Port port_{engine_};
    BankService banks_{engine_};
    uint32_t resourcesSerial_ = 0;
    bool started_ = false, bootedWithoutFactory_ = false;
    bool projectBanks_ = false;                      // the bank names came from a project
    std::array<juce::AudioParameterChoice*, kBendPads> bends_{};
    std::atomic<uint32_t> bendSerial_{0};
    std::atomic<bool> entryOn_{false};
    std::atomic<uint32_t> entrySegs_{0xFFFFFFFF}, entrySerial_{0};
    std::vector<uint8_t> pendingState_;              // a project's machine, waiting for the OS
    // audio thread
    std::vector<nd2::Engine::MidiEvent> events_;
    std::array<std::vector<float>, nd2::Engine::kPads> trig_;
    std::vector<float> scratch_;
    int trigChannels_ = 0;
    bool stems_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ND2Processor)
};
