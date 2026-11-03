#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/Rack.h"
#include "../core/Fmt.h"
#include <array>
#include <atomic>

// Stable parameter IDs. Never rename these (saved projects and automation use them).
//   u{slot}p{0..5}  the six controls of the unit in a slot, normalised 0..1 (the unit decides the meaning)
//   u{slot}on / u{slot}solo / u{slot}mute / u{slot}lane   per slot
//   smix{row}       Dry/Wet of a parallel pair, per row
//   inTrim, outTrim, bypass
namespace ids
{
    inline constexpr const char* inTrim = "inTrim";
    inline constexpr const char* outTrim = "outTrim";
    inline constexpr const char* bypass = "bypass";
}

// Plugin-wide settings kept in the saved state (not presets, not automatable).
namespace setting
{
    inline constexpr const char* look = "look";       // "A", "B", "C"
    inline constexpr const char* tips = "tips";       // info bubbles
    inline constexpr const char* power = "power";     // power saving redraw
    inline constexpr const char* scale = "scale";     // window scale, 1.0 = 100 %
    inline constexpr const char* preset = "preset";   // name of the loaded preset
    inline constexpr const char* sel = "sel";         // selected slot
}

class CatRackProcessor;

// One of the six generic unit controls. Its name and text come from the unit that is in the slot right now.
class SlotParam : public juce::AudioParameterFloat
{
public:
    SlotParam (CatRackProcessor& p, int slot, int idx);
    juce::String getName (int maximumStringLength) const override;
private:
    CatRackProcessor& proc;
    int slotIndex, ctlIndex;
};

class CatRackProcessor : public juce::AudioProcessor
{
public:
    CatRackProcessor();
    ~CatRackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); } // the rack bypasses itself, delay matched

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CatRack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ids::bypass); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    catr::Rack rack;

    // ---- structure (message thread) ----
    catr::Structure structure() const { return rack.current(); }
    // Apply a new arrangement of units. Slots that got a different unit start from that unit's defaults.
    void commitStructure (catr::Structure s, bool defaultsForNewUnits = true);
    int structureVersion() const { return structVer.load(); }
    int slotType (int slot) const { const int t = (int) ((typesPacked.load() >> (slot * 5)) & 31u); return t == 31 ? -1 : t; }

    // ---- control access (message thread) ----
    juce::RangedAudioParameter* slotParam (int slot, int idx) const { return slotP[(size_t) slot][(size_t) idx]; }
    float slotReal (int slot, int idx) const;
    void setSlotReal (int slot, int idx, float real, bool gesture = true);
    void resetSlotControls (int slot);                  // unit defaults, on, no solo, no mute, lane -6
    juce::String slotParamName (int slot, int idx) const;
    juce::String slotParamText (int slot, int idx, float norm) const;
    float slotParamFromText (int slot, int idx, const juce::String& text) const;
    bool flag (const char* id) const { return apvts.getRawParameterValue (id)->load() > 0.5f; }
    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setFlag (const char* id, bool v);
    void setValueReal (const char* id, float real);

    // Settings stored in the state tree.
    juce::var getSetting (const char* key, const juce::var& fallback) const;
    void setSetting (const char* key, const juce::var& v);

    int reportedLatency() const { return getLatencySamples(); }

private:
    static juce::String slotId (int slot, const char* tail) { return "u" + juce::String (slot) + tail; }
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadStructureFromState();

    juce::CriticalSection structLock;
    std::atomic<uint64_t> typesPacked { ~(uint64_t) 0 };
    std::atomic<int> structVer { 0 };

    std::array<std::array<juce::RangedAudioParameter*, catr::kMaxPd>, catr::kMaxUnits> slotP {};
    std::array<std::array<std::atomic<float>*, catr::kMaxPd>, catr::kMaxUnits> rawP {};
    std::array<std::atomic<float>*, catr::kMaxUnits> rawOn {}, rawSolo {}, rawMute {}, rawLane {}, rawMix {};
    std::atomic<float>* rawIn = nullptr; std::atomic<float>* rawOut = nullptr; std::atomic<float>* rawBypass = nullptr;
    catr::Ctl ctl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatRackProcessor)
};
