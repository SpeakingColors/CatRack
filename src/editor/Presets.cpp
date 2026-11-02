#include "Presets.h"
#include "FactoryData.h"
#include "../core/Rack.h"
using namespace juce;

namespace presets
{
static int typeOfKey (const String& key)
{
    for (int i = 0; i < catr::kNumUnits; ++i)
        if (key == catr::unitTable()[i].key) return i;
    return -1;
}

static PSlot makeSlot (int type, const var& over)
{
    PSlot s; s.type = type;
    const auto& d = catr::unitTable()[type];
    for (int k = 0; k < 6; ++k) s.p[k] = k < d.nParams ? d.p[k].def : 0.0f;
    if (auto* o = over.getDynamicObject())
        for (int k = 0; k < d.nParams; ++k)
            if (o->hasProperty (d.p[k].id))
                s.p[k] = jlimit (d.p[k].mn, d.p[k].mx, (float) (double) o->getProperty (d.p[k].id));
    return s;
}

const std::vector<Preset>& factory()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> r;
        int n = 0;
        const char* const* js = catr::factoryChainsJson (n);
        for (int i = 0; i < n; ++i)
        {
            const var o = JSON::parse (String::fromUTF8 (js[i]));
            Preset p; p.factory = true;
            p.name = o["name"].toString(); p.type = o["type"].toString(); p.desc = o["desc"].toString();
            bool ok = true;
            if (auto* chain = o["chain"].getArray())
                for (auto& c : *chain)
                {
                    PStage st;
                    if (auto* par = c["par"].getArray())
                    {
                        st.mix = (float) (double) c["mix"];
                        for (auto& a : *par)
                        {
                            const int t = typeOfKey (a[0].toString());
                            if (t < 0 || t >= catr::kBuiltUnits) ok = false; else st.slots.push_back (makeSlot (t, a[1]));
                        }
                    }
                    else
                    {
                        const int t = typeOfKey (c["u"].toString());
                        if (t < 0 || t >= catr::kBuiltUnits) ok = false; else st.slots.push_back (makeSlot (t, c["o"]));
                    }
                    if (! st.slots.empty()) p.stages.push_back (st);
                }
            if (ok && ! p.stages.empty()) r.push_back (p);
        }
        return r;
    }();
    return list;
}

String chainText (const Preset& p)
{
    StringArray parts;
    for (auto& g : p.stages)
    {
        StringArray names;
        for (auto& s : g.slots) names.add (String::fromUTF8 (catr::unitTable()[s.type].name));
        parts.add (g.slots.size() > 1 ? "[" + names.joinIntoString (" || ") + "]" : names[0]);
    }
    return parts.joinIntoString ("  >  ");
}

File userFolder()
{
   #if JUCE_LINUX || JUCE_BSD
    const String xdg = SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const File base = (xdg.isNotEmpty() && File::isAbsolutePath (xdg)) ? File (xdg)
                                                                       : File::getSpecialLocation (File::userHomeDirectory).getChildFile (".local/share");
   #else
    const File base = File::getSpecialLocation (File::userApplicationDataDirectory); // %APPDATA% or ~/Library/Application Support
   #endif
    return base.getChildFile ("Studio 87").getChildFile ("CatRack").getChildFile ("presets");
}

bool valid (const var& o)
{
    auto* obj = o.getDynamicObject();
    if (obj == nullptr) return false;
    const var name = obj->getProperty ("name"), type = obj->getProperty ("type"), saved = obj->getProperty ("saved");
    if (! name.isString() || name.toString().isEmpty() || name.toString().length() > 40 || ! type.isString()) return false;
    auto* so = saved.getDynamicObject();
    if (so == nullptr) return false;
    auto* io = so->getProperty ("io").getArray();
    auto* stages = so->getProperty ("stages").getArray();
    if (io == nullptr || io->size() != 2 || stages == nullptr || stages->isEmpty() || stages->size() > catr::kMaxUnits) return false;
    int n = 0;
    for (auto& g : *stages)
    {
        auto* slots = g["slots"].getArray();
        if (slots == nullptr || slots->isEmpty() || slots->size() > 2) return false;
        if (! (g["mix"].isDouble() || g["mix"].isInt() || g["mix"].isInt64())) return false;
        for (auto& s : *slots)
        {
            const int t = typeOfKey (s["t"].toString());
            if (t < 0 || t >= catr::kBuiltUnits) return false;   // a unit this version does not host: refuse
            auto* pa = s["p"].getArray();
            if (pa == nullptr || pa->size() != catr::unitTable()[t].nParams) return false;
            for (auto& v : *pa)
                if (! (v.isDouble() || v.isInt() || v.isInt64()) || ! std::isfinite ((double) v)) return false;
            if (! (s["lane"].isDouble() || s["lane"].isInt() || s["lane"].isInt64())) return false;
            ++n;
        }
    }
    return n >= 1 && n <= catr::kMaxUnits;
}

Preset fromVar (const var& o)
{
    Preset p;
    p.name = o["name"].toString().substring (0, 40);
    const String t = o["type"].toString();
    p.type = "Other";
    for (auto* ty : types) if (t == ty) p.type = t;
    p.desc = o["desc"].toString().substring (0, 200);
    const var sv = o["saved"];
    if (auto* io = sv["io"].getArray(); io != nullptr && io->size() == 2)
    {
        p.inDb = jlimit (-12.0f, 12.0f, (float) (double) (*io)[0]);
        p.outDb = jlimit (-12.0f, 12.0f, (float) (double) (*io)[1]);
    }
    if (auto* stages = sv["stages"].getArray())
        for (auto& g : *stages)
        {
            PStage st; st.mix = jlimit (0.0f, 100.0f, (float) (double) g["mix"]);
            if (auto* slots = g["slots"].getArray())
                for (auto& s : *slots)
                {
                    const int ty = typeOfKey (s["t"].toString());
                    if (ty < 0 || ty >= catr::kBuiltUnits) continue;
                    PSlot ps = makeSlot (ty, var());
                    ps.on = (bool) s["on"]; ps.solo = (bool) s["solo"]; ps.mute = (bool) s["mute"];
                    ps.lane = jlimit (-24.0f, 6.0f, (float) (double) s["lane"]);
                    if (auto* pa = s["p"].getArray())
                        for (int k = 0; k < pa->size() && k < catr::unitTable()[ty].nParams; ++k)
                            ps.p[k] = jlimit (catr::unitTable()[ty].p[k].mn, catr::unitTable()[ty].p[k].mx, (float) (double) (*pa)[k]);
                    st.slots.push_back (ps);
                }
            if (! st.slots.empty()) p.stages.push_back (st);
        }
    return p;
}

var toVar (const Preset& p)
{
    auto* o = new DynamicObject();
    o->setProperty ("name", p.name);
    o->setProperty ("type", p.type);
    o->setProperty ("desc", p.desc);
    auto* sv = new DynamicObject();
    Array<var> io; io.add (p.inDb); io.add (p.outDb);
    sv->setProperty ("io", io);
    Array<var> stages;
    for (auto& g : p.stages)
    {
        auto* go = new DynamicObject();
        go->setProperty ("mix", g.mix);
        Array<var> slots;
        for (auto& s : g.slots)
        {
            auto* so = new DynamicObject();
            const auto& d = catr::unitTable()[s.type];
            so->setProperty ("t", String (d.key));
            so->setProperty ("on", s.on); so->setProperty ("solo", s.solo); so->setProperty ("mute", s.mute);
            so->setProperty ("lane", s.lane);
            Array<var> pa; for (int k = 0; k < d.nParams; ++k) pa.add (s.p[k]);
            so->setProperty ("p", pa);
            slots.add (var (so));
        }
        go->setProperty ("slots", slots);
        stages.add (var (go));
    }
    sv->setProperty ("stages", stages);
    o->setProperty ("saved", var (sv));
    return var (o);
}

static String fileNameFor (const String& name)
{
    String s = name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-+.");
    s = s.trim().replaceCharacters (" ", "_");
    if (s.isEmpty()) s = "preset";
    return s.substring (0, 40) + "-" + String::toHexString (name.hashCode()) + ".json";
}

Store::Store() { reload(); }

void Store::reload()
{
    mine.clear();
    auto dir = userFolder();
    if (! dir.isDirectory()) return;
    for (auto& f : dir.findChildFiles (File::findFiles, false, "*.json"))
    {
        if (f.getSize() > 60000) continue;
        const var o = JSON::parse (f);
        if (! valid (o)) continue;
        Preset p = fromVar (o);
        if (p.stages.empty()) continue;
        bool clash = false;
        for (auto& x : factory()) if (x.name == p.name) clash = true;
        for (auto& x : mine) if (x.name == p.name) clash = true;
        if (! clash) mine.push_back (p);
    }
    std::sort (mine.begin(), mine.end(), [] (const Preset& a, const Preset& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

std::vector<Preset> Store::all() const
{
    std::vector<Preset> r = factory();
    r.insert (r.end(), mine.begin(), mine.end());
    return r;
}

const Preset* Store::find (const String& name) const
{
    for (auto& p : factory()) if (p.name == name) return &p;
    for (auto& p : mine) if (p.name == name) return &p;
    return nullptr;
}

bool Store::save (const Preset& p, const String& replaceName)
{
    auto dir = userFolder();
    if (! dir.createDirectory().wasOk()) return false;
    const String old = replaceName.isEmpty() ? p.name : replaceName;
    for (auto& f : dir.findChildFiles (File::findFiles, false, "*.json"))
    {
        const var o = JSON::parse (f);
        if (o["name"].toString() == old || o["name"].toString() == p.name) f.deleteFile();
    }
    Preset q = p; q.factory = false;
    if (! dir.getChildFile (fileNameFor (q.name)).replaceWithText (JSON::toString (toVar (q), false))) return false;
    reload();
    return true;
}

bool Store::rename (const String& from, const String& to)
{
    const Preset* p = find (from);
    if (p == nullptr || p->factory) return false;
    Preset q = *p; q.name = to;
    return save (q, from);
}

bool Store::remove (const String& name)
{
    bool ok = false;
    for (auto& f : userFolder().findChildFiles (File::findFiles, false, "*.json"))
        if (JSON::parse (f)["name"].toString() == name) ok = f.deleteFile() || ok;
    reload();
    return ok;
}

String Store::exportJson (const std::vector<Preset>& list) const
{
    auto* root = new DynamicObject();
    root->setProperty ("catrack", 1);
    Array<var> arr; for (auto& p : list) arr.add (toVar (p));
    root->setProperty ("presets", arr);
    return JSON::toString (var (root), false);
}

int Store::importJson (const String& text)
{
    const var o = JSON::parse (text);
    if (! o.isObject() || (int) o["catrack"] != 1) return -1;
    auto* arr = o["presets"].getArray();
    if (arr == nullptr || arr->isEmpty() || arr->size() > 200) return -1;
    for (auto& x : *arr) if (! valid (x)) return -1;
    for (auto& x : *arr)
    {
        Preset p = fromVar (x);
        auto taken = [this] (const String& n) { return find (n) != nullptr; };
        while (taken (p.name)) p.name = (p.name + " (imported)").substring (0, 40);
        save (p);
    }
    return arr->size();
}
} // namespace presets
