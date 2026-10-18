#include "PluginProcessor.h"
#include <atomic>
#include <cmath>
#include "PluginEditor.h"
#include "Resources.h"

namespace {
constexpr uint32_t kStateMagic = 0x50324E44;     // "ND2P" (little-endian)
constexpr uint32_t kStateVersion = 5;            // 2: bank names after the machine; 3: then MIDI out to the host;
                                                 // 4 (test builds only): then six per-pad bends (one byte each, not
                                                 // read any more); 5: then the six kit bend amounts (floats)
constexpr int kTimerMs = 100;
}  // namespace

juce::AudioProcessor::BusesProperties ND2Processor::buses() {
    auto b = BusesProperties()
                 .withInput("Trigger In", juce::AudioChannelSet::discreteChannels(nd2::Engine::kPads), true)
                 .withOutput("Main", juce::AudioChannelSet::stereo(), true);
    for (int c = 1; c <= ND2Processor::kChannelOuts; ++c)
        b = b.withOutput("Ch " + juce::String(c), juce::AudioChannelSet::stereo(), false);
    return b;
}

namespace {
// The six kit bends as host parameters: smooth ones (amount 0-1, with the setting as text) and stepped ones (9 choices).
juce::RangedAudioParameter* makeBendParameter(int bend) {
    using nd2::KitBend;
    namespace bm = nd2::bendmap;
    static const char* const ids[nd2::kKitBends] = {"starve", "brownouts", "time", "sinebit", "crystal", "dacbits"};
    static const char* const names[nd2::kKitBends] = {"CPU Starve", "Brownouts", "Time", "Sine Stuck Bit", "Crystal", "DAC Bits"};
    const juce::ParameterID id(ids[bend], 1);
    if (bend == int(KitBend::SineBit))
        return new juce::AudioParameterChoice(id, names[bend], {"Off", "Bit 8", "Bit 9", "Bit 10", "Bit 11", "Bit 12", "Bit 13",
                                                                 "Bit 14", "Bit 15"}, 0);
    if (bend == int(KitBend::DacBits))
        return new juce::AudioParameterChoice(id, names[bend], {"Off", "12 bits", "10 bits", "9 bits", "8 bits", "7 bits",
                                                                 "6 bits", "5 bits", "4 bits"}, 0);
    auto text = [bend](float v, int) -> juce::String {
        if (v <= 0.0f) return "Off";
        switch (KitBend(bend)) {
        case KitBend::Starve: return juce::String(int(bm::starveFrameCycles(v))) + " cycles";
        case KitBend::Brownouts: return juce::String(bm::brownoutsPerSecond(v), 1) + " per second";
        case KitBend::Time: return v >= 1.0f ? juce::String("Freeze") : "x" + juce::String(1.0 / bm::timeRate(v), 1);
        case KitBend::Crystal: return "-" + juce::String(juce::roundToInt((1.0 - bm::crystalRatio(v)) * 100.0)) + " %";
        default: return juce::String(v, 2);
        }
    };
    return new juce::AudioParameterFloat(id, names[bend], juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction(text));
}
}  // namespace

ND2Processor::ND2Processor() : AudioProcessor(buses()) {
    static_assert(kBends == skin::kBendPots && nd2::kBendSteps == skin::kBendSteps, "bends and steps agree");
    for (int k = 0; k < kBends; ++k) {
        auto* p = makeBendParameter(k);
        addParameter(p);
        p->addListener(this);
        bends_[size_t(k)] = p;
        remembered_[size_t(k)].store(0.0f);
    }
    events_.reserve(4096);
    port_.owner = this;
    setMidiToHost(true);                             // the OS's MIDI out (panel CCs, pad notes, programs) to the host
    Resources::get();                                // finds the OS and the factory memory
    timerCallback();
    startTimer(kTimerMs);
}

ND2Processor::~ND2Processor() {
    stopTimer();
    for (auto* p : bends_) p->removeListener(this);
}

// A bend parameter changed (the pot, host automation, a project; any thread, the audio thread included): the engine
// takes it lock-free at its next block. Turning a bend on opens the bent panel, so a bent sound is always visible.
void ND2Processor::parameterValueChanged(int parameterIndex, float) {
    for (int k = 0; k < kBends; ++k)
        if (bends_[size_t(k)]->getParameterIndex() == parameterIndex) {
            const float v = bends_[size_t(k)]->getValue();
            engine_.setKitBend(k, v);
            if (v > 0.0f) bentView.store(true);
            bendSerial_.fetch_add(1);
        }
}

// The skin's render thread: the engine at once; the parameter (and so the host) on the message thread.
void ND2Processor::Port::setBend(int pot, int value) {
    if (pot < 0 || pot >= kBends) return;
    float a = float(juce::jlimit(0, skin::kBendMax, value)) / float(skin::kBendMax);
    if (nd2::bendmap::stepped(pot)) a = float(nd2::bendmap::step(a)) / float(nd2::kBendSteps - 1);
    engine.setKitBend(pot, a);
    owner->bendSerial_.fetch_add(1);
    std::weak_ptr<int> al = alive;
    ND2Processor* p = owner;
    juce::MessageManager::callAsync([al, p] { if (al.lock()) p->syncBendParams(); });
}

void ND2Processor::Port::bendGesture(int pot, bool begin) {
    std::weak_ptr<int> a = alive;
    ND2Processor* p = owner;
    juce::MessageManager::callAsync([a, p, pot, begin] {
        if (!a.lock()) return;
        if (auto* prm = p->bendParam(pot)) { if (begin) prm->beginChangeGesture(); else prm->endChangeGesture(); }
    });
}

// Message thread: each parameter takes the engine's amount (after a pot turn or Cmd-click; no-op when they agree).
void ND2Processor::syncBendParams() {
    for (int k = 0; k < kBends; ++k) {
        auto* prm = bends_[size_t(k)];
        const float v = engine_.kitBend(k);
        if (std::abs(prm->getValue() - v) > 1e-6f) prm->setValueNotifyingHost(v);
    }
}

// Cmd-click. Hiding the panel turns every bend Off (the drum as normal) and remembers the amounts; showing it again
// brings back the amounts it had when it was hidden (this session only: not saved).
void ND2Processor::setBends(bool shown) {
    if (!shown) {
        for (int k = 0; k < kBends; ++k) { remembered_[size_t(k)].store(engine_.kitBend(k)); engine_.setKitBend(k, 0.0f); }
    } else {
        for (int k = 0; k < kBends; ++k) engine_.setKitBend(k, remembered_[size_t(k)].load());
    }
    bentView.store(shown);
    bendSerial_.fetch_add(1);
    std::weak_ptr<int> a = port_.alive;
    juce::MessageManager::callAsync([a, this, shown] {
        if (!a.lock()) return;
        for (int k = 0; k < kBends; ++k) {
            auto* prm = bends_[size_t(k)];
            const float v = engine_.kitBend(k);
            if (std::abs(prm->getValue() - v) <= 1e-6f) continue;
            prm->beginChangeGesture(); prm->setValueNotifyingHost(v); prm->endChangeGesture();
        }
        bentView.store(shown);                       // the parameters' own changes don't reopen a hidden panel
        bendSerial_.fetch_add(1);
    });
}

// The skin's render thread: handled on the message thread by the editor, if this instance still exists.
void ND2Processor::Port::action(const std::string& name) {
    std::weak_ptr<int> a = alive;
    ND2Processor* p = owner;
    const juce::String n = juce::String::fromUTF8(name.c_str());
    juce::MessageManager::callAsync([a, p, n] { if (a.lock() && p->onSkinAction) p->onSkinAction(n); });
}

// The OS and the factory memory are shared; when they appear (located, built), this instance starts with them.
void ND2Processor::timerCallback() {
    // With the window open and the host not processing audio (Logic Pro: a track that is not live or playing), the
    // machine runs on its own so the panel answers; never in offline renders.
    engine_.setIdleRun(getActiveEditor() != nullptr && !isNonRealtime());
    Resources& r = Resources::get();
    if (r.serial() == resourcesSerial_) return;
    resourcesSerial_ = r.serial();
    if (!r.hasOs()) return;
    if (!started_) { startMachine(); return; }
    // Factory presets arrived while this instance ran without them (it had none to lose): start again with them.
    if (bootedWithoutFactory_ && r.hasFactory()) {
        engine_.setBootFlash(r.factoryFlash());
        std::string err;
        if (engine_.setOs(r.os(), err)) {
            bootedWithoutFactory_ = false;
            if (!projectBanks_) banks_.setRecords(r.factoryRecords());
        }
    }
}

void ND2Processor::startMachine() {
    Resources& r = Resources::get();
    engine_.setBootFlash(r.hasFactory() ? r.factoryFlash() : std::vector<uint8_t>());
    std::string err;
    if (!engine_.setOs(r.os(), err)) return;
    started_ = true;
    bootedWithoutFactory_ = !r.hasFactory();
    if (pendingState_.empty() && !projectBanks_) banks_.setRecords(r.factoryRecords());   // a new instance
    if (!pendingState_.empty()) {                    // a project opened before the OS was there
        if (engine_.setState(pendingState_, err)) bootedWithoutFactory_ = false;
        pendingState_.clear();
    }
}

bool ND2Processor::channelOutsEnabled() const {
    for (int b = 1; b < getBusCount(false); ++b)
        if (const auto* bus = getBus(false, b); bus != nullptr && bus->isEnabled()) return true;
    return false;
}

void ND2Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const auto* in = getBus(true, 0);
    trigChannels_ = in != nullptr && in->isEnabled() ? std::min(in->getNumberOfChannels(), nd2::Engine::kPads) : 0;
    stems_ = channelOutsEnabled();
    const int block = std::max(1, samplesPerBlock);
    for (auto& t : trig_) t.assign(size_t(block), 0.0f);
    scratch_.assign(size_t(block), 0.0f);
    engine_.prepare(sampleRate, block, trigChannels_ > 0, stems_);
    setLatencySamples(engine_.latencySamples());
}

// Main: stereo. Ch 1-6: stereo or off. Trigger In: off, or all 6 channels (pads 1-6 in order). Fewer channels are
// refused: hosts that ask for a stereo input first (REAPER does) then keep the 6-channel layout instead of
// silently getting only pads 1 and 2. Ableton Live only: 1-6 channels are accepted (pads 1..n), as in 0.1.0; Live
// was reported to fail with the six-or-none rule.
bool ND2Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.outputBuses.isEmpty() || layouts.outputBuses[0] != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b)
        if (!layouts.outputBuses[b].isDisabled() && layouts.outputBuses[b] != juce::AudioChannelSet::stereo()) return false;
    for (const auto& in : layouts.inputBuses) {
        if (in.isDisabled() || in.size() == nd2::Engine::kPads) continue;
        if (!(acceptsFewerTriggerChannels() && in.size() < nd2::Engine::kPads)) return false;
    }
    return true;
}

// Hosts that give an instrument a mono or stereo input and nothing else, where six-or-none would leave ND2 unable to
// start: Ableton Live, and Apple's hosts (Logic Pro, GarageBand, MainStage), which run AUs in AUHostingService.
bool ND2Processor::acceptsFewerTriggerChannels() {
    static const bool fewer = [] {
        const juce::PluginHostType host;
        return host.isAbletonLive() || host.isLogic() || host.isGarageBand() || host.isMainStage()
            || juce::File::getSpecialLocation(juce::File::hostApplicationPath).getFullPathName().contains("AUHostingService");
    }();
    return fewer;
}

void ND2Processor::releaseResources() {}

void ND2Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int block = int(scratch_.size());
    // The trigger inputs share the buffer with the outputs (in place): copied out first, one chunk at a time.
    const auto inBus = getBusBuffer(buffer, true, 0);
    const int trigCh = std::min(trigChannels_, inBus.getNumChannels());
    auto mainBus = getBusBuffer(buffer, false, 0);
    if (mainBus.getNumChannels() < 2) { buffer.clear(); midi.clear(); return; }
    std::array<juce::AudioBuffer<float>, kChannelOuts> outs;
    std::array<bool, kChannelOuts> outOn{};
    for (int c = 0; c < kChannelOuts; ++c)
        if (c + 1 < getBusCount(false) && getBus(false, c + 1)->isEnabled()) {
            outs[size_t(c)] = getBusBuffer(buffer, false, c + 1);
            outOn[size_t(c)] = outs[size_t(c)].getNumChannels() >= 2;
        }
    events_.clear();
    for (const auto meta : midi)
        if (events_.size() < events_.capacity()) events_.push_back({meta.samplePosition, meta.data, meta.numBytes});
    midi.clear();
    size_t ev = 0;
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        const float* trig[nd2::Engine::kPads] = {};
        for (int c = 0; c < trigCh; ++c) {
            std::copy(inBus.getReadPointer(c) + pos, inBus.getReadPointer(c) + pos + n, trig_[size_t(c)].begin());
            trig[c] = trig_[size_t(c)].data();
        }
        // this chunk's MIDI, with offsets relative to the chunk
        const size_t first = ev;
        while (ev < events_.size() && events_[ev].offset < pos + n) { events_[ev].offset -= pos; ++ev; }
        float* stemOut[nd2::Engine::kStemChannels] = {};
        for (int c = 0; c < kChannelOuts; ++c)
            if (outOn[size_t(c)]) {
                stemOut[2 * c] = outs[size_t(c)].getWritePointer(0) + pos;
                stemOut[2 * c + 1] = outs[size_t(c)].getWritePointer(1) + pos;
            }
        engine_.process(mainBus.getWritePointer(0) + pos, mainBus.getWritePointer(1) + pos, n, events_.data() + first,
                        int(ev - first), trigCh > 0 ? trig : nullptr, stems_ ? stemOut : nullptr);
        for (const auto& e : engine_.hostMidiEvents())   // the OS's MIDI output (only when MIDI to host is on)
            midi.addEvent(engine_.hostMidiBytes() + e.start, int(e.size), e.offset + pos);
    }
    for (int c = 0; c < kChannelOuts; ++c)            // enabled after prepare: silent until the next prepare
        if (outOn[size_t(c)] && !stems_) outs[size_t(c)].clear();
}

juce::AudioProcessorEditor* ND2Processor::createEditor() {
    return new ND2Editor(*this);
}

// State: magic, version, MASTER LEVEL (0-1), window scale, then the machine (gzip; empty if it was not running),
// then (version 2) the bank names (bankfiles::writeRecords).
void ND2Processor::getStateInformation(juce::MemoryBlock& dest) {
    std::vector<uint8_t> machine = engine_.getState();
    if (machine.empty()) machine = pendingState_;    // not started yet: keep the project's machine as it came
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(int(kStateMagic));
    out.writeInt(int(kStateVersion));
    out.writeFloat(engine_.masterLevel());
    out.writeFloat(editorScale.load());
    juce::MemoryBlock packed;
    {
        juce::MemoryOutputStream raw(packed, false);
        juce::GZIPCompressorOutputStream gz(raw, 6);
        gz.write(machine.data(), machine.size());
    }
    out.writeInt64(juce::int64(machine.size()));
    out.writeInt64(juce::int64(packed.getSize()));
    out.write(packed.getData(), packed.getSize());
    bankfiles::writeRecords(out, banks_.records());
    out.writeByte(midiToHost() ? 1 : 0);
    for (auto* p : bends_) out.writeFloat(p->getValue());
}

void ND2Processor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream in(data, size_t(size), false);
    if (uint32_t(in.readInt()) != kStateMagic) return;
    const int version = in.readInt();
    if (version < 1 || version > int(kStateVersion)) return;
    const float master = in.readFloat();
    editorScale = in.readFloat();
    const juce::int64 rawSize = in.readInt64(), packedSize = in.readInt64();
    if (master >= 0.0f && master <= 1.0f) engine_.setMasterLevel(master);
    // an empty machine (saved before the OS was found): the rest of the state still applies
    if (rawSize < 0 || rawSize > (juce::int64(64) << 20) || packedSize <= 0 || packedSize > in.getNumBytesRemaining()) return;
    juce::MemoryBlock packed;
    in.readIntoMemoryBlock(packed, packedSize);
    std::vector<uint8_t> machine(static_cast<size_t>(rawSize));
    if (rawSize > 0) {
        juce::MemoryInputStream packedIn(packed, false);
        juce::GZIPDecompressorInputStream gz(packedIn);
        if (gz.read(machine.data(), int(rawSize)) != int(rawSize)) return;
    }
    bankfiles::BankRecords names{};
    for (auto& b : names) b.crcs.fill(bankfiles::kEmpty);
    if (version >= 2) bankfiles::readRecords(in, names);
    setMidiToHost(version >= 3 && !in.isExhausted() ? in.readByte() != 0 : true);   // older projects: on (as 0.2.1)
    if (version == 4) for (int k = 0; k < kBends && !in.isExhausted(); ++k) in.readByte();   // per-pad bends: gone
    for (auto* p : bends_) {                         // older projects: no bends
        const float v = version >= 5 && in.getNumBytesRemaining() >= 4 ? in.readFloat() : 0.0f;
        p->setValueNotifyingHost(std::isfinite(v) ? juce::jlimit(0.0f, 1.0f, v) : 0.0f);
    }
    banks_.setRecords(names);                        // projects from before 0.1.7: no names (the window finds them)
    projectBanks_ = true;
    std::string err;
    if (machine.empty()) return;
    if (started_) {
        if (engine_.setState(machine, err)) bootedWithoutFactory_ = false;
    } else {
        pendingState_ = std::move(machine);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ND2Processor(); }
