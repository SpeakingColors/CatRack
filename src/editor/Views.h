#pragma once
#include <cstring>
#include "Widgets.h"
#include "Layout.h"
#include "../plugin/PluginProcessor.h"

namespace ui
{
// ---- what the views need from the editor --------------------------------------------------------------
struct SlotLive
{
    float gr = 0.0f, outDb = -90.0f, eqd = 0.0f;
    float hist[180] {};
    int hp = 0;
    float at (int i) const { return hist[(hp + i) % 180]; }
};

struct DropTarget
{
    enum Mode { None, Before, After, Par, End, Start } mode = None;
    int row = -1;
};

struct RackHost
{
    virtual ~RackHost() = default;
    virtual CatRackProcessor& proc() = 0;
    virtual const Ctx& context() const = 0;
    virtual SlotLive& live (int slot) = 0;
    virtual int selected() const = 0;
    virtual void select (int slot) = 0;
    virtual void edited() = 0;                    // the user changed a control (marks the preset as changed)
    virtual void removeSlot (int slot) = 0;
    virtual void moveRow (int row, int d) = 0;
    virtual void joinAbove (int slot) = 0;
    virtual void splitOut (int slot) = 0;
    virtual void dropSlot (int slot, const DropTarget& t) = 0;
    virtual void openAdd() = 0;
    virtual void focusSlot (int slot) = 0;
    virtual float rowLevelDb (int row) = 0;       // output level of a row, for the arrows in the signal path
    virtual float inLevelDb() = 0;
};

// ---- the controls' definitions that are not unit controls ---------------------------------------------------
inline const catr::ParamDef& laneDef()
{
    static const catr::ParamDef d { "lane", "This lane", "LANE", "This lane", -24.0f, 6.0f, -6.0f, " dB", 1,
        "Lane level: how loud this unit is in the parallel pair. The two lanes are added together.", false, 0, {}, {} };
    return d;
}
inline const catr::ParamDef& otherLaneDef()
{
    static const catr::ParamDef d { "lane2", "Other lane", "LANE", "Other lane", -24.0f, 6.0f, -6.0f, " dB", 1,
        "Other lane: the level of the unit running beside this one.", false, 0, {}, {} };
    return d;
}
inline const catr::ParamDef& pairMixDef()
{
    static const catr::ParamDef d { "pmix", "Dry/Wet", "MIX", "Dry/Wet", 0.0f, 100.0f, 100.0f, " %", 0,
        "Pair Dry/Wet: blends the untouched signal with the two lanes added together. Below 100 percent keeps some of the original.", false, 0, {}, {} };
    return d;
}
inline const catr::ParamDef& inTrimDef()
{
    static const catr::ParamDef d { "in", "IN trim", "IN", "IN trim", -12.0f, 12.0f, 0.0f, " dB", 1,
        "Input trim: turns the signal up or down before the first unit.", false, 0, {}, {} };
    return d;
}
inline const catr::ParamDef& outTrimDef()
{
    static const catr::ParamDef d { "out", "OUT trim", "OUT", "OUT trim", -12.0f, 12.0f, 0.0f, " dB", 1,
        "Output trim: turns the signal up or down after the last unit.", false, 0, {}, {} };
    return d;
}
inline Binding slotBinding (CatRackProcessor& p, int slot, int idx)
{
    Binding b;
    const int t = p.slotType (slot);
    if (t >= 0 && idx < catr::unitTable()[t].nParams) { b.prm = p.slotParam (slot, idx); b.pd = &catr::unitTable()[t].p[idx]; }
    return b;
}
inline Binding paramBinding (CatRackProcessor& p, const char* id, const catr::ParamDef& d) { return { p.apvts.getParameter (id), &d }; }
inline String slotId (int slot, const char* tail) { return "u" + String (slot) + tail; }

// ---- small drawings (mini displays and the big one) ------------------------------------------------------------
inline void tickLabels (Graphics& g, const Ctx& ctx, float x0, float bw, float y0, float bh, float h, const std::vector<float>& marks, float lo, float hi)
{
    auto& t = ctx.t();
    const auto f = theme::font (theme::FontKind::mono, 9.0f);
    for (size_t i = 0; i < marks.size(); ++i)
    {
        const float d = marks[i], xx = x0 + (d - lo) / (hi - lo) * bw;
        g.setColour (t.dim); g.fillRect (std::floor (xx), y0 + bh, 1.0f, 3.0f);
        const bool first = i == 0, last = i + 1 == marks.size();
        const String s = d == 0.0f ? String ("0") : String ((int) d);
        const Justification j = first ? Justification::centredLeft : last ? Justification::centredRight : Justification::centred;
        drawText (g, s, { first ? xx - 1.0f : last ? xx - 40.0f + 1.0f : xx - 20.0f, h - 11.0f, 40.0f, 11.0f }, f, t.dim, j);
    }
}

inline void paintEqCurve (Graphics& g, const Ctx& ctx, int type, const float* p, Colour col, float x, float y, float w, float h, bool big)
{
    auto& t = ctx.t();
    auto X = [=] (float f) { return x + std::log (f / 20.0f) / std::log (1000.0f) * w; };
    auto Y = [=] (float d) { return y + h * 0.5f - d / 12.0f * h * 0.5f; };
    const auto f9 = theme::font (theme::FontKind::mono, 9.0f);
    if (big)
    {
        for (int d : { -12, -6, 0, 6, 12 })
        {
            g.setColour (t.line.withAlpha (d == 0 ? 0.9f : 0.35f));
            g.fillRect (x, std::floor (Y ((float) d)), w, 1.0f);
            drawText (g, (d > 0 ? "+" : "") + String (d), { x - 30.0f, Y ((float) d) - 6.0f, 27.0f, 12.0f }, f9, t.dim, Justification::centredRight);
        }
        struct M { float f; const char* s; };
        for (auto m : { M { 100.0f, "100" }, M { 1000.0f, "1k" }, M { 10000.0f, "10k" } })
        {
            const float xx = X (m.f);
            g.setColour (t.line.withAlpha (0.35f)); g.fillRect (std::floor (xx), y, 1.0f, h);
            drawText (g, m.s, { xx - 20.0f, y + h + 2.0f, 40.0f, 10.0f }, f9, t.dim);
        }
    }
    else
    {
        g.setColour (t.line); g.fillRect (x, std::floor (Y (0.0f)), w, 1.0f);
    }
    Path path;
    for (int i = 0; i <= 80; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / 80.0f);
        const float d = jlimit (-12.0f, 12.0f, catr::eqDisplayDb (type, p, f));
        if (i == 0) path.startNewSubPath (X (f), Y (d)); else path.lineTo (X (f), Y (d));
    }
    g.setColour (col);
    g.strokePath (path, PathStrokeType (big ? 2.0f : 1.5f));
}

// The small display on a unit card. w and h are the canvas size.
inline void paintMini (Graphics& g, const Ctx& ctx, const catr::UnitDef& u, const float* p, const SlotLive& L, float w, float h)
{
    auto& t = ctx.t();
    const Colour col (u.color);
    if (u.viz == catr::VizEq)
    {
        g.setColour (t.disp); g.fillRect (2.0f, 2.0f, w - 4.0f, h - 14.0f);
        g.setColour (t.line); g.drawRect (2.5f, 2.5f, w - 5.0f, h - 15.0f, 1.0f);
        paintEqCurve (g, ctx, std::strcmp (u.key, "peq") == 0 ? 9 : 3, p, col, 4.0f, 4.0f, w - 8.0f, h - 18.0f, false);
        drawText (g, u8 ("\xc2\xb1" "12 dB"), { 0.0f, h - 11.0f, w, 11.0f }, theme::font (theme::FontKind::mono, 9.0f), t.dim);
        return;
    }
    const float x0 = 2.0f, bw = w - 4.0f, y0 = 3.0f, bh = 9.0f;
    g.setColour (t.disp); g.fillRect (x0, y0, bw, bh);
    if (u.viz == catr::VizWet)
    {
        g.setColour (col); g.fillRect (x0, y0, jlimit (0.0f, 1.0f, (L.outDb + 40.0f) / 40.0f) * bw, bh);
    }
    else
    {
        g.setColour (theme::pink); g.fillRect (x0, y0, jlimit (0.0f, 1.0f, L.gr / 20.0f) * bw, bh);
    }
    g.setColour (t.line); g.drawRect (x0 + 0.5f, y0 + 0.5f, bw - 1.0f, bh - 1.0f, 1.0f);
    if (u.viz == catr::VizWet) tickLabels (g, ctx, x0, bw, y0, bh, h, { -40.0f, -20.0f, 0.0f }, -40.0f, 0.0f);
    else if (w >= 100.0f) tickLabels (g, ctx, x0, bw, y0, bh, h, { 0.0f, -5.0f, -10.0f, -15.0f, -20.0f }, 0.0f, -20.0f);
    else tickLabels (g, ctx, x0, bw, y0, bh, h, { 0.0f, -10.0f, -20.0f }, 0.0f, -20.0f);
}

// The large display in the unit panel.
inline void paintBig (Graphics& g, const Ctx& ctx, const catr::UnitDef& u, int type, const float* p, const SlotLive& L, float w, float h)
{
    auto& t = ctx.t();
    const Colour col (u.color);
    if (u.viz == catr::VizEq) { paintEqCurve (g, ctx, type, p, col, 26.0f, 3.0f, w - 30.0f, h - 16.0f, true); return; }
    const float gx = 26.0f, gw = w - gx - 3.0f, gy = 3.0f, gh = h - 6.0f;
    const auto f9 = theme::font (theme::FontKind::mono, 9.0f);
    const bool wet = u.viz == catr::VizWet;
    auto yOf = [&] (float v) { return wet ? gy + (-jlimit (-40.0f, 0.0f, v)) / 40.0f * gh : gy + jlimit (0.0f, 1.0f, v / 20.0f) * gh; };
    if (wet)
        for (int d : { 0, -20, -40 })
        {
            const float y = gy + (float) -d / 40.0f * gh;
            g.setColour (t.line.withAlpha (0.4f)); g.fillRect (gx, std::floor (y), gw, 1.0f);
            drawText (g, String (d), { 0.0f, jlimit (6.0f, h - 6.0f, y) - 6.0f, gx - 3.0f, 12.0f }, f9, t.dim, Justification::centredRight);
        }
    else
        for (int d : { 0, 5, 10, 15, 20 })
        {
            const float y = gy + (float) d / 20.0f * gh;
            g.setColour (t.line.withAlpha (0.4f)); g.fillRect (gx, std::floor (y), gw, 1.0f);
            drawText (g, d ? "-" + String (d) : String ("0"), { 0.0f, jlimit (6.0f, h - 6.0f, y) - 6.0f, gx - 3.0f, 12.0f }, f9, t.dim, Justification::centredRight);
        }
    Path line;
    for (int i = 0; i < 180; ++i)
    {
        const float X = gx + (float) i / 179.0f * gw, Y = yOf (L.at (i));
        if (i == 0) line.startNewSubPath (X, Y); else line.lineTo (X, Y);
    }
    Path area (line);
    if (wet) { area.lineTo (gx + gw, gy + gh); area.lineTo (gx, gy + gh); }
    else { area.lineTo (gx + gw, gy); area.lineTo (gx, gy); }
    area.closeSubPath();
    const Colour fillCol = wet ? col : theme::pink;
    g.setColour (fillCol.withAlpha (wet ? 0.4f : 0.55f)); g.fillPath (area);
    g.setColour (fillCol); g.strokePath (line, PathStrokeType (1.5f));
}

// ---- one unit on the rack ----------------------------------------------------------------------------------------
class SlotCard : public Component, public TipClient
{
public:
    SlotCard (RackHost& h, int slotIdx, int pairIdx_, bool compact) : host (h), slot (slotIdx), pairIdx (pairIdx_), cp (compact)
    {
        const int type = host.proc().slotType (slot);
        u = &catr::unitTable()[type];
        col = Colour (u->color);
        const bool pr = pairIdx >= 0;
        const int ks = cp ? 22 : pr ? 28 : 30;
        for (int i = 0; i < 3; ++i)
        {
            auto* k = knobs.add (new KnobWidget (host.context(), KnobWidget::Mini, ks));
            k->labelH = cp ? 9.0f : 11.0f;
            k->bind (slotBinding (host.proc(), slot, i), col);
            k->onEdited = [this] { host.edited(); };
            addAndMakeVisible (k);
        }
        const char* names[3] = { "ON", "S", "M" };
        const char* tails[3] = { "on", "solo", "mute" };
        const Tgl::Kind kinds[3] = { Tgl::On, Tgl::Solo, Tgl::Mute };
        const char* tips[3] = { layout::tipOn, layout::tipSolo, layout::tipMute };
        for (int i = 0; i < 3; ++i)
        {
            auto* t = tgls.add (new Tgl (host.context(), names[i], kinds[i]));
            t->bind (host.proc().apvts.getParameter (slotId (slot, tails[i])));
            t->tip = tips[i];
            t->onEdited = [this] { host.edited(); };
            addAndMakeVisible (t);
        }
        setWantsKeyboardFocus (true);
        tip = String::fromUTF8 (u->name) + ": " + String::fromUTF8 (u->blurb) + " Click to open its controls.";
        offP = host.proc().apvts.getRawParameterValue (slotId (slot, "on"));
        muteP = host.proc().apvts.getRawParameterValue (slotId (slot, "mute"));
    }

    std::function<void (SlotCard&, const MouseEvent&)> onPress, onDrag, onRelease;
    int slotIndex() const { return slot; }
    bool dragging = false;

    String tipAt (Point<int> p) const override
    {
        if (gripR.contains (p)) return layout::tipGrip;
        if (dispR.contains (p))
            return u->viz == catr::VizEq ? "Shows the EQ curve of this unit, plus or minus 12 dB. Open the unit to see it larger."
                 : u->viz == catr::VizWet ? "Output level of this unit in dB, from -40 to 0."
                 : "Gain reduction meter: how many dB this unit is turning the signal down right now. Scale from 0 to -20 dB.";
        return tip;
    }
    Rectangle<int> tipBoundsAt (Point<int> p) const override
    {
        if (gripR.contains (p)) return gripR;
        if (dispR.contains (p)) return dispR;
        return nameR;
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        const bool pr = pairIdx >= 0;
        const int mkW = pr ? 32 : 40, gap = pr ? 0 : 8;
        const int ks = cp ? 22 : pr ? 28 : 30;
        const int lab = cp ? 9 : 11;
        const int mkH = ks + 1 + lab;
        const int btnW[3] = { 22, 20, 20 };
        int x = W - 5 - (btnW[0] + btnW[1] + btnW[2] + 4);
        const int by = (H - 22) / 2;
        for (int i = 0; i < 3; ++i) { tgls[i]->setBounds (x, by, btnW[i], 22); x += btnW[i] + 2; }
        const int bx = W - 5 - (btnW[0] + btnW[1] + btnW[2] + 4);
        const int gw = pr ? 52 : 132, gh = cp ? 26 : 34;
        const int gxBox = bx - 4 - (gw + 4);
        dispR = { gxBox + 2, (H - gh) / 2, gw, gh };
        const int mksW = 3 * mkW + 2 * gap;
        const int mx = gxBox - 4 - mksW;
        for (int i = 0; i < 3; ++i) knobs[i]->setBounds (mx + i * (mkW + gap), (H - mkH) / 2, mkW, mkH);
        gripR = { 3, 4, 12, H - 8 };
        nameR = { 19, 0, mx - 4 - 19, H };
    }

    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        const bool sel = host.selected() == slot, off = offP->load() < 0.5f, muted = muteP->load() > 0.5f;
        auto r = getLocalBounds().toFloat();
        g.setColour (sel ? theme::sel : t.plate2); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (sel ? theme::selb : t.line); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (0, 0, 3, getHeight());
            g.setColour (col); g.fillRoundedRectangle (r, 5.0f);
        }
        // grip: a column of dots
        g.setColour (t.dim.withAlpha (0.7f));
        for (float y = (float) gripR.getY() + 3.0f + 5.0f; y < (float) gripR.getBottom() - 1.0f; y += 6.0f)
            g.fillEllipse ((float) gripR.getX() + 6.0f - 1.0f, y - 1.0f, 2.0f, 2.0f);
        // name and kind
        const float a = off ? 0.45f : 1.0f;
        const float ns = cp ? 14.0f : 15.0f;
        const float nameH = ns * 1.05f, subH = cp ? 0.0f : 10.0f * 1.05f + 2.0f;
        const float top = ((float) getHeight() - (nameH + subH)) * 0.5f;
        Rectangle<float> nr ((float) nameR.getX(), top, (float) nameR.getWidth(), nameH + 2.0f);
        const auto nf = theme::font (theme::FontKind::brand, ns);
        const Colour nc = (muted ? theme::pink : t.ink).withMultipliedAlpha (a);
        drawText (g, String::fromUTF8 (u->name), nr, nf, nc, Justification::centredLeft, 0.3f);
        if (muted)
        {
            const float w = textWidth (nf, String::fromUTF8 (u->name)) + 0.3f * (float) String (u->name).length();
            g.setColour (nc); g.fillRect (nr.getX(), nr.getCentreY() + 1.0f, w, 1.0f);
        }
        if (! cp)
        {
            const String sub = pairIdx < 0 ? String::fromUTF8 (u->kind) : String ("Lane ") + (pairIdx ? "B" : "A") + " (parallel)";
            drawText (g, sub.toUpperCase(), { (float) nameR.getX(), top + nameH + 2.0f, (float) nameR.getWidth(), 11.0f },
                      theme::font (theme::FontKind::regular, 10.0f), t.dim.withMultipliedAlpha (a), Justification::centredLeft, 1.0f);
        }
        // display
        {
            Graphics::ScopedSaveState ss (g);
            g.setOpacity (a);
            g.addTransform (AffineTransform::translation ((float) dispR.getX(), (float) dispR.getY()));
            g.reduceClipRegion (0, 0, dispR.getWidth(), dispR.getHeight());
            float p[6] {};
            for (int k = 0; k < 6; ++k) p[k] = host.proc().slotReal (slot, k);
            paintMini (g, host.context(), *u, p, host.live (slot), (float) dispR.getWidth(), (float) dispR.getHeight());
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (theme::selb);
            g.drawRoundedRectangle (r.expanded (1.0f), 6.0f, 2.0f);
        }
    }

    void refreshAlpha()
    {
        const float a = offP->load() < 0.5f ? 0.45f : 1.0f;
        for (auto* k : knobs) k->setAlpha (a);
    }

    void mouseDown (const MouseEvent& e) override { grabKeyboardFocus(); if (onPress) onPress (*this, e); }
    void mouseDrag (const MouseEvent& e) override { if (onDrag) onDrag (*this, e); }
    void mouseUp (const MouseEvent& e) override { if (onRelease) onRelease (*this, e); }

    bool keyPressed (const KeyPress& k) override
    {
        const auto s = host.proc().structure();
        const int row = s.stageOf (slot);
        if (k.isKeyCode (KeyPress::returnKey) || k.isKeyCode (KeyPress::spaceKey)) { host.select (slot); return true; }
        if (k.getModifiers().isAltDown() && k.isKeyCode (KeyPress::upKey)) { host.moveRow (row, -1); return true; }
        if (k.getModifiers().isAltDown() && k.isKeyCode (KeyPress::downKey)) { host.moveRow (row, 1); return true; }
        if (k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::downKey))
        {
            std::vector<int> order;
            for (int g = 0; g < s.nStages; ++g) for (int i = 0; i < s.st[g].n; ++i) order.push_back (s.st[g].slot[i]);
            for (size_t i = 0; i < order.size(); ++i)
                if (order[i] == slot)
                {
                    const int j = (int) i + (k.isKeyCode (KeyPress::downKey) ? 1 : -1);
                    if (j >= 0 && j < (int) order.size()) { host.select (order[(size_t) j]); host.focusSlot (order[(size_t) j]); }
                    break;
                }
            return true;
        }
        if (k.isKeyCode (KeyPress::deleteKey)) { host.removeSlot (slot); return true; }
        return false;
    }

private:
    RackHost& host;
    int slot, pairIdx;
    bool cp;
    const catr::UnitDef* u = nullptr;
    Colour col;
    OwnedArray<KnobWidget> knobs;
    OwnedArray<Tgl> tgls;
    Rectangle<int> gripR, nameR, dispR;
    std::atomic<float>* offP = nullptr;
    std::atomic<float>* muteP = nullptr;
};

// ---- the rows of units, with drag and drop ---------------------------------------------------------------------------
class RowsArea : public Component, public TipClient
{
public:
    explicit RowsArea (RackHost& h) : host (h) {}

    void rebuild()
    {
        cards.clear();
        s = host.proc().structure();
        used = s.unitCount();
        const int n = s.nStages, availH = getHeight();
        const int more = used < catr::kMaxUnits ? 1 : 0;
        int rh = 52, gp = 3;
        if (n > 0 && n * 52 + (n - 1) * 3 + (more ? 31 : 0) > availH)
        {
            gp = 2;
            rh = (availH - (n - 1) * 2 - (more ? 30 : 0)) / n;
            rh = jlimit (30, 52, rh);
        }
        const bool compact = rh < 46;
        rowRects.clear();
        int y = 0;
        for (int g = 0; g < n; ++g)
        {
            rowRects.push_back ({ 0, y, getWidth(), rh });
            const int cnt = s.st[g].n;
            const int cw = cnt == 2 ? (getWidth() - 6) / 2 : getWidth();
            for (int i = 0; i < cnt; ++i)
            {
                auto* c = cards.add (new SlotCard (host, s.st[g].slot[i], cnt == 2 ? i : -1, compact));
                c->onPress = [this] (SlotCard& cd, const MouseEvent& e) { press (cd, e); };
                c->onDrag = [this] (SlotCard& cd, const MouseEvent& e) { drag (cd, e); };
                c->onRelease = [this] (SlotCard& cd, const MouseEvent& e) { release (cd, e); };
                addAndMakeVisible (c);
                c->setBounds (i * (cw + 6), y, cw, rh);
                c->refreshAlpha();
            }
            y += rh + gp;
        }
        addRect = used < catr::kMaxUnits ? Rectangle<int> (0, y, getWidth(), 30) : Rectangle<int>();
        emptyRect = n == 0 ? Rectangle<int> (0, 0, getWidth(), 96) : Rectangle<int>();
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        if (! emptyRect.isEmpty())
        {
            dashed (g, emptyRect.toFloat(), 6.0f, t.line);
            auto f = theme::font (theme::FontKind::regular, 16.0f);
            drawText (g, "The rack is empty.", emptyRect.toFloat().withTrimmedTop (26.0f).withHeight (21.0f), f, t.dim);
            const String a = "Press ", b = "+ Add unit", c = ", or pick a chain preset with the arrows in the header.";
            const auto fb = theme::font (theme::FontKind::bold, 16.0f);
            const float wa = textWidth (f, a), wb = textWidth (fb, b), wc = textWidth (f, c), x0 = ((float) getWidth() - (wa + wb + wc)) * 0.5f, yy = 47.0f;
            drawText (g, a, { x0, yy, wa, 21.0f }, f, t.dim, Justification::centredLeft);
            drawText (g, b, { x0 + wa, yy, wb, 21.0f }, fb, t.ink, Justification::centredLeft);
            drawText (g, c, { x0 + wa + wb, yy, wc, 21.0f }, f, t.dim, Justification::centredLeft);
        }
        if (! addRect.isEmpty())
        {
            dashed (g, addRect.toFloat(), 5.0f, addHover ? theme::selb : t.line);
            drawText (g, String ("+ Add a unit ") + u8 ("\xc2\xb7") + " " + String (catr::kMaxUnits - used) + " places left", addRect.toFloat(),
                      theme::font (theme::FontKind::regular, 14.0f), addHover ? t.ink : t.dim, Justification::centred, 1.5f);
        }
    }

    void paintOverChildren (Graphics& g) override
    {
        if (! dragOn) return;
        if (tgt.mode == DropTarget::Par && tgt.row >= 0 && tgt.row < (int) rowRects.size())
        {
            auto r = rowRects[(size_t) tgt.row].toFloat();
            g.setColour (theme::selb.withAlpha (0.12f)); g.fillRoundedRectangle (r, 6.0f);
            dashed (g, r.reduced (1.0f), 6.0f, theme::selb, 2.0f);
        }
        else if (tgt.mode != DropTarget::None)
        {
            float y = 0.0f;
            if (tgt.mode == DropTarget::End && ! rowRects.empty()) y = (float) rowRects.back().getBottom() + 1.0f;
            else if (tgt.mode == DropTarget::Start && ! rowRects.empty()) y = (float) rowRects.front().getY() - 3.0f;
            else if (tgt.row >= 0 && tgt.row < (int) rowRects.size())
                y = tgt.mode == DropTarget::Before ? (float) rowRects[(size_t) tgt.row].getY() - 3.0f : (float) rowRects[(size_t) tgt.row].getBottom() + 1.0f;
            g.setColour (theme::selb.withAlpha (0.25f)); g.fillRoundedRectangle (0.0f, y - 3.0f, (float) getWidth(), 9.0f, 4.0f);
            g.setColour (theme::selb); g.fillRoundedRectangle (0.0f, y, (float) getWidth(), 3.0f, 2.0f);
        }
        // the ghost follows the pointer
        const auto f = theme::font (theme::FontKind::brand, 15.0f);
        const String text = ghostName + (tgt.mode == DropTarget::Par ? String ("  ") + u8 ("\xc2\xb7") + "  side by side" : String());
        const float w = textWidth (f, text) + 22.0f;
        Rectangle<float> gr (ghostPos.x + 12.0f, ghostPos.y + 8.0f, w, 28.0f);
        g.setColour (theme::sel.withAlpha (0.92f)); g.fillRoundedRectangle (gr, 5.0f);
        g.setColour (theme::selb.withAlpha (0.92f)); g.drawRoundedRectangle (gr.reduced (0.5f), 5.0f, 1.0f);
        drawText (g, text, gr, f, host.context().t().ink, Justification::centred);
    }

    void mouseMove (const MouseEvent& e) override { const bool h = addRect.contains (e.getPosition()); if (h != addHover) { addHover = h; repaint(); } }
    void mouseExit (const MouseEvent&) override { if (addHover) { addHover = false; repaint(); } }
    void mouseUp (const MouseEvent& e) override { if (addRect.contains (e.getPosition()) && e.mouseWasClicked()) host.openAdd(); }
    String tipAt (Point<int> p) const override
    {
        if (addRect.contains (p)) return "Open the unit browser to add something to the end of the rack.";
        return {};
    }
    Rectangle<int> tipBoundsAt (Point<int> p) const override { return addRect.contains (p) ? addRect : Rectangle<int>(); }

    SlotCard* cardFor (int slot) const { for (auto* c : cards) if (c->slotIndex() == slot) return c; return nullptr; }
    void repaintCards() { for (auto* c : cards) { c->refreshAlpha(); c->repaint(); } }

    static void dashed (Graphics& g, Rectangle<float> r, float radius, Colour c, float thick = 1.0f)
    {
        Path p; p.addRoundedRectangle (r.reduced (0.5f), radius);
        const float d[2] = { 4.0f, 3.0f };
        Path out;
        PathStrokeType (thick).createDashedStroke (out, p, d, 2);
        g.setColour (c); g.fillPath (out);
    }

private:
    void press (SlotCard& c, const MouseEvent& e) { dragSlot = c.slotIndex(); dragStart = e.getEventRelativeTo (this).position; dragOn = false; c.dragging = false; }
    void drag (SlotCard& c, const MouseEvent& e)
    {
        const auto p = e.getEventRelativeTo (this).position;
        if (! dragOn)
        {
            if (p.getDistanceFrom (dragStart) < 5.0f) return;
            dragOn = true; c.dragging = true; c.setAlpha (0.3f);
            ghostName = String::fromUTF8 (catr::unitTable()[host.proc().slotType (dragSlot)].name);
        }
        ghostPos = p;
        tgt = targetFor ((int) p.y);
        repaint();
    }
    void release (SlotCard& c, const MouseEvent&)
    {
        if (dragOn)
        {
            dragOn = false; c.dragging = false; c.setAlpha (1.0f);
            const DropTarget t = tgt; const int sl = dragSlot;
            tgt = {}; repaint();
            if (t.mode != DropTarget::None)
            {
                SafePointer<RowsArea> sp (this);
                MessageManager::callAsync ([sp, this, t, sl] { if (sp != nullptr) host.dropSlot (sl, t); });
            }
        }
        else host.select (c.slotIndex());
    }
    DropTarget targetFor (int y) const
    {
        DropTarget t;
        if (rowRects.empty()) { t.mode = DropTarget::End; return t; }
        int best = 0, bd = 1 << 30;
        for (size_t i = 0; i < rowRects.size(); ++i)
        {
            const auto& b = rowRects[i];
            const int d = y < b.getY() ? b.getY() - y : y > b.getBottom() ? y - b.getBottom() : 0;
            if (d < bd) { bd = d; best = (int) i; }
        }
        const auto& b = rowRects[(size_t) best];
        if (best == (int) rowRects.size() - 1 && y > b.getBottom() + 6) { t.mode = DropTarget::End; return t; }
        if (best == 0 && y < b.getY() - 6) { t.mode = DropTarget::Start; return t; }
        const float rel = (float) (y - b.getY()) / (float) b.getHeight();
        const int own = s.stageOf (dragSlot);
        t.row = best;
        if (rel < 0.28f) { t.mode = DropTarget::Before; return t; }
        if (rel > 0.72f) { t.mode = DropTarget::After; return t; }
        if (best != own && s.st[best].n == 1) { t.mode = DropTarget::Par; return t; }
        if (best == own && s.st[own].n == 1) { t.mode = DropTarget::None; return t; }
        t.mode = rel < 0.5f ? DropTarget::Before : DropTarget::After;
        return t;
    }

    RackHost& host;
    OwnedArray<SlotCard> cards;
    catr::Structure s;
    int used = 0;
    std::vector<Rectangle<int>> rowRects;
    Rectangle<int> addRect, emptyRect;
    bool addHover = false;
    int dragSlot = -1;
    Point<float> dragStart, ghostPos;
    bool dragOn = false;
    DropTarget tgt;
    String ghostName;
};
} // namespace ui
