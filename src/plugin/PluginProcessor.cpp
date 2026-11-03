#include "PluginProcessor.h"
#include "../editor/PluginEditor.h"

using namespace juce;

// ---- SlotParam ---------------------------------------------------------------------------------------
SlotParam::SlotParam (CatRackProcessor& p, int slot, int idx)
    : AudioParameterFloat (ParameterID { "u" + String (slot) + "p" + String (idx), 1 }, "Slot " + String (slot + 1) + " control " + String (idx + 1),
                           NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                           AudioParameterFloatAttributes()
                               .withStringFromValueFunction ([&p, slot, idx] (float v, int) { return p.slotParamText (slot, idx, v); })
                               .withValueFromStringFunction ([&p, slot, idx] (const String& s) { return p.slotParamFromText (slot, idx, s); })),
      proc (p), slotIndex (slot), ctlIndex (idx)
{
}

String SlotParam::getName (int maximumStringLength) const
{
    const String n = proc.slotParamName (slotIndex, ctlIndex);
    return maximumStringLength > 0 ? n.substring (0, maximumStringLength) : n;
}

// ---- layout ------------------------------------------------------------------------------------------
AudioProcessorValueTreeState::ParameterLayout CatRackProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 1;
    auto dbText = [] (float x, int) { return String (x, 1) + " dB"; };
    auto dbParse = [] (const String& s) { return s.getFloatValue(); };
    for (int s = 0; s < catr::kMaxUnits; ++s)
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("slot" + String (s), "Slot " + String (s + 1), " | ");
        for (int k = 0; k < catr::kMaxPd; ++k) grp->addChild (std::make_unique<SlotParam> (*this, s, k));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { slotId (s, "on"), v }, "Slot " + String (s + 1) + " on", true));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { slotId (s, "solo"), v }, "Slot " + String (s + 1) + " solo", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { slotId (s, "mute"), v }, "Slot " + String (s + 1) + " mute", false));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { slotId (s, "lane"), v }, "Slot " + String (s + 1) + " lane level",
                           NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -6.0f,
                           AudioParameterFloatAttributes().withStringFromValueFunction (dbText).withValueFromStringFunction (dbParse)));
        layout.add (std::move (grp));
    }
    for (int g = 0; g < catr::kMaxUnits; ++g)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smix" + String (g), v }, "Row " + String (g + 1) + " pair dry/wet",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + " %"; })
                                                       .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::inTrim, v }, "Input trim", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (dbText).withValueFromStringFunction (dbParse)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::outTrim, v }, "Output trim", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (dbText).withValueFromStringFunction (dbParse)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, v }, "Bypass", false));
    return layout;
}

CatRackProcessor::CatRackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CATRACK", createLayout())
{
    for (int s = 0; s < catr::kMaxUnits; ++s)
    {
        for (int k = 0; k < catr::kMaxPd; ++k)
        {
            slotP[(size_t) s][(size_t) k] = apvts.getParameter ("u" + String (s) + "p" + String (k));
            rawP[(size_t) s][(size_t) k] = apvts.getRawParameterValue ("u" + String (s) + "p" + String (k));
        }
        rawOn[(size_t) s] = apvts.getRawParameterValue (slotId (s, "on"));
        rawSolo[(size_t) s] = apvts.getRawParameterValue (slotId (s, "solo"));
        rawMute[(size_t) s] = apvts.getRawParameterValue (slotId (s, "mute"));
        rawLane[(size_t) s] = apvts.getRawParameterValue (slotId (s, "lane"));
        rawMix[(size_t) s] = apvts.getRawParameterValue ("smix" + String (s));
    }
    rawIn = apvts.getRawParameterValue (ids::inTrim);
    rawOut = apvts.getRawParameterValue (ids::outTrim);
    rawBypass = apvts.getRawParameterValue (ids::bypass);
}

bool CatRackProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == AudioChannelSet::mono() || out == AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

// ---- names and text of the generic slot controls -------------------------------------------------------
String CatRackProcessor::slotParamName (int slot, int idx) const
{
    const int t = slotType (slot);
    if (t < 0) return "Slot " + String (slot + 1) + " control " + String (idx + 1);
    const auto& d = catr::unitTable()[t];
    if (idx >= d.nParams) return "Slot " + String (slot + 1) + " control " + String (idx + 1) + " (unused)";
    return "Slot " + String (slot + 1) + " " + String::fromUTF8 (d.name) + " " + String::fromUTF8 (d.p[idx].label);
}

String CatRackProcessor::slotParamText (int slot, int idx, float norm) const
{
    const int t = slotType (slot);
    if (t < 0 || idx >= catr::unitTable()[t].nParams) return String (norm, 2);
    const auto& d = catr::unitTable()[t].p[idx];
    return String::fromUTF8 (catr::fmtVal (d, catr::fromNorm (d, norm)).c_str());
}

float CatRackProcessor::slotParamFromText (int slot, int idx, const String& text) const
{
    const int t = slotType (slot);
    if (t < 0 || idx >= catr::unitTable()[t].nParams) return jlimit (0.0f, 1.0f, text.getFloatValue());
    const auto& d = catr::unitTable()[t].p[idx];
    for (int i = 0; i < d.nNames; ++i)
        if (text.trim().equalsIgnoreCase (String::fromUTF8 (d.names[i]))) return catr::toNorm (d, (float) i);
    return catr::toNorm (d, text.retainCharacters ("0123456789.-+").getFloatValue());
}

float CatRackProcessor::slotReal (int slot, int idx) const
{
    const int t = slotType (slot);
    if (t < 0 || idx >= catr::unitTable()[t].nParams) return 0.0f;
    return catr::fromNorm (catr::unitTable()[t].p[idx], rawP[(size_t) slot][(size_t) idx]->load());
}

void CatRackProcessor::setSlotReal (int slot, int idx, float real, bool gesture)
{
    const int t = slotType (slot);
    if (t < 0 || idx >= catr::unitTable()[t].nParams) return;
    auto* p = slotP[(size_t) slot][(size_t) idx];
    const float n = catr::toNorm (catr::unitTable()[t].p[idx], real);
    if (gesture) p->beginChangeGesture();
    p->setValueNotifyingHost (n);
    if (gesture) p->endChangeGesture();
}

void CatRackProcessor::setFlag (const char* id, bool v)
{
    auto* p = apvts.getParameter (id);
    p->beginChangeGesture(); p->setValueNotifyingHost (v ? 1.0f : 0.0f); p->endChangeGesture();
}

void CatRackProcessor::setValueReal (const char* id, float real)
{
    auto* p = apvts.getParameter (id);
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture();
}

void CatRackProcessor::resetSlotControls (int slot)
{
    const int t = slotType (slot);
    if (t >= 0)
    {
        const auto& d = catr::unitTable()[t];
        for (int k = 0; k < catr::kMaxPd; ++k)
            slotP[(size_t) slot][(size_t) k]->setValueNotifyingHost (k < d.nParams ? catr::toNorm (d.p[k], d.p[k].def) : 0.5f);
    }
    apvts.getParameter (slotId (slot, "on"))->setValueNotifyingHost (1.0f);
    apvts.getParameter (slotId (slot, "solo"))->setValueNotifyingHost (0.0f);
    apvts.getParameter (slotId (slot, "mute"))->setValueNotifyingHost (0.0f);
    auto* lane = apvts.getParameter (slotId (slot, "lane"));
    lane->setValueNotifyingHost (lane->convertTo0to1 (-6.0f));
}

// ---- structure -------------------------------------------------------------------------------------------
void CatRackProcessor::commitStructure (catr::Structure s, bool defaultsForNewUnits)
{
    const ScopedLock sl (structLock);
    const catr::Structure old = rack.current();
    const catr::Structure applied = rack.applyStructure (s);
    uint64_t packed = 0;
    for (int i = 0; i < catr::kMaxUnits; ++i)
        packed |= (uint64_t) (applied.slotType[i] < 0 ? 31 : applied.slotType[i]) << (i * 5);
    typesPacked.store (packed);
    if (defaultsForNewUnits)
        for (int i = 0; i < catr::kMaxUnits; ++i)
            if (applied.slotType[i] >= 0 && applied.slotType[i] != old.slotType[i]) resetSlotControls (i);
    setLatencySamples (applied.totalLat);
    apvts.state.setProperty ("structure", String (applied.toText()), nullptr);
    structVer.fetch_add (1);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true).withParameterInfoChanged (true));
}

void CatRackProcessor::loadStructureFromState()
{
    catr::Structure s;
    const String text = apvts.state.getProperty ("structure", "CR1:").toString();
    if (! catr::Structure::fromText (text.toStdString(), s)) s = catr::Structure();
    commitStructure (s, false);
}

// ---- settings ----------------------------------------------------------------------------------------------
var CatRackProcessor::getSetting (const char* key, const var& fallback) const
{
    return apvts.state.getProperty (key, fallback);
}

void CatRackProcessor::setSetting (const char* key, const var& v)
{
    apvts.state.setProperty (key, v, nullptr);
}

// ---- audio ----------------------------------------------------------------------------------------------------
void CatRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const ScopedLock sl (structLock);
        rack.prepare (sampleRate, samplesPerBlock);
        setLatencySamples (rack.current().totalLat);
    }
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
}

void CatRackProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numCh = jmin (2, buffer.getNumChannels());
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh == 0) return;

    // controls for this block
    const uint64_t packed = typesPacked.load();
    for (int s = 0; s < catr::kMaxUnits; ++s)
    {
        auto& sc = ctl.slot[s];
        sc.on = rawOn[(size_t) s]->load() > 0.5f;
        sc.solo = rawSolo[(size_t) s]->load() > 0.5f;
        sc.mute = rawMute[(size_t) s]->load() > 0.5f;
        sc.lane = rawLane[(size_t) s]->load();
        ctl.stageMix[s] = rawMix[(size_t) s]->load();
        const int t = (int) ((packed >> (s * 5)) & 31u);
        if (! catr::hosted (t)) continue;
        const auto& d = catr::unitTable()[t];
        for (int k = 0; k < d.nParams && k < catr::kMaxPd; ++k) sc.p[k] = catr::fromNorm (d.p[k], rawP[(size_t) s][(size_t) k]->load());
    }
    ctl.inDb = rawIn->load(); ctl.outDb = rawOut->load(); ctl.bypass = rawBypass->load() > 0.5f;

    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    if (numCh > 1)
    {
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i) { l[i] = catr::finitef (l[i]); r[i] = catr::finitef (r[i]); }
        rack.process (l, r, n, ctl);
    }
    else
    {
        float tl[catr::kChunk], tr[catr::kChunk];
        for (int off = 0; off < n; off += catr::kChunk)
        {
            const int m = jmin (catr::kChunk, n - off);
            for (int i = 0; i < m; ++i) tl[i] = tr[i] = catr::finitef (l[off + i]);
            rack.process (tl, tr, m, ctl);
            for (int i = 0; i < m; ++i) l[off + i] = tl[i];
        }
    }
}

AudioProcessorEditor* CatRackProcessor::createEditor()
{
    return new CatRackEditor (*this);
}

void CatRackProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void CatRackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml)); // unknown keys are ignored by APVTS; older states keep loading
            loadStructureFromState();
            restored = true;
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CatRackProcessor(); }
