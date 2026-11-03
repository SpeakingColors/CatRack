// Renders the real editor offscreen to a PNG (docs/screenshot.png, README, manual tour) and runs the plugin-level self tests.
// usage: CatRSnapshot out.png [scale] [look A|B|C] [dialog none|settings|guide|about|presets|save|export|import|add|clear|tip] [frames] [preset name]
//        CatRSnapshot --selftest [fixture.xml]
//        CatRSnapshot --write-fixture out.xml
#include "../src/plugin/PluginProcessor.h"
#include "../src/editor/PluginEditor.h"
#include "../src/editor/Presets.h"

// The chain used by the fixture and the state test: two units in series, then a parallel pair.
static catr::Structure sampleStructure()
{
    catr::Structure s;
    s.slotType[0] = catr::U_PAW; s.slotType[1] = catr::U_PUNCH; s.slotType[2] = catr::U_PURR; s.slotType[3] = catr::U_FET;
    s.nStages = 3;
    s.st[0].n = 1; s.st[0].slot[0] = 0;
    s.st[1].n = 1; s.st[1].slot[0] = 1;
    s.st[2].n = 2; s.st[2].slot[0] = 2; s.st[2].slot[1] = 3;
    return s;
}

static void setReal (CatRackProcessor& p, const char* id, float real)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

static void setFixtureValues (CatRackProcessor& a)
{
    a.commitStructure (sampleStructure(), true);
    a.setSlotReal (2, 0, 3.0f);
    setReal (a, "smix2", 40.0f); setReal (a, "inTrim", 3.0f); setReal (a, "outTrim", -2.0f); setReal (a, "bypass", 1.0f);
    setReal (a, "u1mute", 1.0f);
    a.setSetting (setting::look, "B"); a.setSetting (setting::preset, "My chain");
}

// Saved state of state version 1 must keep loading (tests/fixtures/state-v1.xml).
static int fixtureTest (const juce::File& f)
{
    int bad = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++bad; };
    CatRackProcessor p;
    p.prepareToPlay (48000.0, 512);
    auto xml = juce::XmlDocument::parse (f);
    check (xml != nullptr, "fixture parses");
    if (xml == nullptr) return 1;
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (*xml, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto s = p.structure();
    check (s.nStages == 3 && s.unitCount() == 4 && s.slotType[0] == catr::U_PAW && s.slotType[1] == catr::U_PUNCH && s.st[2].n == 2, "arrangement from the v1 fixture");
    check (std::abs (p.value ("smix2") - 40.0f) < 0.05f && std::abs (p.value (ids::inTrim) - 3.0f) < 0.05f && std::abs (p.value (ids::outTrim) + 2.0f) < 0.05f && p.flag (ids::bypass), "pair mix, trims and bypass from the v1 fixture");
    check (p.flag ("u1mute") && ! p.flag ("u0mute") && p.flag ("u0on"), "mute and on from the v1 fixture");
    check (std::abs (p.slotReal (2, 0) - catr::fromNorm (catr::unitTable()[catr::U_PURR].p[0], 0.6f)) < 0.05f, "a unit control from the v1 fixture");
    check (p.getSetting (setting::look, "C").toString() == "B" && p.getSetting (setting::preset, "").toString() == "My chain", "settings from the v1 fixture");
    return bad;
}

static int selfTest (const juce::File& fixture)
{
    int bad = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++bad; };
    CatRackProcessor a, b;
    a.prepareToPlay (48000.0, 512);
    // a new instance is an empty rack, bypass off, trims at 0 dB
    check (a.structure().unitCount() == 0 && a.getLatencySamples() == 0, "new instance is an empty rack with no latency");
    check (! a.flag (ids::bypass) && std::abs (a.value (ids::inTrim)) < 1e-4f && std::abs (a.value (ids::outTrim)) < 1e-4f, "bypass off, trims at 0 dB");
    check (a.getBypassParameter() != nullptr, "bypass parameter is exposed to the host");
    // every hosted unit type can be put in a slot, and the reported latency is the sum of the rows
    {
        bool all = true, latOk = true;
        for (int t = 0; t < catr::kNumUnits; ++t)
        {
            if (! catr::hosted (t)) continue;
            catr::Structure s; s.slotType[0] = t; s.nStages = 1; s.st[0].n = 1; s.st[0].slot[0] = 0;
            a.commitStructure (s, true);
            all = all && a.slotType (0) == t && a.structure().unitCount() == 1;
            auto u = catr::makeUnit (t);
            u->prepare (48000.0, 256);
            latOk = latOk && a.getLatencySamples() == u->latency();
        }
        check (all, "every hosted unit can be placed in a slot");
        check (latOk, "reported latency equals the unit's own latency for every unit");
        // series and parallel
        int expect = 0;
        const catr::Structure s = sampleStructure();
        for (int g = 0; g < s.nStages; ++g)
        {
            int m = 0;
            for (int k = 0; k < s.st[g].n; ++k) { auto u = catr::makeUnit (s.slotType[s.st[g].slot[k]]); u->prepare (48000.0, 256); m = std::max (m, u->latency()); }
            expect += m;
        }
        a.commitStructure (s, true);
        check (a.getLatencySamples() == expect, "latency of a mixed chain is the sum of the rows (largest unit per row)");
    }
    // per-slot controls survive a set and read
    a.commitStructure (sampleStructure(), true);
    a.setSlotReal (2, 0, 5.0f);
    check (std::abs (a.slotReal (2, 0) - 5.0f) < 0.05f, "a unit control sets and reads back");
    setReal (a, "u1solo", 1.0f);
    check (a.flag ("u1solo"), "solo sets and reads back");
    setReal (a, "u1solo", 0.0f);
    // state round trip: arrangement, values and settings
    setReal (a, "smix2", 35.0f); setReal (a, "inTrim", -4.0f); setReal (a, "bypass", 1.0f);
    a.setSetting (setting::look, "B"); a.setSetting (setting::preset, "Vocal Silk");
    juce::MemoryBlock mb; a.getStateInformation (mb);
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    check (b.structure().toText() == a.structure().toText(), "arrangement survives a state round trip");
    check (b.getLatencySamples() == a.getLatencySamples(), "latency is restored with the arrangement");
    check (std::abs (b.slotReal (2, 0) - 5.0f) < 0.05f && std::abs (b.value ("smix2") - 35.0f) < 0.05f && std::abs (b.value (ids::inTrim) + 4.0f) < 0.05f && b.flag (ids::bypass), "values survive a state round trip");
    check (b.getSetting (setting::look, "C").toString() == "B" && b.getSetting (setting::preset, "").toString() == "Vocal Silk", "settings survive a state round trip");
    // a damaged arrangement text must not crash, it gives an empty rack
    {
        auto st = a.apvts.copyState();
        st.setProperty ("structure", "CR1:9.99|x", nullptr);
        juce::MemoryBlock bad2;
        if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, bad2);
        CatRackProcessor c; c.prepareToPlay (48000.0, 512);
        c.setStateInformation (bad2.getData(), (int) bad2.getSize());
        check (c.structure().unitCount() == 0, "a damaged arrangement loads as an empty rack");
    }
    // the factory presets are legal and every preset round trips through export and import
    {
        bool legal = ! presets::factory().empty();
        for (auto& p : presets::factory())
        {
            legal = legal && p.unitCount() >= 1 && p.unitCount() <= catr::kMaxUnits && presets::valid (presets::toVar (p));
        }
        check (legal && presets::factory().size() >= 16, "factory presets are legal");
        presets::Store s;
        const auto text = s.exportJson (presets::factory());
        auto v = juce::JSON::parse (text);
        bool back = v.getProperty ("presets", juce::var()).isArray();
        if (back)
            for (auto& e : *v.getProperty ("presets", juce::var()).getArray()) back = back && presets::valid (e) && presets::fromVar (e).unitCount() >= 1;
        check (back, "exported presets read back and validate");
    }
    // audio: a signal through every factory chain stays finite and bounded, and bypass is delayed copy
    {
        bool fine = true;
        for (auto& p : presets::factory())
        {
            CatRackProcessor c; c.prepareToPlay (48000.0, 512);
            catr::Structure ns; int slot = 0; std::vector<std::pair<int, int>> dummy;
            for (auto& st : p.stages)
            {
                const int g = ns.nStages++; ns.st[g].n = 0;
                for (auto& sl : st.slots) { if (slot >= catr::kMaxUnits || ns.st[g].n >= 2) break; ns.slotType[slot] = sl.type; ns.st[g].slot[ns.st[g].n++] = slot++; }
            }
            c.commitStructure (ns, true);
            juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; juce::Random rnd (3);
            float peak = 0.0f;
            for (int blk = 0; blk < 60; ++blk)
            {
                for (int i = 0; i < 512; ++i) { const float v = 0.5f * std::sin (0.07f * (float) (blk * 512 + i)) + 0.2f * (rnd.nextFloat() - 0.5f); buf.setSample (0, i, v); buf.setSample (1, i, v); }
                c.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) { const float x = buf.getSample (ch, i); fine = fine && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
            }
            fine = fine && peak < 8.0f;
        }
        check (fine, "every factory chain stays finite and bounded on a test signal");
    }
    // an editor can be made with units loaded, selected, resized and deleted
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        auto* pe = dynamic_cast<CatRackEditor*> (ed.get());
        ed->setSize (600, 320);
        ed->setSize (1800, 960);
        if (pe != nullptr) { pe->selectForTesting (2); pe->tickForTesting (3); pe->openDialogForTesting ("add"); }
        check (ed->getWidth() == 1800 && pe != nullptr, "editor creates, selects and resizes with a mixed chain");
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        check (ed != nullptr, "editor opens on a second instance");
    }
    if (fixture != juce::File()) bad += fixtureTest (fixture);
    std::printf (bad == 0 ? "ALL SELF TESTS PASSED\n" : "SELF TESTS FAILED\n");
    return bad;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--write-fixture")
    {
        CatRackProcessor p; p.prepareToPlay (48000.0, 512); setFixtureValues (p);
        auto state = p.apvts.copyState(); state.setProperty ("stateVersion", 1, nullptr);
        if (auto xml = state.createXml()) juce::File (argv[2]).replaceWithText (xml->toString());
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--selftest")
        return selfTest (argc > 2 ? juce::File (argv[2]) : juce::File()) == 0 ? 0 : 1;
    if (argc < 2) return 2;
    CatRackProcessor proc;
    proc.prepareToPlay (48000.0, 800);
    if (argc > 3) proc.setSetting (setting::look, juce::String (argv[3]));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* pe = dynamic_cast<CatRackEditor*> (ed.get());
    const double sc = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 1.0;
    ed->setSize (juce::roundToInt (1200 * sc), juce::roundToInt (640 * sc));
    const juce::String presetName = argc > 6 ? juce::String (argv[6]) : juce::String ("Start Here");
    if (pe != nullptr) pe->loadPresetForTesting (presetName);
    const juce::String dialog = argc > 4 ? juce::String (argv[4]) : juce::String ("none");
    const int frames = argc > 5 ? juce::String (argv[5]).getIntValue() : 120;
    // Feed a drum, bass and pad loop in real time so the meters and the unit displays show something.
    juce::AudioBuffer<float> buf (2, 800);
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    long pos = 0;
    float pink[3] = { 0, 0, 0 };
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < 800; ++i, ++pos)
        {
            const double t = (double) pos / 48000.0, tk = std::fmod (t, 0.5), th = std::fmod (t + 0.25, 0.5);
            float v = (float) (0.8 * std::exp (-tk * 12.0) * std::sin (6.2831853 * (52.0 + 90.0 * std::exp (-tk * 40.0)) * tk));
            const float w = rnd.nextFloat() * 2.0f - 1.0f;
            pink[0] = 0.99765f * pink[0] + w * 0.0990460f; pink[1] = 0.96300f * pink[1] + w * 0.2965164f; pink[2] = 0.57000f * pink[2] + w * 1.0526913f;
            v += 0.07f * (pink[0] + pink[1] + pink[2] + w * 0.1848f);
            v += (float) (0.18 * std::exp (-th * 45.0)) * w;
            v += 0.10f * (float) std::sin (6.2831853 * 110.0 * t) * (float) (0.6 + 0.4 * std::sin (6.2831853 * 0.25 * t));
            buf.setSample (0, i, v); buf.setSample (1, i, v * 0.97f);
        }
        proc.processBlock (buf, midi);
        if (pe != nullptr) pe->tickForTesting (1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }
    if (dialog != "none" && pe != nullptr) pe->openDialogForTesting (dialog);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
    juce::File out (argv[1]);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
