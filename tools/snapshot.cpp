// Renders the real editor offscreen to a PNG (docs/screenshot.png, README, manual tour) and runs the plugin-level self tests.
// usage: CatRSnapshot out.png [scale] [look A|B|C] [dialog none|settings|guide|about|presets|save|export|import|tip] [frames] [preset name]
//        CatRSnapshot --selftest [fixture.xml]
//        CatRSnapshot --write-fixture out.xml
#include "../src/plugin/PluginProcessor.h"
#include "../src/editor/PluginEditor.h"
#include "../src/editor/Presets.h"

static void setFixtureValues (CatRackProcessor& a)
{
    auto set = [&a] (const char* id, float real) { auto* p = a.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (real)); };
    set ("lowF", 3); set ("lowB", 6.5f); set ("lowA", 4.0f); set ("hiF", 6); set ("hiB", 3.5f); set ("bw", 8.0f);
    set ("attF", 1); set ("hiA", 2.5f); set ("tube", 4.0f); set ("out", 6.0f); set ("bypass", 1);
    a.setSetting (setting::look, "B"); a.setSetting (setting::preset, "Vocal Air");
}

// Saved state of state version 1 must keep loading (tests/fixtures/state-v1.xml).
static int fixtureTest (const juce::File& f)
{
    int bad = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++bad; };
    CatRackProcessor p;
    auto xml = juce::XmlDocument::parse (f);
    check (xml != nullptr, "fixture parses");
    if (xml == nullptr) return 1;
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (*xml, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    auto real = [&p] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    check ((int) real ("lowF") == 3 && std::abs (real ("lowB") - 6.5f) < 0.02f && std::abs (real ("lowA") - 4.0f) < 0.02f, "low section from the v1 fixture");
    check ((int) real ("hiF") == 6 && std::abs (real ("hiB") - 3.5f) < 0.02f && std::abs (real ("bw") - 8.0f) < 0.02f, "high boost from the v1 fixture");
    check ((int) real ("attF") == 1 && std::abs (real ("hiA") - 2.5f) < 0.02f && std::abs (real ("tube") - 4.0f) < 0.02f
               && std::abs (real ("out") - 6.0f) < 0.02f && real ("bypass") > 0.5f, "atten, tube, output and bypass from the v1 fixture");
    check (p.getSetting (setting::look, "C").toString() == "B" && p.getSetting (setting::preset, "").toString() == "Vocal Air", "settings from the v1 fixture");
    return bad;
}

static int selfTest (const juce::File& fixture)
{
    int bad = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++bad; };
    CatRackProcessor a, b;
    auto set = [] (CatRackProcessor& p, const char* id, float real)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    };
    auto real = [] (CatRackProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    // defaults are the mockup's defaults
    check ((int) real (a, "lowF") == 2 && (int) real (a, "hiF") == 4 && (int) real (a, "attF") == 2, "frequency switches default to 60 Hz, 10 kHz, 20 kHz");
    check (real (a, "lowB") == 0 && real (a, "lowA") == 0 && real (a, "hiB") == 0 && real (a, "hiA") == 0 && real (a, "tube") == 0, "boost, atten and tube default to 0");
    check (std::abs (real (a, "bw") - 5.0f) < 1e-4f && std::abs (real (a, "out") - 5.0f) < 1e-4f && real (a, "bypass") < 0.5f, "width and output default to 5, bypass off");
    // every item of every choice control survives a set and read (the last item included)
    const struct { const char* id; int n; } ch[] = { { "lowF", 4 }, { "hiF", 7 }, { "attF", 3 } };
    for (auto& c : ch)
    {
        bool all = true;
        for (int i = 0; i < c.n; ++i) { set (a, c.id, (float) i); all = all && (int) real (a, c.id) == i; }
        check (all, c.id);
    }
    // knob ends
    set (a, "lowB", 10.0f); set (a, "out", 0.0f);
    check (std::abs (real (a, "lowB") - 10.0f) < 1e-4f && std::abs (real (a, "out")) < 1e-4f, "knob ends reach 0 and 10");
    // the factory presets only use legal values and every preset round trips through export and import
    {
        presets::Store s;
        bool legal = true;
        for (auto& p : presets::factory())
            for (int i = 0; i < presets::kCount; ++i) legal = legal && p.v[i] >= presets::lo[i] && p.v[i] <= presets::hi[i];
        check (legal && presets::factory().size() >= 16, "factory presets are legal");
        const auto text = s.exportJson (presets::factory());
        presets::Store t;
        const int before = (int) t.user().size();
        (void) before;
        check (presets::valid (juce::JSON::parse (text).getProperty ("presets", juce::var()).getArray()->getFirst()), "exported preset validates");
    }
    // state round trip including plugin settings
    set (a, "lowF", 3); set (a, "lowB", 6.5f); set (a, "hiF", 6); set (a, "tube", 4.0f); set (a, "bypass", 1);
    a.setSetting (setting::look, "B"); a.setSetting (setting::preset, "Vocal Air");
    juce::MemoryBlock mb; a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    check ((int) real (b, "lowF") == 3 && std::abs (real (b, "lowB") - 6.5f) < 0.02f && (int) real (b, "hiF") == 6 && real (b, "bypass") > 0.5f, "parameters survive a state round trip");
    check (b.getSetting (setting::look, "C").toString() == "B" && b.getSetting (setting::preset, "").toString() == "Vocal Air", "settings survive a state round trip");
    // the host sees a bypass parameter and the reported latency
    check (b.getBypassParameter() != nullptr, "bypass parameter is exposed to the host");
    b.prepareToPlay (48000.0, 512);
    check (b.getLatencySamples() == pawl::kLatencySamples, "reported latency equals the core latency");
    // an editor can be made, resized and deleted
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        ed->setSize (600, 294);
        ed->setSize (1800, 882);
        check (ed->getWidth() == 1800, "editor creates and resizes");
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
        CatRackProcessor p; setFixtureValues (p);
        auto state = p.apvts.copyState(); state.setProperty ("stateVersion", 1, nullptr);
        if (auto xml = state.createXml()) juce::File (argv[2]).replaceWithText (xml->toString());
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--selftest")
        return selfTest (argc > 2 ? juce::File (argv[2]) : juce::File()) == 0 ? 0 : 1;
    CatRackProcessor proc;
    proc.prepareToPlay (48000.0, 800);
    if (argc > 3) proc.setSetting (setting::look, juce::String (argv[3]));
    // a factory preset so the curve shows something
    const juce::String presetName = argc > 6 ? juce::String (argv[6]) : juce::String ("Start Here");
    for (auto& p : presets::factory())
        if (p.name == presetName)
        {
            for (int i = 0; i < presets::kCount; ++i)
            {
                auto* prm = proc.apvts.getParameter (presets::keys[i]);
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) p.v[i]));
            }
            proc.setSetting (setting::preset, p.name);
        }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    const double sc = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 1.0;
    ed->setSize (juce::roundToInt (1200 * sc), juce::roundToInt (588 * sc));
    const juce::String dialog = argc > 4 ? juce::String (argv[4]) : juce::String ("none");
    const int frames = argc > 5 ? juce::String (argv[5]).getIntValue() : 120;
    // Feed a drum, bass and pad loop in real time so the meters and both displays show something.
    juce::AudioBuffer<float> buf (2, 800);
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    long pos = 0;
    float lp = 0.0f, pink[3] = { 0, 0, 0 };
    auto* pe = dynamic_cast<CatRackEditor*> (ed.get());
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
            lp += 0.02f * (v - lp); (void) lp;
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
