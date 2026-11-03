#include "PluginEditor.h"
#include "Widgets.h"
#include "Views.h"
#include "Panels.h"
#include "Dialogs.h"
#include "Presets.h"

using namespace juce;
using namespace ui;

namespace
{
// ---- the wood cheeks: same three colours and vertical grain as PurrOne ---------------------------
struct Mul32
{
    uint32_t a;
    double next()
    {
        a += 0x6D2B79F5u;
        uint32_t t = a ^ (a >> 15);
        t *= (1u | a);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
};

Image makeWood (uint32_t seed, bool edgeRight)
{
    const int w = 104, h = 1280;
    Image img (Image::ARGB, w, h, true);
    Graphics g (img);
    Mul32 r { seed };
    g.fillAll (Colour (0xff3d2210));
    for (int x = 0; x < w; ++x)
    {
        const double a = r.next();
        g.setColour (Colour::fromFloatRGBA (123 / 255.0f, 74 / 255.0f, 38 / 255.0f, (float) (0.40 * (0.4 + 0.6 * a))));
        g.fillRect (x, 0, 1, h);
        if (r.next() < 0.35)
        {
            const float al = (float) (0.75 * r.next());
            const double y0 = r.next() * h;
            const double len = h * (0.2 + 0.6 * r.next());
            g.setColour (Colour::fromFloatRGBA (90 / 255.0f, 52 / 255.0f, 25 / 255.0f, al));
            g.fillRect ((float) x, (float) y0, 1.0f, (float) len);
        }
        if (r.next() < 0.12)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRect (x, 0, 1, h);
        }
    }
    g.setColour (Colours::black);
    g.fillRect (edgeRight ? 0 : w - 3, 0, 3, h);
    return img;
}

std::unique_ptr<Drawable> makeLogo()
{
    static const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><path d="M8 6 L24 22 H40 L56 6 L60 36 Q60 58 32 60 Q4 58 4 36 Z" fill="#FF7BB1"/><path d="M12 14 L21 23 H14 Z M52 14 L43 23 H50 Z" fill="#FBBDD0"/><ellipse cx="22" cy="36" rx="4" ry="5" fill="#16171a"/><ellipse cx="42" cy="36" rx="4" ry="5" fill="#16171a"/><path d="M29 44 H35 L32 48 Z" fill="#16171a"/><path d="M32 48 Q28 53 24 50 M32 48 Q36 53 40 50" stroke="#16171a" stroke-width="2" fill="none"/><path d="M2 40 H14 M2 46 H14 M62 40 H50 M62 46 H50" stroke="#FBBDD0" stroke-width="1.6"/></svg>)";
    if (auto xml = XmlDocument::parse (svg)) return Drawable::createFromSVG (*xml);
    return {};
}

class Cheek : public Component, public TipClient
{
public:
    Cheek (Image i, std::function<void()> reset) : img (std::move (i)), onReset (std::move (reset)) {}
    void paint (Graphics& g) override
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (img, getLocalBounds().toFloat());
    }
    void mouseDoubleClick (const MouseEvent&) override { if (onReset) onReset(); }
private:
    Image img;
    std::function<void()> onReset;
};

struct Delay : Timer
{
    std::function<void()> fn;
    void start (int ms) { startTimer (ms); }
    void timerCallback() override { stopTimer(); if (fn) fn(); }
};

class Bubble : public Component
{
public:
    Bubble (const Ctx& c) : ctx (c) { setInterceptsMouseClicks (false, false); setVisible (false); }
    void showAt (const String& s, Rectangle<int> target, int parentW, int parentH)
    {
        text = s;
        AttributedString as = build();
        TextLayout tl; tl.createLayout (as, 282.0f);
        const int tw = (int) std::ceil (std::min (282.0f, tl.getWidth())) + 20, th = (int) std::ceil (tl.getHeight()) + 14;
        int x = jlimit (60, std::max (60, parentW - tw - 60), target.getX());
        int y = target.getBottom() + 6;
        if (y + th > parentH - 4) y = target.getY() - th - 6;
        setBounds (x, y, tw, th);
        setVisible (true);
        toFront (false);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        build().draw (g, r.reduced (10.0f, 7.0f));
    }
private:
    AttributedString build() const
    {
        AttributedString as; as.setWordWrap (AttributedString::byWord); as.setLineSpacing (2.0f);
        as.append (text, theme::font (theme::FontKind::regular, 14.0f), ctx.t().ink);
        return as;
    }
    const Ctx& ctx;
    String text;
};

class Toast : public Component
{
public:
    Toast (const Ctx&) { setInterceptsMouseClicks (false, false); setVisible (false); done.fn = [this] { setVisible (false); }; }
    void say (const String& s, int parentW, int parentH)
    {
        text = s;
        const int w = (int) textWidth (theme::font (theme::FontKind::regular, 15.0f), s) + 30;
        setBounds ((parentW - w) / 2, parentH - 34 - 28, w, 28);
        setVisible (true); toFront (false); repaint();
        done.start (1600);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        drawText (g, text, r, theme::font (theme::FontKind::regular, 15.0f), Colours::white);
    }
private:
    String text;
    Delay done;
};


// The static faceplate: wood-free parts (header strip, brand, meter column, footer). Drawn once into an image;
// it repaints only when the look changes.
class Backdrop : public Component
{
public:
    explicit Backdrop (const Ctx& c) : ctx (c), logo (makeLogo())
    {
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }
    static float nameWidth() { return textWidth (theme::font (theme::FontKind::brand, 26.0f), "CatRack") + 0.5f * 7.0f; }
    static float brandEnd() { return 64.0f + 34.0f + 8.0f + nameWidth() + 8.0f + 59.78f; }
    void paint (Graphics& g) override
    {
        using namespace layout;
        auto& t = ctx.t();
        g.fillAll (Colours::black);
        g.setColour (t.plate); g.fillRect (mainX, 0.0f, mainW, (float) H);
        // header
        g.setColour (t.plate2); g.fillRect (mainX, 0.0f, mainW, hdrH);
        g.setColour (t.line); g.fillRect (mainX, hdrH - 1.0f, mainW, 1.0f);
        if (logo) logo->drawWithin (g, { 64.0f, 5.5f, 34.0f, 34.0f }, RectanglePlacement::centred, 1.0f);
        drawText (g, "CatRack", { 106.0f, 8.5f, nameWidth(), 28.0f }, theme::font (theme::FontKind::brand, 26.0f), Colours::white, Justification::centredLeft, 0.5f);
        drawText (g, "STUDIO 87", { 106.0f + nameWidth() + 8.0f, 15.5f, 59.78f, 14.0f }, theme::font (theme::FontKind::regular, 12.0f), t.dim, Justification::centredLeft, 2.0f);
        // meter column
        g.setColour (t.plate2); g.fillRect (metX, bodyY, metW, bodyH);
        g.setColour (t.line); g.fillRect (metX, bodyY, 1.0f, bodyH);
        // footer
        g.setColour (t.plate2); g.fillRect (mainX, footY, mainW, footH);
        g.setColour (t.line); g.fillRect (mainX, footY, mainW, 1.0f);
        drawText (g, String ("Studio 87 ") + u8 ("\xc2\xb7") + " CatRack v" + JucePlugin_VersionString, { 64.0f, 619.5f, 400.0f, 14.0f },
                  theme::font (theme::FontKind::regular, 12.0f), t.dim, Justification::centredLeft, 1.0f);
    }
private:
    const Ctx& ctx;
    std::unique_ptr<Drawable> logo;
};

// The window size in the footer and the "N of 12 units" text next to the toolbar buttons.
class Readouts : public Component
{
public:
    explicit Readouts (const Ctx& c) : ctx (c) { setInterceptsMouseClicks (false, false); }
    String sizeText, countText;
    float countX = 200.0f;
    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        drawText (g, sizeText, { 1136.0f - 200.0f, 619.5f, 200.0f, 14.0f }, theme::font (theme::FontKind::regular, 12.0f), t.dim, Justification::centredRight, 1.0f);
        drawText (g, countText, { countX, layout::toolbar.y, 200.0f, layout::toolbar.h }, theme::font (theme::FontKind::regular, 13.0f), t.dim, Justification::centredLeft, 1.0f);
    }
private:
    const Ctx& ctx;
};
} // namespace

// =====================================================================================================
class Face : public Component, public DialogHost, public RackHost, private Timer
{
public:
    Face (CatRackProcessor& p, std::function<void()> resetSize)
        : proc_ (p), onResetSize (std::move (resetSize)),
          backdrop (ctx), strip (*this), rowsArea (*this), detail (*this), meter (ctx), readouts (ctx),
          bubble (ctx), toastView (ctx), modal (*this)
    {
        const String lk = proc_.getSetting (setting::look, "C").toString();
        ctx.look = lk == "A" ? 'A' : lk == "B" ? 'B' : 'C';
        ctx.tips = (bool) proc_.getSetting (setting::tips, true);
        ctx.power = (bool) proc_.getSetting (setting::power, false);
        dirtyFlag = (bool) proc_.getSetting ("dirty", false);
        sel = (int) proc_.getSetting (setting::sel, -1);
        setSize (layout::W, layout::H);
        setWantsKeyboardFocus (true);

        addAndMakeVisible (backdrop);
        backdrop.setBounds (0, 0, layout::W, layout::H);
        addAndMakeVisible (cheekL = new Cheek (makeWood (11, true), onResetSize));
        addAndMakeVisible (cheekR = new Cheek (makeWood (29, false), onResetSize));
        cheekL->setBounds (0, 0, 52, layout::H);
        cheekR->setBounds (1148, 0, 52, layout::H);
        cheekL->tip = layout::tipCheek; cheekR->tip = layout::tipCheek;

        auto* brand = new TipArea(); owned.add (brand);
        addAndMakeVisible (brand);
        brand->setBounds (64, 5, roundToInt (Backdrop::brandEnd() - 64.0f), 34);
        brand->tip = layout::tipBrand;

        buildHeader();
        buildRack();

        addAndMakeVisible (meter);
        meter.setBounds (1060, 54, 84, 534);
        meter.onClear = [this] { toast ("Peaks and clip lights cleared"); };
        addAndMakeVisible (readouts);
        readouts.setBounds (0, 0, layout::W, layout::H);

        addAndMakeVisible (bubble);
        addAndMakeVisible (toastView);
        addChildComponent (modal);
        addMouseListener (this, true);
        tipTimer.fn = [this] { showTip(); };
        validateSelection();
        rebuildAll();
        seenVer = proc_.structureVersion();
        tick (1.0f / 60.0f);
        syncName();
        if (! (bool) proc_.getSetting ("started", false))      // a brand new instance opens with the Start Here chain, as the mockup does
        {
            proc_.setSetting ("started", true);
            if (! proc_.restoredFromHost() && proc_.structure().unitCount() == 0)
                if (const Preset* sh = presetStore.find ("Start Here")) loadPreset (*sh);
        }
        startTimerHz (ctx.power ? 16 : 60);
    }
    ~Face() override { stopTimer(); removeMouseListener (this); }

    void openForTesting (const String& n)
    {
        if (n == "settings") modal.openSettings();
        else if (n == "guide") modal.openGuide();
        else if (n == "about") modal.openAbout();
        else if (n == "presets") modal.openBrowser();
        else if (n == "save") modal.openSave (nullptr);
        else if (n == "export") modal.openExport (nullptr);
        else if (n == "import") modal.openImport();
        else if (n == "add") modal.openAdd();
        else if (n == "clear") modal.openClear();
        else if (n == "tip") { tipHost = &rowsArea; tipText = "Drag to move this unit. Drop on the top or bottom edge of a row to put it before or after."; showTip(); }
    }
    void tickForTesting (int frames) { for (int i = 0; i < frames; ++i) tick (1.0f / 60.0f); }
    void loadByName (const String& n)
    {
        if (auto* p = presetStore.find (n)) { Preset q = *p; loadPreset (q); }
    }
    void selectForTesting (int slot) { select (slot); }

    // ---- DialogHost --------------------------------------------------------------------------
    CatRackProcessor& processor() override { return proc_; }
    presets::Store& store() override { return presetStore; }
    Ctx& context() override { return ctx; }
    String presetName() override { return proc_.getSetting (setting::preset, "Empty rack").toString(); }
    void setPresetName (const String& n) override { proc_.setSetting (setting::preset, n); syncName(); }
    bool dirty() override { return dirtyFlag; }
    void markSaved() override { setDirty (false); }
    void toast (const String& s) override { toastView.say (s, getWidth(), getHeight()); }
    void resetWindow() override { if (onResetSize) onResetSize(); }
    int unitCount() override { return proc_.structure().unitCount(); }
    void settingsChanged() override
    {
        startTimerHz (ctx.power ? 16 : 60);
        backdrop.repaint();
        repaint();
        for (auto* c : getChildren()) c->repaint();
        rowsArea.repaintCards();
    }
    void addUnit (int type) override
    {
        auto s = proc_.structure();
        const int slot = s.freeSlot();
        if (s.unitCount() >= catr::kMaxUnits || slot < 0) { toast ("The rack is full: 12 units is the most"); return; }
        std::vector<Row> rows = readRows(s);
        rows.push_back ({ 1, { slot, -1 }, 100.0f });
        s.slotType[slot] = type;
        applyRows (rows, s);
        select (slot);
        setDirty (true);
        toast ("Added " + String::fromUTF8 (catr::unitTable()[type].name));
    }
    void clearRack() override
    {
        proc_.commitStructure (catr::Structure(), true);
        for (int g = 0; g < catr::kMaxUnits; ++g) setIfDiffers ("smix" + String (g), 100.0f);
        sel = -1; proc_.setSetting (setting::sel, -1);
        setDirty (true);
        rebuildAll();
    }

    Preset currentPreset() override
    {
        Preset p;
        const auto s = proc_.structure();
        for (int g = 0; g < s.nStages; ++g)
        {
            PStage st; st.mix = proc_.value (("smix" + String (g)).toRawUTF8());
            for (int i = 0; i < s.st[g].n; ++i)
            {
                const int sl = s.st[g].slot[i];
                PSlot ps; ps.type = s.slotType[sl];
                ps.on = proc_.flag (slotId (sl, "on").toRawUTF8()); ps.solo = proc_.flag (slotId (sl, "solo").toRawUTF8()); ps.mute = proc_.flag (slotId (sl, "mute").toRawUTF8());
                ps.lane = proc_.value (slotId (sl, "lane").toRawUTF8());
                for (int k = 0; k < catr::unitTable()[ps.type].nParams; ++k) ps.p[k] = proc_.slotReal (sl, k);
                st.slots.push_back (ps);
            }
            p.stages.push_back (st);
        }
        p.inDb = proc_.value (ids::inTrim); p.outDb = proc_.value (ids::outTrim);
        return p;
    }

    void loadPreset (const Preset& p) override
    {
        catr::Structure ns;
        std::vector<Row> rows;
        int slot = 0;
        for (auto& st : p.stages)
        {
            Row r { 0, { -1, -1 }, st.mix };
            for (auto& ps : st.slots)
            {
                if (slot >= catr::kMaxUnits || r.n >= 2) break;
                ns.slotType[slot] = ps.type; r.slot[r.n++] = slot++;
            }
            if (r.n > 0) rows.push_back (r);
        }
        applyRows (rows, ns);
        slot = 0;
        for (auto& st : p.stages)
            for (auto& ps : st.slots)
            {
                if (slot >= catr::kMaxUnits) break;
                for (int k = 0; k < catr::unitTable()[ps.type].nParams; ++k) proc_.setSlotReal (slot, k, ps.p[k]);
                proc_.setFlag (slotId (slot, "on").toRawUTF8(), ps.on); proc_.setFlag (slotId (slot, "solo").toRawUTF8(), ps.solo); proc_.setFlag (slotId (slot, "mute").toRawUTF8(), ps.mute);
                proc_.setValueReal (slotId (slot, "lane").toRawUTF8(), ps.lane);
                ++slot;
            }
        proc_.setValueReal (ids::inTrim, p.inDb); proc_.setValueReal (ids::outTrim, p.outDb);
        const auto s = proc_.structure();
        sel = s.nStages > 0 ? s.st[0].slot[0] : -1;
        proc_.setSetting (setting::sel, sel);
        setPresetName (p.name);
        setDirty (false);
        rebuildAll();
        toast ("Loaded " + p.name);
    }

    // ---- RackHost ----------------------------------------------------------------------------
    CatRackProcessor& proc() override { return proc_; }
    const Ctx& context() const override { return ctx; }
    SlotLive& live (int slot) override { return lives[(size_t) jlimit (0, catr::kMaxUnits - 1, slot)]; }
    int selected() const override { return sel; }
    void select (int slot) override
    {
        sel = slot; proc_.setSetting (setting::sel, sel);
        detail.rebuild(); strip.repaint(); rowsArea.repaintCards();
    }
    void edited() override { setDirty (true); }
    void removeSlot (int slot) override
    {
        auto s = proc_.structure();
        std::vector<Row> rows = readRows (s);
        int si = -1;
        for (size_t g = 0; g < rows.size(); ++g)
            for (int i = 0; i < rows[g].n; ++i) if (rows[g].slot[i] == slot) si = (int) g;
        if (si < 0) return;
        auto& r = rows[(size_t) si];
        if (r.n == 2) { r.slot[0] = r.slot[0] == slot ? r.slot[1] : r.slot[0]; r.slot[1] = -1; r.n = 1; r.mix = 100.0f; }
        else rows.erase (rows.begin() + si);
        s.slotType[slot] = -1;
        applyRows (rows, s);
        if (! rows.empty()) sel = rows[(size_t) jmin (si, (int) rows.size() - 1)].slot[0]; else sel = -1;
        proc_.setSetting (setting::sel, sel);
        setDirty (true);
        rebuildAll();
    }
    void moveRow (int row, int d) override
    {
        auto s = proc_.structure();
        std::vector<Row> rows = readRows (s);
        const int j = row + d;
        if (row < 0 || row >= (int) rows.size() || j < 0 || j >= (int) rows.size()) return;
        std::swap (rows[(size_t) row], rows[(size_t) j]);
        const int keep = sel;
        applyRows (rows, s);
        sel = keep; setDirty (true); rebuildAll();
        focusSlot (keep);
    }
    void joinAbove (int slot) override
    {
        auto s = proc_.structure();
        std::vector<Row> rows = readRows (s);
        const int si = s.stageOf (slot);
        if (si < 1 || rows[(size_t) si].n != 1 || rows[(size_t) si - 1].n != 1) return;
        auto& prev = rows[(size_t) si - 1];
        prev.slot[1] = slot; prev.n = 2; prev.mix = 100.0f;
        const int other = prev.slot[0];
        rows.erase (rows.begin() + si);
        applyRows (rows, s);
        proc_.setValueReal (slotId (slot, "lane").toRawUTF8(), -6.0f); proc_.setValueReal (slotId (other, "lane").toRawUTF8(), -6.0f);
        setDirty (true); rebuildAll();
    }
    void splitOut (int slot) override
    {
        auto s = proc_.structure();
        std::vector<Row> rows = readRows (s);
        const int si = s.stageOf (slot);
        if (si < 0 || rows[(size_t) si].n != 2) return;
        auto& r = rows[(size_t) si];
        r.slot[0] = r.slot[0] == slot ? r.slot[1] : r.slot[0]; r.slot[1] = -1; r.n = 1; r.mix = 100.0f;
        rows.insert (rows.begin() + si + 1, Row { 1, { slot, -1 }, 100.0f });
        applyRows (rows, s);
        setDirty (true); rebuildAll();
    }
    void dropSlot (int slot, const DropTarget& t) override
    {
        auto s = proc_.structure();
        std::vector<Row> rows = readRows (s);
        const int oi = s.stageOf (slot);
        if (oi < 0 || t.mode == DropTarget::None) return;
        const int targetFirst = (t.row >= 0 && t.row < (int) rows.size()) ? rows[(size_t) t.row].slot[0] : -1;
        if (t.row == oi && rows[(size_t) oi].n == 1 && t.mode != DropTarget::End && t.mode != DropTarget::Start) return;
        auto& o = rows[(size_t) oi];
        if (o.n == 2) { o.slot[0] = o.slot[0] == slot ? o.slot[1] : o.slot[0]; o.slot[1] = -1; o.n = 1; o.mix = 100.0f; }
        else rows.erase (rows.begin() + oi);
        auto findRow = [&rows] (int firstSlot) { for (size_t g = 0; g < rows.size(); ++g) if (rows[g].slot[0] == firstSlot) return (int) g; return -1; };
        if (t.mode == DropTarget::Par)
        {
            const int ti = findRow (targetFirst);
            if (ti < 0 || rows[(size_t) ti].n != 1) return;
            rows[(size_t) ti].slot[1] = slot; rows[(size_t) ti].n = 2; rows[(size_t) ti].mix = 100.0f;
            const int other = rows[(size_t) ti].slot[0];
            applyRows (rows, s);
            proc_.setValueReal (slotId (slot, "lane").toRawUTF8(), -6.0f); proc_.setValueReal (slotId (other, "lane").toRawUTF8(), -6.0f);
        }
        else
        {
            int idx = (int) rows.size();
            if (t.mode == DropTarget::Start) idx = 0;
            else if (t.mode == DropTarget::Before || t.mode == DropTarget::After)
            {
                const int ti = findRow (targetFirst);
                idx = ti < 0 ? (int) rows.size() : ti + (t.mode == DropTarget::After ? 1 : 0);
            }
            rows.insert (rows.begin() + idx, Row { 1, { slot, -1 }, 100.0f });
            applyRows (rows, s);
        }
        sel = slot; proc_.setSetting (setting::sel, sel);
        setDirty (true); rebuildAll();
    }
    void openAdd() override { modal.openAdd(); }
    void focusSlot (int slot) override { if (auto* c = rowsArea.cardFor (slot)) c->grabKeyboardFocus(); }
    float rowLevelDb (int row) override { return row >= 0 && row < catr::kMaxUnits ? stageDb[row] : -90.0f; }
    float inLevelDb() override { return inDb; }

    // ---- keyboard ----------------------------------------------------------------------------
    bool keyPressed (const KeyPress& k) override
    {
        if (modal.isOpen() || k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown() || k.getModifiers().isAltDown()) return false;
        const juce_wchar c = CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (c == 'b') toggleBypass();
        else if (c == '[') step (-1);
        else if (c == ']') step (1);
        else return false;
        return true;
    }

    // ---- info bubbles ------------------------------------------------------------------------
    void mouseMove (const MouseEvent& e) override { trackTip (e); }
    void mouseEnter (const MouseEvent& e) override { trackTip (e); }
    void mouseExit (const MouseEvent&) override { hideTip(); }
    void mouseDown (const MouseEvent& e) override
    {
        hideTip();
        if (dynamic_cast<TextEditor*> (e.eventComponent) == nullptr && ! modal.isOpen() && dynamic_cast<SlotCard*> (e.eventComponent) == nullptr
            && dynamic_cast<KnobWidget*> (e.eventComponent) == nullptr) grabKeyboardFocus();
    }

private:
    struct Row { int n; int slot[2]; float mix; };

    // The display refresh stops while the window is not on screen (plugin windows are often hidden or minimised).
    void timerCallback() override
    {
        if (! isShowing()) return;
        tick (1.0f / (float) (ctx.power ? 16 : 60));
    }

    void setDirty (bool v)
    {
        if (dirtyFlag == v) return;
        dirtyFlag = v; proc_.setSetting ("dirty", v); syncName();
    }
    void syncName() { pName->setText (presetName() + (dirtyFlag ? " *" : "")); }

    void tick (float dt)
    {
        if (proc_.structureVersion() != seenVer) { seenVer = proc_.structureVersion(); validateSelection(); rebuildAll(); }
        auto& rk = proc_.rack;
        const float inL = rk.inPk[0].exchange (0.0f), inR = rk.inPk[1].exchange (0.0f), outL = rk.outPk[0].exchange (0.0f), outR = rk.outPk[1].exchange (0.0f);
        meter.update (inL, inR, outL, outR, dt);
        inDb = dbOfLin (jmax (inL, inR));
        const auto s = proc_.structure();
        histAcc += dt;
        const bool pushHist = histAcc >= 1.0f / 30.0f;
        if (pushHist) histAcc = std::fmod (histAcc, 1.0f / 30.0f);
        for (int g = 0; g < catr::kMaxUnits; ++g)
        {
            const float sp = rk.stagePk[g].exchange (0.0f), d = dbOfLin (sp);
            stageDb[g] = d > stageDb[g] ? d : jmax (d, stageDb[g] - 40.0f * dt);
        }
        for (int sl = 0; sl < catr::kMaxUnits; ++sl)
        {
            auto& L = lives[(size_t) sl];
            const float pk = rk.slotPk[sl].exchange (0.0f), d = dbOfLin (pk), gr = rk.slotGr[sl].load();
            L.gr = gr > L.gr ? gr : L.gr + (gr - L.gr) * 0.25f;
            if (L.gr < 0.02f) L.gr = 0.0f;
            L.outDb = d > L.outDb ? d : jmax (d, L.outDb - 40.0f * dt);
            if (pushHist && s.slotType[sl] >= 0)
            {
                const bool wet = catr::unitTable()[s.slotType[sl]].viz == catr::VizWet;
                L.hist[L.hp] = wet ? L.outDb : L.gr;
                L.hp = (L.hp + 1) % 180;
            }
        }
        bBypass->setOn (proc_.flag (ids::bypass));
        rowsArea.repaintCards();
        strip.tick();
        detail.tick();
        if (auto* ed = findParentComponentOfClass<CatRackEditor>())
            readouts.sizeText = String (ed->getWidth()) + u8 (" \xc3\x97 ") + String (ed->getHeight()) + " px";
        readouts.repaint (1000, 612, 200, 28);
        syncName();
    }

    void toggleBypass() { proc_.setFlag (ids::bypass, ! proc_.flag (ids::bypass)); }

    void step (int d)
    {
        auto all = presetStore.all();
        if (all.empty()) return;
        int i = -1;
        for (size_t k = 0; k < all.size(); ++k) if (all[k].name == presetName()) i = (int) k;
        if (i < 0) i = lastStepIdx;                           // name not found (edited or renamed): carry on from the last preset we stepped to
        const int n = (int) all.size();
        i = i < 0 ? (d > 0 ? 0 : n - 1) : (i + d + n) % n;
        lastStepIdx = i;
        loadPreset (all[(size_t) i]);
    }

    // ---- structure editing ---------------------------------------------------------------------------
    std::vector<Row> readRows (const catr::Structure& s)
    {
        std::vector<Row> r;
        for (int g = 0; g < s.nStages; ++g)
            r.push_back ({ s.st[g].n, { s.st[g].slot[0], s.st[g].slot[1] }, proc_.value (("smix" + String (g)).toRawUTF8()) });
        return r;
    }
    void setIfDiffers (const String& id, float v)
    {
        if (std::abs (proc_.value (id.toRawUTF8()) - v) > 0.01f) proc_.setValueReal (id.toRawUTF8(), v);
    }
    // base carries the unit type of every slot; the rows say how they are arranged.
    void applyRows (const std::vector<Row>& rows, catr::Structure base)
    {
        catr::Structure ns;
        for (int i = 0; i < catr::kMaxUnits; ++i) ns.slotType[i] = base.slotType[i];
        ns.nStages = (int) rows.size();
        for (int g = 0; g < ns.nStages; ++g)
        {
            ns.st[g].n = rows[(size_t) g].n;
            ns.st[g].slot[0] = rows[(size_t) g].slot[0]; ns.st[g].slot[1] = rows[(size_t) g].slot[1];
        }
        proc_.commitStructure (ns, true);
        for (int g = 0; g < catr::kMaxUnits; ++g)
            setIfDiffers ("smix" + String (g), g < (int) rows.size() ? rows[(size_t) g].mix : 100.0f);
        seenVer = proc_.structureVersion();
    }

    void validateSelection()
    {
        const auto s = proc_.structure();
        if (sel < 0 || sel >= catr::kMaxUnits || s.stageOf (sel) < 0) sel = s.nStages > 0 ? s.st[0].slot[0] : -1;
    }

    void rebuildAll()
    {
        validateSelection();
        strip.rebuild();
        rowsArea.rebuild();
        detail.rebuild();
        const int n = proc_.structure().unitCount();
        readouts.countText = String (n) + " of " + String (catr::kMaxUnits) + " units";
        bAdd->setDisabled (n >= catr::kMaxUnits);
        bAdd->tip = n >= catr::kMaxUnits ? "The rack is full: 12 units is the most." : "Open the unit browser: dynamics, EQ, saturation, space, creative and utility units. The rack holds up to 12.";
        readouts.repaint();
        syncName();
    }

    // ---- building ----------------------------------------------------------------------------
    PButton* addButton (const String& text, Rectangle<int> r, const char* tip, float px = 14.0f, theme::FontKind k = theme::FontKind::regular)
    {
        auto* b = new PButton (ctx, text, px, k);
        owned.add (b);
        addAndMakeVisible (b);
        b->setBounds (r);
        b->tip = tip;
        return b;
    }

    void buildHeader()
    {
        const float xb = 819.83f;
        const float presetW = 35.34f + 4.0f + 210.0f + 4.0f + 35.34f + 4.0f + 47.08f;
        const float free = xb - Backdrop::brandEnd() - 4.0f * 10.0f - presetW, sp = free * 0.5f;
        const float px = Backdrop::brandEnd() + 10.0f + sp + 10.0f;
        pPrev = addButton ({}, { roundToInt (px), 10, 35, 25 }, layout::tipPrev); pPrev->glyph = 1;
        pName = addButton (presetName(), { roundToInt (px + 39.34f), 9, 210, 27 }, "Open the preset browser: the ready-made chains plus your own. Double-click to rename your own preset.", 16.0f, theme::FontKind::semi);
        pNext = addButton ({}, { roundToInt (px + 39.34f + 214.0f), 10, 35, 25 }, layout::tipNext); pNext->glyph = 2;
        pSave = addButton ("Save", { roundToInt (px + 39.34f + 214.0f + 39.34f), 10, 47, 25 }, layout::tipSave);
        bBypass = addButton ("Bypass", { 820, 10, 59, 25 }, layout::tipBypass);
        bGuide = addButton ("Guide", { 889, 10, 52, 25 }, layout::tipGuide);
        bSettings = addButton ("Settings", { 951, 10, 66, 25 }, layout::tipSettings);
        bAbout = addButton ("About & Support", { 1027, 10, 109, 25 }, layout::tipAbout);
        pPrev->onClick = [this] (const ModifierKeys&) { step (-1); };
        pNext->onClick = [this] (const ModifierKeys&) { step (1); };
        pSave->onClick = [this] (const ModifierKeys&) { modal.openSave (nullptr); };
        bBypass->onClick = [this] (const ModifierKeys&) { toggleBypass(); };
        bGuide->onClick = [this] (const ModifierKeys&) { modal.openGuide(); };
        bSettings->onClick = [this] (const ModifierKeys&) { modal.openSettings(); };
        bAbout->onClick = [this] (const ModifierKeys&) { modal.openAbout(); };
        pName->onClick = [this] (const ModifierKeys&) { nameDelay.start (260); };
        nameDelay.fn = [this] { modal.openBrowser(); };
        pName->onDouble = [this]
        {
            nameDelay.stopTimer();
            const Preset* p = presetStore.find (presetName());
            if (p == nullptr || p->factory) { toast ("Save a copy first, factory presets cannot be renamed"); return; }
            modal.openBrowser (true);
        };
    }

    void buildRack()
    {
        addAndMakeVisible (strip);
        strip.setBounds (roundToInt (layout::strip.x), roundToInt (layout::strip.y), roundToInt (layout::strip.w), roundToInt (layout::strip.h));
        bAdd = addButton ("+ Add unit", { 58, 142, 90, 24 }, "");
        bClear = addButton ("Clear", { 154, 142, 52, 24 }, layout::tipClear);
        bAdd->onClick = [this] (const ModifierKeys&) { modal.openAdd(); };
        bClear->onClick = [this] (const ModifierKeys&)
        {
            if (proc_.structure().unitCount() > 0) modal.openClear();
        };
        readouts.countX = 214.0f;
        addAndMakeVisible (rowsArea);
        rowsArea.setBounds (roundToInt (layout::rows.x), roundToInt (layout::rows.y), roundToInt (layout::rows.w), roundToInt (layout::rows.h));
        addAndMakeVisible (detail);
        detail.setBounds (768, 46, 288, 568);
    }

    // ---- tips --------------------------------------------------------------------------------
    void trackTip (const MouseEvent& e)
    {
        if (modal.isOpen() || ! ctx.tips || rowsDragging()) { hideTip(); return; }
        Component* c = e.eventComponent;
        String text; Component* host = nullptr; Rectangle<int> bounds;
        while (c != nullptr && c != this)
        {
            if (auto* tc = dynamic_cast<TipClient*> (c))
            {
                const auto p = c->getLocalPoint (e.eventComponent, e.getPosition());
                const String t = tc->tipAt (p);
                if (t.isNotEmpty()) { text = t; host = c; bounds = tc->tipBoundsAt (p); break; }
            }
            c = c->getParentComponent();
        }
        if (host == nullptr || text.isEmpty()) { hideTip(); return; }
        if (host == tipHost && text == tipText) return;
        hideTip();
        tipHost = host; tipText = text; tipBounds = bounds;
        tipTimer.start (1000);
    }
    bool rowsDragging() const { return (ModifierKeys::currentModifiers.getRawFlags() & ModifierKeys::leftButtonModifier) != 0; }
    void showTip()
    {
        if (tipHost == nullptr || modal.isOpen()) return;
        auto target = getLocalArea (tipHost, tipBounds.isEmpty() ? tipHost->getLocalBounds() : tipBounds);
        bubble.showAt (tipText, target, getWidth(), getHeight());
    }
    void hideTip() { tipTimer.stopTimer(); bubble.setVisible (false); tipHost = nullptr; tipText = {}; }

    // ---- members -----------------------------------------------------------------------------
    CatRackProcessor& proc_;
    std::function<void()> onResetSize;
    Ctx ctx;
    presets::Store presetStore;
    Backdrop backdrop;
    Cheek* cheekL = nullptr; Cheek* cheekR = nullptr;
    OwnedArray<Component> owned;
    PButton *pPrev = nullptr, *pName = nullptr, *pNext = nullptr, *pSave = nullptr, *bBypass = nullptr, *bGuide = nullptr, *bSettings = nullptr, *bAbout = nullptr;
    PButton *bAdd = nullptr, *bClear = nullptr;
    StripView strip;
    RowsArea rowsArea;
    DetailPanel detail;
    MeterView meter;
    Readouts readouts;
    Bubble bubble; Toast toastView;
    Modal modal;
    Delay nameDelay, tipTimer;
    Component* tipHost = nullptr;
    Rectangle<int> tipBounds;
    String tipText;
    std::array<SlotLive, catr::kMaxUnits> lives {};
    float stageDb[catr::kMaxUnits] {};
    float inDb = -90.0f, histAcc = 0.0f;
    int sel = -1, seenVer = -1;
    bool dirtyFlag = false;
    int lastStepIdx = -1;
};

// =====================================================================================================
CatRackEditor::CatRackEditor (CatRackProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    face = std::make_unique<Face> (p, [this] { resetSize(); });
    addAndMakeVisible (*face);
    constrainer.setFixedAspectRatio ((double) kW / (double) kH);
    constrainer.setMinimumSize (480, 256);
    constrainer.setMaximumSize (1800, 960);
    setConstrainer (&constrainer);
    setResizable (true, false);
    border = std::make_unique<ResizableBorderComponent> (this, &constrainer);
    border->setBorderThickness (BorderSize<int> (5));
    addAndMakeVisible (*border);
    const double sc = jlimit (0.4, 1.5, (double) proc.getSetting (setting::scale, 1.0));
    setSize (roundToInt (kW * sc), roundToInt (kH * sc));
}

CatRackEditor::~CatRackEditor() = default;

void CatRackEditor::resized()
{
    const double s = (double) getWidth() / (double) kW;
    face->setTransform (AffineTransform::scale ((float) s));
    face->setBounds (0, 0, kW, kH);
    border->setBounds (getLocalBounds());
    proc.setSetting (setting::scale, s);
}

void CatRackEditor::resetSize() { setSize (kW, kH); }
void CatRackEditor::openDialogForTesting (const juce::String& n) { face->openForTesting (n); }
void CatRackEditor::tickForTesting (int frames) { face->tickForTesting (frames); }
void CatRackEditor::loadPresetForTesting (const juce::String& n) { face->loadByName (n); }
void CatRackEditor::selectForTesting (int slot) { face->selectForTesting (slot); }
