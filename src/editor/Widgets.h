#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "Theme.h"
#include "../core/Rack.h"
#include "../core/Fmt.h"

namespace ui
{
using namespace juce;

// Settings every widget needs to paint (look and info bubbles).
struct Ctx
{
    char look = 'C';
    bool tips = true;
    bool power = false;
    const theme::Look& t() const { return theme::look (look); }
};

// Anything that can show an info bubble.
struct TipClient
{
    String tip;
    virtual ~TipClient() = default;
    // Components with several parts can give a different bubble for each (position in their own coordinates).
    virtual String tipAt (Point<int>) const { return tip; }
    virtual Rectangle<int> tipBoundsAt (Point<int>) const { return {}; }   // empty: the whole component
};

inline String u8 (const char* s) { return String::fromUTF8 (s); }

inline float textWidth (const Font& f, const String& s)
{
    GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// Text in a box, never shortened with dots. Letter spacing is in px.
inline void drawText (Graphics& g, const String& s, Rectangle<float> r, Font f, Colour c,
                      Justification j = Justification::centred, float spacingPx = 0.0f)
{
    if (std::abs (spacingPx) > 1.0e-6f) f = f.withExtraKerningFactor (spacingPx / jmax (1.0f, f.getHeight())); // JUCE kerning is a fraction of the font height
    g.setFont (f);
    g.setColour (c);
    Rectangle<float> box = r.expanded (30.0f, 0.0f); // room for font differences, text is never shortened
    if (j.testFlags (Justification::left))       box = r.withWidth (r.getWidth() + 60.0f);
    else if (j.testFlags (Justification::right)) box = r.withLeft (r.getX() - 60.0f);
    g.drawText (s, box, j, false);
}

// ---- Button in the mockup's .btn style -----------------------------------------------------------
class PButton : public Component, public TipClient
{
public:
    PButton (const Ctx& c, String t, float px = 14.0f, theme::FontKind k = theme::FontKind::regular)
        : text (std::move (t)), ctx (c), size (px), kind (k) { setMouseCursor (MouseCursor::PointingHandCursor); }

    std::function<void (const ModifierKeys&)> onClick;
    std::function<void()> onDouble;
    bool on = false;
    bool disabled = false;
    int glyph = 0;               // 1: triangle left, 2: triangle right, 3: triangle up then text, 4: text then triangle down
    float spacing = 0.5f;
    float radius = 4.0f;
    String text;

    void setText (const String& t) { if (t != text) { text = t; repaint(); } }
    void setOn (bool v) { if (v != on) { on = v; repaint(); } }
    void setDisabled (bool v) { if (v != disabled) { disabled = v; setMouseCursor (v ? MouseCursor::NormalCursor : MouseCursor::PointingHandCursor); repaint(); } }

    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setOpacity (1.0f);
        const float a = disabled ? 0.4f : 1.0f;
        g.setColour ((on ? theme::sel : t.plate).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, radius);
        g.setColour ((on ? theme::selb : (over && ! disabled ? t.dim : t.line)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (r, radius, 1.0f);
        const Colour tc = (on ? theme::lilac : t.ink).withMultipliedAlpha (a);
        const auto f = theme::font (kind, size);
        if (glyph == 0) { drawText (g, text, r, f, tc, Justification::centred, spacing); }
        else
        {
            auto tri = [&g, tc] (float cx, float cy, int dir)
            {
                Path p;
                if (dir == 1)      { p.addTriangle (cx + 3.5f, cy - 4.5f, cx + 3.5f, cy + 4.5f, cx - 3.5f, cy); }
                else if (dir == 2) { p.addTriangle (cx - 3.5f, cy - 4.5f, cx - 3.5f, cy + 4.5f, cx + 3.5f, cy); }
                else if (dir == 3) { p.addTriangle (cx - 4.5f, cy + 3.5f, cx + 4.5f, cy + 3.5f, cx, cy - 3.5f); }
                else               { p.addTriangle (cx - 4.5f, cy - 3.5f, cx + 4.5f, cy - 3.5f, cx, cy + 3.5f); }
                g.setColour (tc); g.fillPath (p);
            };
            if (glyph <= 2) tri (r.getCentreX(), r.getCentreY(), glyph);
            else
            {
                const float tw = textWidth (f, text) + spacing * (float) text.length(), total = tw + 6.0f + 9.0f, x0 = r.getCentreX() - total * 0.5f;
                if (glyph == 3) { tri (x0 + 4.5f, r.getCentreY(), 3); drawText (g, text, { x0 + 15.0f, r.getY(), tw, r.getHeight() }, f, tc, Justification::centredLeft, spacing); }
                else            { drawText (g, text, { x0, r.getY(), tw, r.getHeight() }, f, tc, Justification::centredLeft, spacing); tri (x0 + tw + 10.5f, r.getCentreY(), 4); }
            }
        }
    }
    void mouseEnter (const MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! disabled && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick) onClick (e.mods);
    }
    void mouseDoubleClick (const MouseEvent&) override { if (! disabled && onDouble) onDouble(); }

private:
    const Ctx& ctx;
    float size;
    theme::FontKind kind;
    bool over = false;
};

// A transparent area that only carries an info bubble (the brand, the wood).
class TipArea : public Component, public TipClient {};

// ---- A control the knobs and switches are bound to ----------------------------------------------------
struct Binding
{
    RangedAudioParameter* prm = nullptr;
    const catr::ParamDef* pd = nullptr;     // how the normalised value maps to the real one
};

// ---- Rotary knob drawn like the mockup's SVG (viewBox 100) ------------------------------------------------
// Mini: the small knobs on the unit cards, a short label below that shows the value while the mouse is on it.
// Detail: the knobs of the unit panel, the label above and the value below.
class KnobWidget : public Component, public TipClient
{
public:
    enum Style { Mini, Detail };
    KnobWidget (const Ctx& c, Style s, int knobPx) : ctx (c), style (s), ks (knobPx)
    {
        setMouseCursor (MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (true);
    }
    ~KnobWidget() override = default;

    std::function<void()> onEdited;
    float labelH = 11.0f;
    float labelSize = 13.0f;      // Detail: label font size
    String labelOverride;         // Mini: shown instead of the short label (IN and OUT knobs show their names)

    void bind (Binding b, Colour col)
    {
        att.reset();
        bnd = b; colour = col;
        if (b.prm != nullptr)
        {
            att = std::make_unique<ParameterAttachment> (*b.prm, [this] (float) { repaint(); });
            att->sendInitialUpdate();
        }
        tip = b.pd != nullptr ? String::fromUTF8 (b.pd->tip) : String();
        repaint();
    }

    float norm() const { return bnd.prm != nullptr ? bnd.prm->getValue() : 0.0f; }
    float real() const { return bnd.pd != nullptr ? catr::fromNorm (*bnd.pd, norm()) : 0.0f; }
    void setReal (float v)
    {
        if (bnd.prm == nullptr || bnd.pd == nullptr) return;
        bnd.prm->setValueNotifyingHost (catr::toNorm (*bnd.pd, v));
        if (onEdited) onEdited();
    }
    void setNorm (float n)
    {
        if (bnd.pd == nullptr) return;
        setReal (catr::fromNorm (*bnd.pd, n));
    }

    void paint (Graphics& g) override
    {
        if (bnd.pd == nullptr) return;
        auto& t = ctx.t();
        const auto& pd = *bnd.pd;
        const float v = real();
        float top = 0.0f;
        if (style == Detail)
        {
            drawText (g, String::fromUTF8 (pd.longLabel).toUpperCase(), { 0.0f, 0.0f, (float) getWidth(), 15.0f },
                      theme::font (theme::FontKind::bold, labelSize), t.title, Justification::centred, 1.5f);
            top = 15.0f;
        }
        paintKnob (g, (getWidth() - (float) ks) * 0.5f, top, (float) ks, colour, t.ink, catr::toNorm (pd, v), pd.mn < 0.0f && pd.mx > 0.0f ? catr::toNorm (pd, 0.0f) : 0.0f);
        if (style == Detail)
        {
            const String vt = String::fromUTF8 (catr::fmtVal (pd, v).c_str());
            drawText (g, vt, { 0.0f, top + (float) ks - 2.0f, (float) getWidth(), 14.0f }, theme::font (theme::FontKind::mono, 11.0f), t.ink);
        }
        else
        {
            const bool show = hovering || dragging || hasKeyboardFocus (false);
            const String lt = show ? String::fromUTF8 (catr::fmtShort (pd, v).c_str()) : (labelOverride.isNotEmpty() ? labelOverride : String::fromUTF8 (pd.shortLabel));
            drawText (g, lt, { 0.0f, (float) ks + 1.0f, (float) getWidth(), labelH }, theme::font (theme::FontKind::mono, 9.0f), t.dim, Justification::centred, -0.3f);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (theme::selb);
            g.drawEllipse ((getWidth() - (float) ks) * 0.5f - 1.0f, top - 1.0f, (float) ks + 2.0f, (float) ks + 2.0f, 2.0f);
        }
    }

    static void paintKnob (Graphics& g, float x, float y, float size, Colour arc, Colour pointer, float n, float zeroN)
    {
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::scale (size / 100.0f).translated (x, y));
        const PathStrokeType st7 (7.0f, PathStrokeType::curved, PathStrokeType::rounded);
        Path track; track.addCentredArc (50.0f, 50.0f, 40.0f, 40.0f, 0.0f, degreesToRadians (-135.0f), degreesToRadians (135.0f), true);
        g.setColour (Colours::black.withAlpha (0.55f)); g.strokePath (track, st7);
        const float a = -135.0f + n * 270.0f, az = -135.0f + zeroN * 270.0f;
        if (std::abs (a - az) >= 0.5f)
        {
            Path va; va.addCentredArc (50.0f, 50.0f, 40.0f, 40.0f, 0.0f, degreesToRadians (jmin (a, az)), degreesToRadians (jmax (a, az)), true);
            g.setColour (arc); g.strokePath (va, st7);
        }
        g.setColour (Colour (0xff2b2d33)); g.fillEllipse (19.0f, 19.0f, 62.0f, 62.0f);
        g.setColour (Colours::black); g.drawEllipse (19.0f, 19.0f, 62.0f, 62.0f, 3.0f);
        g.setColour (Colour (0xff3b3e46)); g.fillEllipse (24.0f, 24.0f, 52.0f, 52.0f);
        Path p; p.startNewSubPath (50.0f, 50.0f); p.lineTo (50.0f, 28.0f);
        g.setColour (pointer);
        g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::rotation (degreesToRadians (a), 50.0f, 50.0f));
    }

    void mouseEnter (const MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const MouseEvent&) override
    {
        if (bnd.prm == nullptr) return;
        grabKeyboardFocus();
        startN = norm(); dragging = true; bnd.prm->beginChangeGesture(); repaint();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging) return;
        const float sens = e.mods.isShiftDown() ? 700.0f : 170.0f;
        setNorm (startN + (float) -e.getDistanceFromDragStartY() / sens);
    }
    void mouseUp (const MouseEvent&) override
    {
        if (dragging && bnd.prm != nullptr) bnd.prm->endChangeGesture();
        dragging = false; repaint();
    }
    void mouseDoubleClick (const MouseEvent&) override
    {
        if (bnd.prm == nullptr || bnd.pd == nullptr) return;
        bnd.prm->beginChangeGesture(); setReal (bnd.pd->def); bnd.prm->endChangeGesture();
    }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (bnd.prm == nullptr || bnd.pd == nullptr) return;
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        if (std::abs (dy) < 1.0e-6f) return;
        const float dir = dy > 0 ? 1.0f : -1.0f;
        float v = catr::fromNorm (*bnd.pd, norm() + dir * (e.mods.isShiftDown() ? 0.01f : 0.04f));
        if (v == real()) v = jlimit (bnd.pd->mn, bnd.pd->mx, real() + dir * catr::stepOf (*bnd.pd));
        bnd.prm->beginChangeGesture(); setReal (v); bnd.prm->endChangeGesture();
    }
    bool keyPressed (const KeyPress& k) override
    {
        if (bnd.prm == nullptr || bnd.pd == nullptr) return false;
        const auto& pd = *bnd.pd;
        const float s = catr::stepOf (pd), big = (pd.mx - pd.mn) / 10.0f;
        const bool shift = k.getModifiers().isShiftDown();
        float v = real();
        if (k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::rightKey)) v += shift ? s : jmax (s, big / 5.0f);
        else if (k.isKeyCode (KeyPress::downKey) || k.isKeyCode (KeyPress::leftKey)) v -= shift ? s : jmax (s, big / 5.0f);
        else if (k.isKeyCode (KeyPress::homeKey)) v = pd.mn;
        else if (k.isKeyCode (KeyPress::endKey)) v = pd.mx;
        else if (k.isKeyCode (KeyPress::pageUpKey)) v += big;
        else if (k.isKeyCode (KeyPress::pageDownKey)) v -= big;
        else if (k.isKeyCode (KeyPress::deleteKey) || k.isKeyCode (KeyPress::backspaceKey)) v = pd.def;
        else return false;
        bnd.prm->beginChangeGesture(); setReal (jlimit (pd.mn, pd.mx, std::round (v / s) * s)); bnd.prm->endChangeGesture();
        return true;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    const Ctx& ctx;
    Style style;
    int ks;
    Binding bnd;
    Colour colour { 0xff02EBBA };
    std::unique_ptr<ParameterAttachment> att;
    float startN = 0.0f;
    bool dragging = false, hovering = false;
};

// ---- ON / S / M switches -----------------------------------------------------------------------------------
class Tgl : public Component, public TipClient
{
public:
    enum Kind { On, Solo, Mute };
    Tgl (const Ctx& c, String t, Kind k, float px = 11.0f) : ctx (c), text (std::move (t)), kind (k), size (px) { setMouseCursor (MouseCursor::PointingHandCursor); }
    std::function<void()> onEdited;

    void bind (RangedAudioParameter* p)
    {
        att.reset(); prm = p;
        if (p != nullptr) { att = std::make_unique<ParameterAttachment> (*p, [this] (float) { repaint(); }); att->sendInitialUpdate(); }
        repaint();
    }
    bool isOn() const { return prm != nullptr && prm->getValue() > 0.5f; }
    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        const bool o = isOn();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        Colour fill = t.plate, border = t.line, ink = t.dim;
        if (o)
        {
            fill = kind == On ? theme::green : kind == Solo ? t.yellow : theme::red;
            border = Colours::transparentBlack;
            ink = kind == Mute ? Colours::white : Colour (0xff06080a);
        }
        g.setColour (fill); g.fillRoundedRectangle (r, 3.0f);
        if (! o) { g.setColour (border); g.drawRoundedRectangle (r, 3.0f, 1.0f); }
        drawText (g, text, r, theme::font (theme::FontKind::bold, size), ink, Justification::centred, 0.5f);
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (prm == nullptr || ! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition())) return;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (isOn() ? 0.0f : 1.0f); prm->endChangeGesture();
        if (onEdited) onEdited();
    }
private:
    const Ctx& ctx;
    String text;
    Kind kind;
    float size;
    RangedAudioParameter* prm = nullptr;
    std::unique_ptr<ParameterAttachment> att;
};
} // namespace ui
