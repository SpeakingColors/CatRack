#pragma once
#include "Views.h"

namespace ui
{
inline float dbOfLin (float x) { return x <= 1.0e-4f ? -90.0f : 20.0f * std::log10 (x); }

// ---- signal path strip -------------------------------------------------------------------------------------
class Chip : public Component, public TipClient
{
public:
    Chip (RackHost& h, int slotIdx, int row, int lane, bool inPair) : host (h), slot (slotIdx)
    {
        u = &catr::unitTable()[host.proc().slotType (slot)];
        tip = String::fromUTF8 (u->name) + (inPair ? String (", lane ") + (lane ? "B" : "A") : String()) + ": click to open its controls.";
        onP = host.proc().apvts.getRawParameterValue (slotId (slot, "on"));
        soloP = host.proc().apvts.getRawParameterValue (slotId (slot, "solo"));
        muteP = host.proc().apvts.getRawParameterValue (slotId (slot, "mute"));
        setMouseCursor (MouseCursor::PointingHandCursor);
        juce::ignoreUnused (row);
    }
    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        const bool sel = host.selected() == slot, off = onP->load() < 0.5f, solo = soloP->load() > 0.5f, mute = muteP->load() > 0.5f;
        auto r = getLocalBounds().toFloat();
        if (solo || mute)
        {
            g.setColour (mute ? theme::red : t.yellow);
            g.drawRoundedRectangle (r.expanded (1.0f), 5.0f, 1.0f);
        }
        const float a = off ? 0.4f : 1.0f;
        g.setColour ((sel ? theme::sel : t.plate).withMultipliedAlpha (a)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour ((sel ? theme::selb : t.line).withMultipliedAlpha (a)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (Colour (u->color).withMultipliedAlpha (a)); g.fillEllipse (5.0f, ((float) getHeight() - 7.0f) * 0.5f, 7.0f, 7.0f);
        drawText (g, String::fromUTF8 (u->code), { 16.0f, 0.0f, (float) getWidth() - 16.0f, (float) getHeight() }, theme::font (theme::FontKind::bold, 12.0f),
                  t.ink.withMultipliedAlpha (a), Justification::centredLeft, 0.5f);
    }
    void mouseUp (const MouseEvent& e) override { if (e.mouseWasClicked()) host.select (slot); }
private:
    RackHost& host;
    int slot;
    const catr::UnitDef* u = nullptr;
    std::atomic<float>*onP = nullptr, *soloP = nullptr, *muteP = nullptr;
};

class StripView : public Component, public TipClient
{
public:
    explicit StripView (RackHost& h) : host (h), inKnob (h.context(), KnobWidget::Mini, 32), outKnob (h.context(), KnobWidget::Mini, 32)
    {
        for (auto* k : { &inKnob, &outKnob }) { k->labelH = 11.0f; addAndMakeVisible (k); k->onEdited = [this] { host.edited(); }; }
        inKnob.labelOverride = "IN"; outKnob.labelOverride = "OUT";
        inKnob.bind (paramBinding (host.proc(), ids::inTrim, inTrimDef()), Colour (0xff6CC9F5));
        outKnob.bind (paramBinding (host.proc(), ids::outTrim, outTrimDef()), Colour (0xff6CC9F5));
    }

    void resized() override
    {
        inKnob.setBounds (9 + 6, 23, 32, 44); outKnob.setBounds (getWidth() - 1 - 8 - 44 + 6, 23, 32, 44);
        rebuild();
    }

    void rebuild()
    {
        chips.clear(); stages.clear(); arrows.clear();
        s = host.proc().structure();
        const int n = s.nStages;
        const float availW = (float) getWidth() - 2.0f - 16.0f - 88.0f - 8.0f;
        float sum = 0.0f;
        for (int g = 0; g < n; ++g) sum += 42.0f + (s.st[g].n > 1 ? 10.0f : 0.0f);
        float f = 1.0f;
        if (n > 0 && 9.0f * (float) (n + 1) + sum > availW) f = jmax (0.7f, (availW - 9.0f * (float) (n + 1)) / sum);
        const float chipW = std::floor (42.0f * f);
        float total = 9.0f * (float) (n + 1);
        for (int g = 0; g < n; ++g) total += chipW + (s.st[g].n > 1 ? 10.0f : 0.0f);
        if (n == 0) total = 0.0f;
        float x = 1.0f + 8.0f + 44.0f + 4.0f + (availW - total) * 0.5f;
        const float cy = 45.0f;
        emptyRect = {};
        if (n == 0) { emptyRect = Rectangle<float> (57.0f, cy - 10.0f, availW, 20.0f); return; }
        arrows.push_back ({ x + 4.5f, cy }); x += 9.0f;
        for (int g = 0; g < n; ++g)
        {
            const bool pr = s.st[g].n > 1;
            Stage sg;
            sg.pair = pr; sg.row = g;
            const float bw = chipW + (pr ? 10.0f : 0.0f);
            if (pr)
            {
                sg.box = { x, cy - 34.0f, bw, 68.0f };
                for (int i = 0; i < 2; ++i)
                {
                    auto* c = chips.add (new Chip (host, s.st[g].slot[i], g, i, true));
                    addAndMakeVisible (c);
                    c->setBounds (roundToInt (x + 5.0f), roundToInt (cy - 34.0f + 4.0f + (float) i * 25.0f), roundToInt (chipW), 22);
                }
                sg.label = { x, cy - 34.0f + 4.0f + 50.0f + 3.0f, bw, 11.0f };
                sg.mixId = "smix" + String (g);
                sg.mixP = host.proc().apvts.getRawParameterValue (sg.mixId);
            }
            else
            {
                auto* c = chips.add (new Chip (host, s.st[g].slot[0], g, 0, false));
                addAndMakeVisible (c);
                c->setBounds (roundToInt (x), roundToInt (cy - 11.0f), roundToInt (chipW), 22);
            }
            stages.push_back (sg);
            x += bw;
            arrows.push_back ({ x + 4.5f, cy }); x += 9.0f;
        }
        repaint();
    }

    void tick()
    {
        bool changed = false;
        for (size_t i = 0; i < arrows.size(); ++i)
        {
            const float lv = i == 0 ? host.inLevelDb() : host.rowLevelDb ((int) i - 1);
            const float a = jlimit (0.2f, 1.0f, (lv + 70.0f) / 60.0f);
            if (std::abs (a - arrows[i].alpha) > 0.02f) { arrows[i].alpha = a; changed = true; }
        }
        for (auto& sg : stages)
            if (sg.pair && sg.mixP != nullptr && (int) std::lround (sg.mixP->load()) != sg.shownMix) { sg.shownMix = (int) std::lround (sg.mixP->load()); changed = true; }
        if (changed) repaint();
    }

    String tipAt (Point<int> p) const override
    {
        for (auto& sg : stages)
            if (sg.pair && sg.box.contains (p.toFloat()))
                return "Parallel pair: the two units run side by side and are added together. Dry/Wet is " + String (sg.shownMix) + "%.";
        return {};
    }
    Rectangle<int> tipBoundsAt (Point<int> p) const override
    {
        for (auto& sg : stages) if (sg.pair && sg.box.contains (p.toFloat())) return sg.box.toNearestInt();
        return {};
    }

    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        auto r = getLocalBounds().toFloat();
        g.setColour (t.plate2); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        drawText (g, "SIGNAL PATH", { 11.0f, 3.0f, 200.0f, 13.0f }, theme::font (theme::FontKind::regular, 11.0f), t.title, Justification::centredLeft, 1.5f);
        drawText (g, "Left to right, top lane first", { r.getWidth() - 11.0f - 300.0f, 3.0f, 300.0f, 13.0f }, theme::font (theme::FontKind::regular, 11.0f), t.dim, Justification::centredRight, 1.0f);
        if (! emptyRect.isEmpty())
            drawText (g, "Nothing in the rack: the signal goes straight through.", emptyRect, theme::font (theme::FontKind::regular, 13.0f), t.dim);
        for (auto& sg : stages)
        {
            if (! sg.pair) continue;
            g.setColour (t.disp); g.fillRoundedRectangle (sg.box, 7.0f);
            g.setColour (t.line); g.drawRoundedRectangle (sg.box.reduced (0.5f), 7.0f, 1.0f);
            drawText (g, "mix " + String (sg.shownMix) + "%", sg.label, theme::font (theme::FontKind::mono, 9.0f), t.dim);
        }
        for (auto& a : arrows)
        {
            Path p; p.addTriangle (a.x - 2.5f, a.y - 3.5f, a.x - 2.5f, a.y + 3.5f, a.x + 3.0f, a.y);
            g.setColour (t.accent.withAlpha (a.alpha)); g.fillPath (p);
        }
    }

private:
    struct Arrow { float x, y; float alpha = 1.0f; };
    struct Stage { bool pair = false; int row = 0; Rectangle<float> box, label; String mixId; std::atomic<float>* mixP = nullptr; int shownMix = 100; };
    RackHost& host;
    KnobWidget inKnob, outKnob;
    OwnedArray<Chip> chips;
    std::vector<Stage> stages;
    std::vector<Arrow> arrows;
    catr::Structure s;
    Rectangle<float> emptyRect;
};

// ---- the selected unit's controls and display --------------------------------------------------------------------
class SegPair : public Component, public TipClient
{
public:
    SegPair (const Ctx& c, String a, String b) : ctx (c) { labels[0] = std::move (a); labels[1] = std::move (b); setMouseCursor (MouseCursor::PointingHandCursor); }
    std::function<void (int)> onPick;
    int selectedIdx = 0;
    bool disabled[2] { false, false };
    String tips[2];
    String tipAt (Point<int> p) const override { return tips[p.x < getWidth() / 2 ? 0 : 1]; }
    Rectangle<int> tipBoundsAt (Point<int> p) const override { return p.x < getWidth() / 2 ? getLocalBounds().withWidth (getWidth() / 2) : getLocalBounds().withTrimmedLeft (getWidth() / 2); }
    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        auto r = getLocalBounds().toFloat();
        Path clip; clip.addRoundedRectangle (r.reduced (0.5f), 4.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);
            for (int i = 0; i < 2; ++i)
            {
                auto h = r.withWidth (r.getWidth() * 0.5f).withX (r.getX() + (float) i * r.getWidth() * 0.5f);
                const float a = disabled[i] ? 0.4f : 1.0f;
                g.setColour ((selectedIdx == i ? theme::sel : t.plate).withMultipliedAlpha (a)); g.fillRect (h);
                drawText (g, labels[i], h, theme::font (theme::FontKind::regular, 13.0f), (selectedIdx == i ? theme::lilac : t.ink).withMultipliedAlpha (a));
            }
        }
        g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const int i = e.x < getWidth() / 2 ? 0 : 1;
        if (! disabled[i] && onPick) onPick (i);
    }
private:
    const Ctx& ctx;
    String labels[2];
};

class DetailPanel : public Component, public TipClient
{
public:
    explicit DetailPanel (RackHost& h) : host (h) {}

    void rebuild()
    {
        kids.clear(); knobs.clear();
        slot = host.selected();
        s = host.proc().structure();
        row = slot >= 0 ? s.stageOf (slot) : -1;
        if (row < 0 || host.proc().slotType (slot) < 0) { slot = -1; row = -1; repaint(); return; }
        type = host.proc().slotType (slot);
        u = &catr::unitTable()[type];
        col = Colour (u->color);
        const bool pr = s.st[row].n > 1;
        lane = pr ? (s.st[row].slot[0] == slot ? 0 : 1) : -1;
        auto& ctx = host.context();
        const float x0 = 9.0f, w = 271.0f;
        float y = 8.0f;
        headRect = { x0, y, w, 51.0f };
        y += 51.0f + 7.0f;
        // ON / SOLO / MUTE and Remove
        {
            const char* names[3] = { "ON", "SOLO", "MUTE" };
            const char* tails[3] = { "on", "solo", "mute" };
            const Tgl::Kind kinds[3] = { Tgl::On, Tgl::Solo, Tgl::Mute };
            const char* tips[3] = { layout::tipOn, "Solo: hear only the soloed units. All other units step out of the way.", "Mute: silence this unit. In a parallel pair only this lane goes quiet." };
            float x = x0;
            for (int i = 0; i < 3; ++i)
            {
                auto* t = new Tgl (ctx, names[i], kinds[i], 12.0f);
                kids.add (t); addAndMakeVisible (t);
                t->bind (host.proc().apvts.getParameter (slotId (slot, tails[i])));
                t->tip = tips[i];
                t->onEdited = [this] { host.edited(); };
                const int tw = roundToInt (textWidth (theme::font (theme::FontKind::bold, 12.0f), names[i])) + 18 + 2;
                t->setBounds (roundToInt (x), roundToInt (y), tw, 24);
                x += (float) tw + 4.0f;
            }
            auto* rm = new PButton (ctx, "Remove", 13.0f);
            kids.add (rm); addAndMakeVisible (rm);
            const int rw = roundToInt (textWidth (theme::font (theme::FontKind::regular, 13.0f), "Remove")) + 16 + 2 + 3;
            rm->setBounds (roundToInt (x0 + w) - rw, roundToInt (y) + 1, rw, 22);
            rm->tip = "Take this unit out of the rack.";
            rm->onClick = [this] (const ModifierKeys&) { host.removeSlot (slot); };
        }
        y += 24.0f + 7.0f;
        // knob grid
        const int n = u->nParams;
        const int ks = n > 6 ? 40 : 52;
        const float colW = w / 3.0f;
        for (int i = 0; i < n; ++i)
        {
            auto* k = new KnobWidget (ctx, KnobWidget::Detail, ks);
            knobs.add (k); addAndMakeVisible (k);
            k->bind (slotBinding (host.proc(), slot, i), col);
            k->onEdited = [this] { host.edited(); };
            const int cx = i % 3, cr = i / 3;
            k->setBounds (roundToInt (x0 + (float) cx * colW), roundToInt (y + (float) cr * (15.0f + (float) ks + 12.0f + 4.0f)), roundToInt (colW), 15 + ks + 12);
        }
        const int rowsN = (n + 2) / 3;
        y += (float) rowsN * (15.0f + (float) ks + 12.0f) + (float) (rowsN - 1) * 4.0f + 7.0f;
        capRect = { x0, y, w, 13.0f };
        y += 13.0f + 7.0f;
        dispRect = { x0, y, 270.0f, n > 6 ? 52.0f : 66.0f };
        y += dispRect.getHeight() + 7.0f;
        // Earlier / Later / series-or-parallel
        {
            auto* e = new PButton (ctx, "Earlier", 13.0f); e->glyph = 3;
            auto* l = new PButton (ctx, "Later", 13.0f); l->glyph = 4;
            kids.add (e); kids.add (l); addAndMakeVisible (e); addAndMakeVisible (l);
            const int ew = roundToInt (textWidth (theme::font (theme::FontKind::regular, 13.0f), "Earlier")) + 12 + 2 + 18;
            const int lw = roundToInt (textWidth (theme::font (theme::FontKind::regular, 13.0f), "Later")) + 12 + 2 + 18;
            e->setBounds (roundToInt (x0), roundToInt (y), ew, 24);
            l->setBounds (roundToInt (x0) + ew + 4, roundToInt (y), lw, 24);
            e->tip = "Move this row one place earlier in the chain (Alt+Up).";
            l->tip = "Move this row one place later in the chain (Alt+Down).";
            e->setDisabled (row == 0); l->setDisabled (row == s.nStages - 1);
            e->onClick = [this] (const ModifierKeys&) { host.moveRow (row, -1); };
            l->onClick = [this] (const ModifierKeys&) { host.moveRow (row, 1); };
            auto* sg = new SegPair (ctx, "In series", "Beside above");
            kids.add (sg); addAndMakeVisible (sg);
            const int sx = roundToInt (x0) + ew + 4 + lw + 4;
            sg->setBounds (sx, roundToInt (y), roundToInt (x0 + w) - sx, 24);
            sg->selectedIdx = pr ? 1 : 0;
            const bool can = pr || (row > 0 && s.st[row - 1].n == 1 && s.st[row].n == 1);
            sg->disabled[1] = ! can;
            sg->tips[0] = "In series: the signal goes through this unit on its own row, one after another.";
            sg->tips[1] = can ? "Beside above: run this unit side by side with the unit above (parallel). You get lane levels and a Dry/Wet."
                              : "Not possible here: there is no single unit above, or it already has a partner.";
            sg->onPick = [this, pr] (int i) { if (i == 0 && pr) host.splitOut (slot); else if (i == 1 && ! pr) host.joinAbove (slot); };
        }
        y += 24.0f + 7.0f;
        hintRect = {}; sectRect = {};
        if (pr)
        {
            const int other = s.st[row].slot[1 - lane];
            sectText = "Parallel pair: lane " + String (lane ? "B" : "A") + " with " + String::fromUTF8 (catr::unitTable()[host.proc().slotType (other)].name);
            sectRect = { x0, y + 2.0f, w, 13.0f };
            y += 2.0f + 13.0f + 7.0f;
            const Binding bs[3] = { paramBinding (host.proc(), slotId (slot, "lane").toRawUTF8(), laneDef()),
                                    paramBinding (host.proc(), slotId (other, "lane").toRawUTF8(), otherLaneDef()),
                                    paramBinding (host.proc(), ("smix" + String (row)).toRawUTF8(), pairMixDef()) };
            for (int i = 0; i < 3; ++i)
            {
                auto* k = new KnobWidget (ctx, KnobWidget::Detail, 40);
                k->labelSize = 12.0f;
                knobs.add (k); addAndMakeVisible (k);
                k->bind (bs[i], col);
                k->onEdited = [this] { host.edited(); };
                k->setBounds (roundToInt (x0 + (float) i * colW), roundToInt (y), roundToInt (colW), 15 + 40 + 12);
            }
        }
        else hintRect = { x0, y, w, 78.0f };
        repaint();
    }

    void tick()
    {
        if (slot < 0) return;
        repaint (dispRect.getSmallestIntegerContainer().expanded (2));
        repaint (capRect.getSmallestIntegerContainer().expanded (2));
    }

    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        auto r = getLocalBounds().toFloat();
        g.setColour (t.plate2); g.fillRect (r);
        g.setColour (t.line); g.fillRect (0.0f, 0.0f, 1.0f, r.getHeight());
        if (slot < 0)
        {
            Rectangle<float> box (9.0f, 8.0f, 271.0f, 96.0f);
            RowsArea::dashed (g, box, 6.0f, t.line);
            drawText (g, "No unit selected.", box.withTrimmedTop (26.0f).withHeight (21.0f), theme::font (theme::FontKind::regular, 16.0f), t.dim);
            drawText (g, "Click a unit in the rack: its controls appear here.", box.withTrimmedTop (47.0f).withHeight (21.0f), theme::font (theme::FontKind::regular, 14.0f), t.dim);
            return;
        }
        // header
        g.setColour (col); g.fillEllipse (headRect.getX(), headRect.getY() + 20.0f, 10.0f, 10.0f);
        const float hx = headRect.getX() + 18.0f;
        drawText (g, "SELECTED UNIT", { hx, headRect.getY(), 250.0f, 13.0f }, theme::font (theme::FontKind::regular, 11.0f), t.accent, Justification::centredLeft, 1.5f);
        drawText (g, String::fromUTF8 (u->name), { hx, headRect.getY() + 14.0f, 250.0f, 22.0f }, theme::font (theme::FontKind::brand, 22.0f), t.ink, Justification::centredLeft);
        drawText (g, (String::fromUTF8 (u->kind) + " " + u8 ("\xc2\xb7") + " " + String::fromUTF8 (u->cat)).toUpperCase(), { hx, headRect.getY() + 38.0f, 250.0f, 13.0f },
                  theme::font (theme::FontKind::regular, 11.0f), t.dim, Justification::centredLeft, 1.5f);
        // caption and display
        const String cap = u->viz == catr::VizEq ? "EQ response" : u->viz == catr::VizWet ? "Output level, last 6 s" : "Gain reduction, last 6 s";
        drawText (g, cap.toUpperCase(), capRect.withWidth (170.0f), theme::font (theme::FontKind::regular, 11.0f), t.title, Justification::centredLeft, 1.5f);
        const auto& L = host.live (slot);
        float p[6] {};
        for (int k = 0; k < 6; ++k) p[k] = host.proc().slotReal (slot, k);
        String read;
        if (u->viz == catr::VizEq)
        {
            float v = (catr::eqDisplayDb (type, p, 60.0f) + catr::eqDisplayDb (type, p, 8000.0f) + catr::eqDisplayDb (type, p, 300.0f)) / 3.0f * 0.5f;
            if (u->nParams > 5 && type == catr::U_PEQ) v += p[5];
            read = "level " + String (v >= 0 ? "+" : "") + String (v, 1) + " dB";
        }
        else if (u->viz == catr::VizWet) read = String (jmax (-99.0f, L.outDb), 1) + " dB";
        else read = "-" + String (L.gr, 1) + " dB";
        drawText (g, read, { capRect.getRight() - 120.0f, capRect.getY(), 120.0f, 13.0f }, theme::font (theme::FontKind::mono, 11.0f), t.ink, Justification::centredRight);
        g.setColour (t.disp); g.fillRoundedRectangle (dispRect, 3.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::translation (dispRect.getX() + 1.0f, dispRect.getY() + 1.0f));
            g.reduceClipRegion (0, 0, (int) dispRect.getWidth() - 2, (int) dispRect.getHeight() - 2);
            paintBig (g, host.context(), *u, type, p, L, dispRect.getWidth() - 2.0f, dispRect.getHeight() - 2.0f);
        }
        g.setColour (t.line); g.drawRoundedRectangle (dispRect.reduced (0.5f), 3.0f, 1.0f);
        if (! sectRect.isEmpty())
            drawText (g, sectText.toUpperCase(), sectRect, theme::font (theme::FontKind::regular, 11.0f), t.title, Justification::centredLeft, 1.5f);
        if (! hintRect.isEmpty())
        {
            AttributedString as; as.setWordWrap (AttributedString::byWord); as.setJustification (Justification::centredLeft); as.setLineSpacing (1.0f);
            as.append ("Run this unit beside the one above (Beside above) to make a parallel pair. A pair gets a level for each lane and a Dry/Wet.",
                       theme::font (theme::FontKind::regular, 13.0f), t.dim);
            as.draw (g, hintRect.reduced (0.0f, 16.0f));
        }
    }

    String tipAt (Point<int> p) const override
    {
        if (slot >= 0 && headRect.contains (p.toFloat())) return String::fromUTF8 (u->blurb);
        if (slot >= 0 && dispRect.contains (p.toFloat()))
            return u->viz == catr::VizEq ? "The EQ curve of this unit. Across: frequency from 20 Hz to 20 kHz. Up: boost or cut, plus or minus 12 dB."
                 : u->viz == catr::VizWet ? "The output level of this unit over the last six seconds, in dB. 0 at the top, -40 at the bottom."
                 : "How much this unit turned the signal down over the last six seconds, in dB. 0 at the top, -20 at the bottom.";
        return {};
    }
    Rectangle<int> tipBoundsAt (Point<int> p) const override
    {
        if (slot >= 0 && headRect.contains (p.toFloat())) return headRect.toNearestInt();
        if (slot >= 0 && dispRect.contains (p.toFloat())) return dispRect.toNearestInt();
        return {};
    }

private:
    RackHost& host;
    int slot = -1, row = -1, lane = -1, type = 0;
    const catr::UnitDef* u = nullptr;
    Colour col;
    catr::Structure s;
    OwnedArray<Component> kids;
    OwnedArray<KnobWidget> knobs;
    Rectangle<float> headRect, capRect, dispRect, sectRect, hintRect;
    String sectText;
};

// ---- in / out meters ---------------------------------------------------------------------------------------------
class MeterView : public Component, public TipClient
{
public:
    explicit MeterView (const Ctx& c) : ctx (c) {}
    std::function<void()> onClear;

    // peaks are linear, the largest value since the last call
    void update (float inL, float inR, float outL, float outR, float dt)
    {
        const float lv[4] = { dbOfLin (inL), dbOfLin (inR), dbOfLin (outL), dbOfLin (outR) };
        for (int i = 0; i < 4; ++i)
        {
            hold[i] = lv[i] > hold[i] ? lv[i] : jmax (lv[i], hold[i] - 28.0f * dt);
            if (lv[i] > pk[i]) pk[i] = lv[i];
            if (lv[i] >= 0.0f) (i < 2 ? clipIn : clipOut) = true;
        }
        repaint();
    }
    void clear() { for (auto& p : pk) p = -90.0f; clipIn = clipOut = false; repaint(); if (onClear) onClear(); }
    float peakIn() const { return jmax (pk[0], pk[1]); }
    float peakOut() const { return jmax (pk[2], pk[3]); }
    static Colour zoneCol (float d) { return d >= 0.0f ? theme::red : d >= -12.0f ? theme::purple : theme::blue; }

    void mouseUp (const MouseEvent& e) override { if (e.mouseWasClicked() && canvas().contains (e.getPosition())) clear(); }
    String tipAt (Point<int> p) const override { return canvas().contains (p) ? layout::tipMeter : String(); }
    Rectangle<int> tipBoundsAt (Point<int> p) const override { return canvas().contains (p) ? canvas() : Rectangle<int>(); }

    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        const auto cap = theme::font (theme::FontKind::regular, 12.0f);
        drawText (g, "IN", { 0.0f, 0.0f, 30.0f, 14.0f }, cap, t.title, Justification::centred, 1.5f);
        drawText (g, "OUT", { 54.0f, 0.0f, 30.0f, 14.0f }, cap, t.title, Justification::centred, 1.5f);
        {
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::translation (0.0f, 17.0f));
            paintCanvas (g);
        }
        const auto mf = theme::font (theme::FontKind::mono, 10.0f);
        for (int i = 0; i < 2; ++i)
        {
            const float d = i == 0 ? peakIn() : peakOut();
            drawText (g, d <= -90.0f ? String (u8 ("-\xe2\x88\x9e")) : (d > 0 ? "+" : "") + String (d, 1), { i == 0 ? 0.0f : 54.0f, 520.0f, 30.0f, 13.0f }, mf,
                      d <= -90.0f ? t.dim : zoneCol (d));
        }
    }

private:
    static constexpr float MW = 84.0f, MH = 500.0f, MTOP = 8.0f, MBOT = 494.0f, DBMIN = -48.0f, DBMAX = 6.0f;
    static float mY (float d) { return MBOT - (jlimit (DBMIN, DBMAX, d) - DBMIN) / (DBMAX - DBMIN) * (MBOT - MTOP); }
    Rectangle<int> canvas() const { return { 0, 17, 84, 500 }; }

    void paintCanvas (Graphics& g)
    {
        auto& t = ctx.t();
        const float bars[4] = { 0.0f, 15.0f, 55.0f, 70.0f };
        const float bw = 14.0f;
        ColourGradient gr (theme::red, 0.0f, mY (DBMAX), theme::blue, 0.0f, mY (DBMIN), false);
        auto fr = [] (float d) { return (mY (d) - mY (DBMAX)) / (mY (DBMIN) - mY (DBMAX)); };
        const double e = 0.002;
        gr.clearColours();
        gr.addColour (0.0, theme::red); gr.addColour (fr (0.0f) - e, theme::red);
        gr.addColour (fr (0.0f), theme::purple); gr.addColour (fr (-12.0f) - e, theme::purple);
        gr.addColour (fr (-12.0f), theme::blue); gr.addColour (1.0, theme::blue);
        for (int i = 0; i < 4; ++i)
        {
            const float x = bars[i] + 0.5f;
            g.setColour (t.disp); g.fillRect (x, MTOP, bw, MBOT - MTOP);
            const float v = hold[i], y = mY (v);
            if (v > DBMIN)
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (Rectangle<int> (roundToInt (x), roundToInt (y), roundToInt (bw), roundToInt (MBOT - y)));
                g.setGradientFill (gr); g.fillRect (x, MTOP, bw, MBOT - MTOP);
            }
            if (pk[i] > DBMIN) { g.setColour (zoneCol (pk[i])); g.fillRect (x, std::round (mY (pk[i])), bw, 2.0f); }
            g.setColour (t.line); g.drawRect (x, MTOP + 0.5f, bw, MBOT - MTOP - 1.0f, 1.0f);
        }
        const bool clips[4] = { clipIn, clipIn, clipOut, clipOut };
        for (int i = 0; i < 4; ++i) { g.setColour (theme::red.withAlpha (clips[i] ? 1.0f : 0.18f)); g.fillRect (bars[i] + 0.5f, 1.0f, bw, 5.0f); }
        const auto f9 = theme::font (theme::FontKind::mono, 9.0f);
        for (int d = (int) DBMAX; d >= (int) DBMIN; d -= 3)
        {
            const float y = std::floor (mY ((float) d)) + 0.5f;
            const bool major = d % 6 == 0;
            g.setColour (t.dim.withAlpha (major ? 0.9f : 0.45f));
            g.fillRect (30.0f, y - 0.5f, major ? 4.0f : 2.0f, 1.0f);
            g.fillRect (major ? 50.0f : 52.0f, y - 0.5f, major ? 4.0f : 2.0f, 1.0f);
            g.setColour (Colours::white.withAlpha (major ? 0.16f : 0.07f));
            g.fillRect (0.5f, y - 0.5f, 29.0f, 1.0f); g.fillRect (54.5f, y - 0.5f, MW - 55.0f, 1.0f);
            if (major) drawText (g, (d > 0 ? "+" : "") + String (d), { 30.0f, y - 6.0f, 24.0f, 12.0f }, f9, d >= 0 ? theme::pink : t.dim);
        }
    }

    const Ctx& ctx;
    float hold[4] { -90, -90, -90, -90 }, pk[4] { -90, -90, -90, -90 };
    bool clipIn = false, clipOut = false;
};
} // namespace ui
