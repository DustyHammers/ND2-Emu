// ND2 audio processor: the emulated machine (nd2::Engine) as a VST3/AU instrument and a standalone app.
//
// Buses: "Main" stereo out; "Ch 1" ... "Ch 6" stereo outs (each channel's exact share of the main output; off until
// the host enables them); "Trigger In" (up to 6 channels = pads 1-6: piezo audio from the host); MIDI in and out.
// State: the complete machine (presets stored on the panel included), the MASTER LEVEL knob, the window size, and
// the names of the bank files loaded into the 8 banks (Banks window).
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

class ND2Processor : public juce::AudioProcessor, private juce::Timer {
public:
    static constexpr int kChannelOuts = 6;

    ND2Processor();
    ~ND2Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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
    std::atomic<bool> banksOpen{false};              // the Banks window is open (the BANKS label is lit)

private:
    class Port : public skin::PanelPort {
    public:
        explicit Port(nd2::Engine& e) : engine(e) {}
        void sendButton(int index, bool down) override { engine.pressButton(index, down); }
        void turnEncoder(int detents) override { engine.turnEncoder(detents); }
        void setMaster(int raw) override { engine.setMasterLevel(float(raw) / float(skin::kMasterMax)); }
        int master() override { return juce::roundToInt(engine.masterLevel() * float(skin::kMasterMax)); }
        int ledLevel(int led) override { return engine.ledLevel(led); }
        std::array<uint8_t, 4> segments() override { return engine.panel().seg; }
        uint32_t serial() override { return engine.uiSerial() + (owner->banksOpen.load() ? 0x80000000u : 0u); }
        void action(const std::string& name) override;
        bool actionLit(const std::string& name) override { return name == "banks" && owner->banksOpen.load(); }
        nd2::Engine& engine;
        ND2Processor* owner = nullptr;
        std::shared_ptr<int> alive = std::make_shared<int>(0);
    };

    static BusesProperties buses();
    static bool acceptsFewerTriggerChannels();       // the host takes no 6-channel Trigger In (Ableton Live)
    void timerCallback() override;
    void startMachine();                             // the OS (and the factory memory) are there: boot or restore
    bool channelOutsEnabled() const;

    nd2::Engine engine_;
    Port port_{engine_};
    BankService banks_{engine_};
    uint32_t resourcesSerial_ = 0;
    bool started_ = false, bootedWithoutFactory_ = false;
    bool projectBanks_ = false;                      // the bank names came from a project
    std::vector<uint8_t> pendingState_;              // a project's machine, waiting for the OS
    // audio thread
    std::vector<nd2::Engine::MidiEvent> events_;
    std::array<std::vector<float>, nd2::Engine::kPads> trig_;
    std::vector<float> scratch_;
    int trigChannels_ = 0;
    std::atomic<bool> processed_{false};             // diagnostics: processBlock has run
    bool processedLogged_ = false, runningLogged_ = false;
    bool stems_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ND2Processor)
};
