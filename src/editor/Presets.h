#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../core/UnitData.h"

// A preset is a whole chain: the units, their order, the parallel pairs and every control. Same layout as the
// mockup's "saved" object, so export text from the mockup and from the plugin read the same way.
struct PSlot
{
    int type = 0;
    bool on = true, solo = false, mute = false;
    float lane = -6.0f;
    float p[catr::kMaxParams] {};
};
struct PStage
{
    float mix = 100.0f;
    std::vector<PSlot> slots;   // one unit, or two side by side
};
struct Preset
{
    juce::String name, type = "Other", desc;
    bool factory = false;
    float inDb = 0.0f, outDb = 0.0f;
    std::vector<PStage> stages;
    int unitCount() const { int n = 0; for (auto& s : stages) n += (int) s.slots.size(); return n; }
};

namespace presets
{
inline const char* const types[] = { "Vocal", "Drums", "Mix", "Bass", "Kick", "Synth", "Master", "Other" };
inline constexpr int kNumTypes = 8;

const std::vector<Preset>& factory();
juce::File userFolder();
juce::String chainText (const Preset& p);   // "A  >  [B || C]  >  D"

class Store
{
public:
    Store();
    void reload();                                    // read the user folder
    std::vector<Preset> all() const;                  // factory first, then yours
    const std::vector<Preset>& user() const { return mine; }
    const Preset* find (const juce::String& name) const;
    bool save (const Preset& p, const juce::String& replaceName = {}); // writes the file, replaces a same-named one
    bool rename (const juce::String& from, const juce::String& to);
    bool remove (const juce::String& name);
    juce::String exportJson (const std::vector<Preset>& list) const;
    int importJson (const juce::String& text);        // returns count, or -1 when invalid
private:
    std::vector<Preset> mine;
};

bool valid (const juce::var& presetObject);
Preset fromVar (const juce::var& o);
juce::var toVar (const Preset& p);
} // namespace presets
