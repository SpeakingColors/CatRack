#include "Dialogs.h"
#include "Views.h"

namespace ui
{
namespace
{
// ---- little building blocks -----------------------------------------------------------------------
struct Seg { String text; bool bold = false, mono = false, dim = false, accent = false; };

class TextBlock : public Component
{
public:
    TextBlock (const Ctx& c, std::vector<Seg> s, float px = 15.0f) : ctx (c), segs (std::move (s)), size (px) { setInterceptsMouseClicks (false, false); }
    int heightFor (int w) const
    {
        TextLayout tl; tl.createLayout (build(), (float) w);
        return (int) std::ceil (tl.getHeight()) + 2;
    }
    void paint (Graphics& g) override { build().draw (g, getLocalBounds().toFloat()); }
private:
    AttributedString build() const
    {
        auto& t = ctx.t();
        AttributedString as;
        as.setWordWrap (AttributedString::byWord);
        as.setLineSpacing (3.0f);
        for (auto& s : segs)
            as.append (s.text, theme::font (s.mono ? theme::FontKind::mono : s.bold ? theme::FontKind::bold : theme::FontKind::regular, s.mono ? size - 3.0f : size),
                       s.accent ? theme::selb : s.dim ? t.dim : t.ink);
        return as;
    }
    const Ctx& ctx;
    std::vector<Seg> segs;
    float size;
};

class Kbd : public Component
{
public:
    Kbd (const Ctx& c, String s) : ctx (c), text (std::move (s)) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        auto f = theme::font (theme::FontKind::mono, 12.0f);
        const float w = textWidth (f, text) + 12.0f;
        Rectangle<float> r (0.0f, 2.0f, w, 20.0f);
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        drawText (g, text, r, f, t.ink);
    }
private:
    const Ctx& ctx;
    String text;
};

class PToggle : public Component
{
public:
    PToggle (const Ctx& c, String s, bool v) : ctx (c), text (std::move (s)), value (v) { setMouseCursor (MouseCursor::PointingHandCursor); }
    std::function<void (bool)> onChange;
    void paint (Graphics& g) override
    {
        auto& t = ctx.t();
        Rectangle<float> box (0.0f, (getHeight() - 16) * 0.5f, 16.0f, 16.0f);
        g.setColour (t.disp); g.fillRoundedRectangle (box, 3.0f);
        g.setColour (value ? theme::selb : t.line); g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
        if (value)
        {
            Path p; p.startNewSubPath (box.getX() + 3.5f, box.getCentreY()); p.lineTo (box.getX() + 7.0f, box.getBottom() - 4.5f); p.lineTo (box.getRight() - 3.0f, box.getY() + 4.0f);
            g.setColour (t.accent); g.strokePath (p, PathStrokeType (2.0f));
        }
        drawText (g, text, getLocalBounds().toFloat().withTrimmedLeft (24.0f), theme::font (theme::FontKind::regular, 15.0f), t.ink, Justification::centredLeft);
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        value = ! value; repaint();
        if (onChange) onChange (value);
    }
private:
    const Ctx& ctx;
    String text;
    bool value;
};

void styleEditor (TextEditor& e, const Ctx& c, bool multi)
{
    auto& t = c.t();
    e.setColour (TextEditor::backgroundColourId, t.disp);
    e.setColour (TextEditor::textColourId, t.ink);
    e.setColour (TextEditor::outlineColourId, t.line);
    e.setColour (TextEditor::focusedOutlineColourId, theme::selb);
    e.setColour (TextEditor::highlightColourId, theme::sel);
    e.setColour (TextEditor::highlightedTextColourId, theme::lilac);
    e.setColour (CaretComponent::caretColourId, t.ink);
    e.setFont (theme::font (multi ? theme::FontKind::mono : theme::FontKind::regular, multi ? 12.0f : 15.0f));
    e.setIndents (8, 5);
    e.setBorder ({ 1, 1, 1, 1 });
    e.setMultiLine (multi, multi);
    e.setReturnKeyStartsNewLine (multi);
}
} // namespace

// ---- Panel: title bar, rows, footer buttons -------------------------------------------------------
class Panel : public Component
{
public:
    Panel (Modal& m, DialogHost& h, String t, int w) : modal (m), host (h), title (std::move (t)), width (w), closeBtn (h.context(), "Close")
    {
        closeBtn.onClick = [this] (const ModifierKeys&) { modal.close(); };
        addAndMakeVisible (closeBtn);
        setSize (w, 200);
    }
    struct Row
    {
        std::vector<Component*> items;
        std::vector<int> widths;                 // 0 = share of the remaining width
        int height = 24;
        std::function<int (int)> heightFor;
        int gap = 6;
    };
    template <class T, class... A> T* make (A&&... a)
    {
        auto p = std::make_unique<T> (std::forward<A> (a)...);
        T* raw = p.get();
        addAndMakeVisible (*raw);
        owned.push_back (std::move (p));
        return raw;
    }
    void row (std::vector<Component*> items, std::vector<int> widths, int height, int gap = 6)
    {
        Row r; r.items = std::move (items); r.widths = std::move (widths); r.height = height; r.gap = gap;
        rows.push_back (std::move (r));
    }
    void textRow (std::vector<Seg> segs, int gap = 8)
    {
        auto* tb = make<TextBlock> (host.context(), std::move (segs));
        Row r; r.items = { tb }; r.widths = { 0 }; r.gap = gap;
        r.heightFor = [tb] (int w) { return tb->heightFor (w); };
        rows.push_back (std::move (r));
    }
    PButton* footer (const String& text, std::function<void()> fn, bool primary = false)
    {
        auto* b = make<PButton> (host.context(), text);
        b->on = primary;
        b->onClick = [fn] (const ModifierKeys&) { fn(); };
        footers.push_back (b);
        return b;
    }
    void fit (int maxH = 524)
    {
        const int h = std::min (maxH, layoutRows (false) + 12 + (footers.empty() ? 0 : kFooter));
        setSize (width, h);
        resized();
    }
    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (r.translated (0, 6).expanded (6), 12.0f);
        g.setColour (t.plate); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (t.line);
        g.fillRect (1, kTitle, getWidth() - 2, 1);
        if (! footers.empty()) g.fillRect (1, getHeight() - kFooter, getWidth() - 2, 1);
        drawText (g, title, { 16.0f, 0.0f, (float) getWidth() - 100.0f, (float) kTitle }, theme::font (theme::FontKind::regular, 20.0f), t.title, Justification::centredLeft, 1.0f);
    }
    void resized() override
    {
        closeBtn.setBounds (getWidth() - 12 - 60, (kTitle - 26) / 2, 60, 26);
        layoutRows (true);
        int x = getWidth() - 16;
        for (int i = (int) footers.size(); --i >= 0;)
        {
            const int w = std::max(70, (int) textWidth (theme::font (theme::FontKind::regular, 14.0f), footers[(size_t) i]->text) + 36);
            footers[(size_t) i]->setBounds (x - w, getHeight() - kFooter + 11, w, 28);
            x -= w + 8;
        }
    }
    Modal& modal;
    DialogHost& host;
    String title;
    int width;
    PButton closeBtn;
    static constexpr int kTitle = 44, kFooter = 50;
private:
    int layoutRows (bool place)
    {
        const int bw = width - 32;
        int y = kTitle + 12;
        for (auto& r : rows)
        {
            const int h = r.heightFor ? r.heightFor (bw) : r.height;
            if (place)
            {
                int fixed = 0, flex = 0;
                for (int w : r.widths) { if (w > 0) fixed += w; else ++flex; }
                const int gaps = (int) r.items.size() - 1;
                const int share = flex > 0 ? std::max (20, (bw - fixed - gaps * 8) / flex) : 0;
                int x = 16;
                for (size_t i = 0; i < r.items.size(); ++i)
                {
                    const int w = r.widths[i] > 0 ? r.widths[i] : share;
                    r.items[i]->setBounds (x, y, w, h);
                    x += w + 8;
                }
            }
            y += h + r.gap;
        }
        return y;
    }
    std::vector<std::unique_ptr<Component>> owned;
    std::vector<Row> rows;
    std::vector<PButton*> footers;
};

// ---- Modal ------------------------------------------------------------------------------------------
Modal::Modal (DialogHost& h) : host (h) { setVisible (false); setWantsKeyboardFocus (true); }
Modal::~Modal() = default;

void Modal::paint (Graphics& g) { g.fillAll (Colours::black.withAlpha (0.6f)); }
void Modal::resized() { if (panel) panel->setCentrePosition (getWidth() / 2, getHeight() / 2); }
void Modal::mouseDown (const MouseEvent&) { close(); }
bool Modal::keyPressed (const KeyPress& k)
{
    if (k.isKeyCode (KeyPress::escapeKey)) { close(); return true; }
    return true; // swallow keys while a dialog is open
}
void Modal::close()
{
    setVisible (false);
    if (panel) panel->setVisible (false);
    dying = std::move (panel);   // deleted on the next close, never while one of its own buttons is still running
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();
}
void Modal::show (std::unique_ptr<Panel> p)
{
    panel = std::move (p);
    addAndMakeVisible (*panel);
    setBounds (getParentComponent()->getLocalBounds());
    setVisible (true);
    toFront (true);
    resized();
    grabKeyboardFocus();
}


// ---- Settings, Guide, About -------------------------------------------------------------------------
void Modal::openSettings()
{
    auto p = std::make_unique<Panel> (*this, host, "Settings", 760);
    auto& c = host.context();
    auto& proc = host.processor();
    auto* tips = p->make<PToggle> (c, "Info bubbles after hovering for about one second", c.tips);
    tips->onChange = [this, &proc] (bool v) { host.context().tips = v; proc.setSetting (setting::tips, v); host.settingsChanged(); };
    p->row ({ tips }, { 0 }, 24);
    auto* pw = p->make<PToggle> (c, "Power saving (redraw at 16 frames per second)", c.power);
    pw->onChange = [this, &proc] (bool v) { host.context().power = v; proc.setSetting (setting::power, v); host.settingsChanged(); };
    p->row ({ pw }, { 0 }, 24, 10);

    auto* lookLabel = p->make<TextBlock> (c, std::vector<Seg> { { "Colour look" } });
    std::vector<PButton*> looks;
    const char* names[3] = { "Look C, charcoal and white (default)", "Look A, neon navy", "Look B, purple soda pop" };
    const char ids[3] = { 'C', 'A', 'B' };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = p->make<PButton> (c, names[i]);
        b->on = c.look == ids[i];
        looks.push_back (b);
    }
    for (int i = 0; i < 3; ++i)
        looks[(size_t) i]->onClick = [this, looks, ids, i, &proc] (const ModifierKeys&)
        {
            host.context().look = ids[i];
            proc.setSetting (setting::look, String::charToString ((juce_wchar) ids[i]));
            for (int k = 0; k < 3; ++k) looks[(size_t) k]->setOn (k == i);
            host.settingsChanged();
        };
    p->row ({ lookLabel, looks[0], looks[1], looks[2] }, { 90, 0, 0, 0 }, 26);
    p->textRow ({ { "The look is saved with the plugin settings, not with each preset. Wood, cat logo, meter colours and dB scales stay the same in all three.", false, false, true } }, 12);

    auto* reset = p->make<PButton> (c, "Reset window to 100%");
    reset->onClick = [this] (const ModifierKeys&) { host.resetWindow(); host.toast ("Window reset to 100%"); };
    p->row ({ reset }, { 170 }, 28);
    p->fit();
    show (std::move (p));
}

void Modal::openGuide()
{
    auto p = std::make_unique<Panel> (*this, host, "Guide", 760);
    auto& c = host.context();
    p->textRow ({ { "The idea.", true }, { " The rack holds up to 12 units. The signal goes through them from top to bottom, as the strip at the top shows from left to right. Click a unit and its controls appear in the panel on the right, headed Selected unit. The Add unit button opens a browser of the Studio 87 effects, sorted into six groups." } }, 6);
    p->textRow ({ { "Change the order.", true }, { " Drag a unit by its dotted handle. Drop it on the top or bottom edge of a row to place it before or after. Drop it on the middle of another single unit to run the two side by side." } }, 6);
    p->textRow ({ { "Side by side (parallel).", true }, { " Two units in one row are added together. Each lane has a level, and the pair has a Dry/Wet. A row holds two units at most. With many units the rows shrink so nothing needs scrolling." } }, 6);
    p->textRow ({ { "ON, S, M.", true }, { " ON switches a unit on or off. S (solo) lets only the soloed units work so you can hear them alone. M (mute) silences a unit; in a pair only that lane goes quiet." } }, 10);
    const std::pair<const char*, const char*> keys[] = {
        { "B", "Bypass on or off" },
        { "[  /  ]", "Previous or next preset" },
        { "Up / Down", "On a focused unit: select the one above or below" },
        { "Alt+Up / Alt+Down", "Move the focused unit's row earlier or later" },
        { "Delete", "Remove the focused unit" },
        { "Knobs", "Drag up or down; Shift for fine; double-click to reset; arrow keys and mouse wheel also work" },
        { "Wood cheeks", "Double-click to reset the window to 100%" } };
    for (auto& k : keys)
    {
        auto* kb = p->make<Kbd> (c, k.first);
        auto* tb = p->make<TextBlock> (c, std::vector<Seg> { { k.second } });
        p->row ({ kb, tb }, { 170, 0 }, 24, 2);
    }
    p->textRow ({ { "Keys work while the plugin window has keyboard focus.", false, false, true } }, 10);
    p->fit();
    show (std::move (p));
}

void Modal::openAbout()
{
    auto p = std::make_unique<Panel> (*this, host, "About & Support", 760);
    auto& c = host.context();
    p->textRow ({ { "CatRack", true }, { String (" by Studio 87, version ") + JucePlugin_VersionString + ". A free and open source rack plugin inspired by classic hardware and written from public manuals. Licence AGPL-3.0-or-later. Collects nothing and never uses the network." } });
    p->textRow ({ { "Your presets are saved here:", true }, { "\nLinux  " }, { "~/.local/share/Studio 87/CatRack/presets", false, true },
                  { "\nWindows  " }, { "%APPDATA%\\Studio 87\\CatRack\\presets", false, true },
                  { "\nmacOS  " }, { "~/Library/Application Support/Studio 87/CatRack/presets", false, true } });
    p->textRow ({ { "Install and uninstall:", true }, { " the installer puts the VST3 in the standard folder for your system. The uninstaller removes everything it installed and asks before touching your presets (it keeps them by default)." } });
    auto* gh = p->make<PButton> (c, "GitHub");
    gh->onClick = [] (const ModifierKeys&) { URL ("https://github.com/SpeakingColors/CatRack").launchInDefaultBrowser(); };
    auto* rel = p->make<PButton> (c, "Releases");
    rel->onClick = [] (const ModifierKeys&) { URL ("https://github.com/SpeakingColors/CatRack/releases").launchInDefaultBrowser(); };
    auto* kofi = p->make<PButton> (c, "Ko-fi studio87");
    kofi->onClick = [] (const ModifierKeys&) { URL ("https://ko-fi.com/studio87").launchInDefaultBrowser(); };
    p->row ({ gh, rel, kofi }, { 90, 90, 130 }, 28);
    p->textRow ({ { "Support is entirely optional. Nothing is locked.", false, false, true } });
    p->fit();
    show (std::move (p));
}

// ---- Clear -------------------------------------------------------------------------------------------
void Modal::openClear()
{
    auto p = std::make_unique<Panel> (*this, host, "Clear the rack?", 420);
    p->textRow ({ { "This removes all " + String (host.unitCount()) + " units from the rack. You can load a preset again afterwards." } });
    p->footer ("Cancel", [this] { close(); });
    p->footer ("Clear", [this] { close(); host.clearRack(); }, true);
    p->fit();
    show (std::move (p));
}

// ---- Add unit ----------------------------------------------------------------------------------------
namespace
{
String abCat = "Dynamics", abQuery;
const char* const kCats[6] = { "Dynamics", "EQ", "Saturation", "Space", "Creative", "Utility" };

class AddBody : public Component
{
public:
    static constexpr int kItemH = 64, kGap = 6, kGridH = 330;
    AddBody (Modal& m, DialogHost& h) : modal (m), host (h)
    {
        auto& c = h.context();
        styleEditor (search, c, false);
        search.setTextToShowWhenEmpty ("Search units", c.t().dim);
        search.setText (abQuery, false);
        search.onTextChange = [this] { abQuery = search.getText(); scroll = 0; refresh(); };
        search.onEscapeKey = [this] { modal.close(); };
        addAndMakeVisible (search);
        StringArray all ("All");
        for (auto* k : kCats) all.add (k);
        for (auto& n : all)
        {
            auto* b = chips.add (new PButton (c, n, 13.0f));
            b->radius = 12.0f; b->spacing = 0.0f;
            const String nn = n;
            b->onClick = [this, nn] (const ModifierKeys&) { abCat = nn; abQuery = {}; search.setText ({}, false); scroll = 0; refresh(); };
            addAndMakeVisible (b);
        }
        refresh();
    }
    void refresh()
    {
        for (auto* b : chips) b->setOn (b->text == abCat);
        shown.clear();
        const String k = abQuery.toLowerCase();
        for (int i = 0; i < catr::kNumUnits; ++i)
        {
            const auto& u = catr::unitTable()[i];
            const String hay = (String::fromUTF8 (u.name) + " " + u.kind + " " + u.blurb + " " + u.cat).toLowerCase();
            if (k.isNotEmpty() ? hay.contains (k) : (abCat == "All" || abCat == u.cat)) shown.push_back (i);
        }
        resized(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        search.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        int x = 0;
        for (auto* b : chips)
        {
            const int w = (int) textWidth (theme::font (theme::FontKind::regular, 13.0f), b->text) + 20;
            b->setBounds (x, r.getY(), w, 22);
            x += w + 4;
        }
        r.removeFromTop (22 + 6);
        headR = r.removeFromTop (14);
        r.removeFromTop (4);
        gridR = r.withHeight (kGridH);
    }
    int contentH() const { const int rows = ((int) shown.size() + 1) / 2; return rows * kItemH + std::max (0, rows - 1) * kGap; }
    Rectangle<int> itemRect (int i) const
    {
        const int cw = (gridR.getWidth() - kGap) / 2;
        return { gridR.getX() + (i % 2) * (cw + kGap), gridR.getY() + (i / 2) * (kItemH + kGap) - (int) scroll, cw, kItemH };
    }
    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        const String head = abQuery.isNotEmpty() ? String ("Search results") : abCat == "All" ? String ("All units") : abCat == "Saturation" ? String ("Saturation and distortion")
                          : abCat == "Space" ? String ("Space (reverb, delay)") : abCat;
        drawText (g, (head + " " + u8 ("\xc2\xb7") + " " + String ((int) shown.size())).toUpperCase(), headR.toFloat(), theme::font (theme::FontKind::regular, 11.0f), t.title, Justification::centredLeft, 1.5f);
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (gridR);
        if (shown.empty()) drawText (g, "Nothing matches. Try another word.", gridR.withHeight (30).toFloat(), theme::font (theme::FontKind::regular, 15.0f), t.dim, Justification::centredLeft);
        const bool full = host.unitCount() >= catr::kMaxUnits;
        for (size_t n = 0; n < shown.size(); ++n)
        {
            const auto& u = catr::unitTable()[shown[n]];
            auto r = itemRect ((int) n);
            if (r.getBottom() < gridR.getY() || r.getY() > gridR.getBottom()) continue;
            const bool ok = shown[n] < catr::kBuiltUnits && ! full;
            const float a = ok ? 1.0f : 0.4f;
            const bool hot = (int) n == hover && ok;
            auto rf = r.toFloat();
            g.setColour ((hot ? theme::sel : t.plate2).withMultipliedAlpha (a)); g.fillRoundedRectangle (rf, 5.0f);
            g.setColour ((hot ? theme::selb : t.line).withMultipliedAlpha (a)); g.drawRoundedRectangle (rf.reduced (0.5f), 5.0f, 1.0f);
            { Graphics::ScopedSaveState s2 (g); g.reduceClipRegion (r.withWidth (3)); g.setColour (Colour (u.color).withMultipliedAlpha (a)); g.fillRoundedRectangle (rf, 5.0f); }
            g.setColour (Colour (u.color).withMultipliedAlpha (a)); g.fillEllipse (rf.getX() + 13.0f, rf.getY() + 15.0f, 8.0f, 8.0f);
            const String src = shown[n] < catr::kBuiltUnits ? String::fromUTF8 (u.src) : String ("Planned");
            drawText (g, String::fromUTF8 (u.name), { rf.getX() + 29.0f, rf.getY() + 6.0f, rf.getWidth() - 140.0f, 20.0f }, theme::font (theme::FontKind::brand, 16.0f), t.ink.withMultipliedAlpha (a), Justification::centredLeft);
            drawText (g, src.toUpperCase(), { rf.getRight() - 130.0f, rf.getY() + 6.0f, 120.0f, 20.0f }, theme::font (theme::FontKind::regular, 11.0f), t.dim.withMultipliedAlpha (a), Justification::centredRight, 1.0f);
            String d = String::fromUTF8 (u.blurb);
            const int dot = d.indexOf (". ");
            if (dot > 0) d = d.substring (0, dot);
            if (d.endsWithChar ('.')) d = d.dropLastCharacters (1);
            AttributedString as; as.setWordWrap (AttributedString::byWord); as.setLineSpacing (0.0f);
            as.append (d, theme::font (theme::FontKind::regular, 13.0f), t.dim.withMultipliedAlpha (a));
            as.draw (g, Rectangle<float> (rf.getX() + 29.0f, rf.getY() + 27.0f, rf.getWidth() - 39.0f, 34.0f));
        }
    }
    int indexAt (Point<int> p) const
    {
        if (! gridR.contains (p)) return -1;
        for (size_t n = 0; n < shown.size(); ++n) if (itemRect ((int) n).contains (p)) return (int) n;
        return -1;
    }
    void mouseMove (const MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const int i = indexAt (e.getPosition());
        if (i < 0) return;
        const int t = shown[(size_t) i];
        if (t >= catr::kBuiltUnits) { host.toast (String::fromUTF8 (catr::unitTable()[t].name) + " is planned for a later version"); return; }
        if (host.unitCount() >= catr::kMaxUnits) return;
        modal.close();
        host.addUnit (t);
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        const int maxS = std::max (0, contentH() - kGridH);
        scroll = jlimit (0.0f, (float) maxS, scroll - w.deltaY * 140.0f);
        repaint();
    }
    Modal& modal;
    DialogHost& host;
    TextEditor search;
    OwnedArray<PButton> chips;
    std::vector<int> shown;
    Rectangle<int> headR, gridR;
    float scroll = 0.0f;
    int hover = -1;
};
} // namespace

void Modal::openAdd()
{
    auto p = std::make_unique<Panel> (*this, host, "Add a unit (" + String (host.unitCount()) + " of " + String (catr::kMaxUnits) + " used)", 760);
    auto* body = p->make<AddBody> (*this, host);
    p->row ({ body }, { 0 }, 30 + 6 + 22 + 6 + 14 + 4 + AddBody::kGridH);
    p->fit();
    show (std::move (p));
}

// ---- Preset browser ---------------------------------------------------------------------------------
namespace
{
String pbSel, pbType = "All", pbQuery;

class BrowserBody : public Component
{
public:
    static constexpr int kRow = 30;
    BrowserBody (Modal& m, DialogHost& h, bool renameStart) : modal (m), host (h), renaming (renameStart)
    {
        auto& c = h.context();
        styleEditor (search, c, false);
        search.setTextToShowWhenEmpty ("Search presets", c.t().dim);
        search.setText (pbQuery, false);
        search.onTextChange = [this] { pbQuery = search.getText(); scroll = 0; refresh(); };
        search.onEscapeKey = [this] { modal.close(); };
        addAndMakeVisible (search);
        StringArray all ("All");
        for (auto* t : presets::types) all.add (t);
        all.add ("Mine");
        for (auto& n : all)
        {
            auto* b = chips.add (new PButton (c, n, 13.0f));
            b->radius = 12.0f; b->spacing = 0.0f;
            const String nn = n;
            b->onClick = [this, nn] (const ModifierKeys&) { pbType = nn; scroll = 0; refresh(); };
            addAndMakeVisible (b);
        }
        for (auto* n : { "Load", "Rename", "Delete", "Export", "Import" })
            addAndMakeVisible (sideBtns.add (new PButton (c, n)));
        sideBtns[0]->on = true;
        styleEditor (renameBox, c, false);
        renameBox.setInputRestrictions (40);
        renameBox.onEscapeKey = [this] { renaming = false; refresh(); };
        addChildComponent (renameBox);
        okBtn = new PButton (c, "OK"); okBtn->on = true; owned.add (okBtn); addChildComponent (okBtn);
        warn.setColour (Label::textColourId, theme::red);
        warn.setFont (theme::font (theme::FontKind::regular, 14.0f));
        addAndMakeVisible (warn);
        sideBtns[0]->onClick = [this] (const ModifierKeys&) { if (auto* p = selected()) { Preset q = *p; modal.close(); host.loadPreset (q); } };
        sideBtns[1]->onClick = [this] (const ModifierKeys&) { if (auto* p = selected(); p != nullptr && ! p->factory) { renaming = true; renameBox.setText (p->name, false); refresh(); renameBox.grabKeyboardFocus(); } };
        sideBtns[2]->onClick = [this] (const ModifierKeys&)
        {
            auto* p = selected();
            if (p == nullptr || p->factory) return;
            if (! confirmDelete) { confirmDelete = true; sideBtns[2]->setText ("Really delete?"); resized(); return; }
            const String n = p->name;
            host.store().remove (n);
            if (host.presetName() == n) host.setPresetName ("Empty rack");
            pbSel = {}; confirmDelete = false;
            host.toast ("Deleted");
            refresh();
        };
        sideBtns[3]->onClick = [this] (const ModifierKeys&) { if (auto* p = selected()) { Preset q = *p; modal.openExport (&q); } };
        sideBtns[4]->onClick = [this] (const ModifierKeys&) { modal.openImport(); };
        okBtn->onClick = [this] (const ModifierKeys&)
        {
            auto* p = selected();
            if (p == nullptr || p->factory) return;
            const String n = renameBox.getText().trim();
            const String old = p->name;
            if (n.isEmpty() || (host.store().find (n) != nullptr && n.compareIgnoreCase (old) != 0)) { warn.setText ("Pick a name that is not used yet", dontSendNotification); return; }
            if (host.store().rename (old, n))
            {
                if (host.presetName() == old) host.setPresetName (n);
                pbSel = n; renaming = false; warn.setText ({}, dontSendNotification); refresh();
            }
        };
        refresh();
    }

    const Preset* selected() const { return host.store().find (pbSel); }
    int listH() const { return listArea.getHeight(); }
    int contentH() const { return (int) shown.size() * kRow; }

    void refresh()
    {
        for (auto* b : chips) b->setOn (b->text == pbType);
        shown.clear();
        const String q = pbQuery.toLowerCase();
        for (auto& p : host.store().all())
        {
            if (pbType == "Mine" ? p.factory : (pbType != "All" && p.type != pbType)) continue;
            if (q.isNotEmpty() && ! (p.name + " " + p.type).toLowerCase().contains (q)) continue;
            shown.push_back (p);
        }
        scroll = jlimit (0.0f, (float) std::max (0, contentH() - listH()), scroll);
        const Preset* s = selected();
        confirmDelete = false;
        sideBtns[2]->setText ("Delete");
        sideBtns[0]->setVisible (s != nullptr);
        sideBtns[1]->setVisible (s != nullptr && ! s->factory);
        sideBtns[2]->setVisible (s != nullptr && ! s->factory);
        sideBtns[3]->setVisible (s != nullptr);
        renameBox.setVisible (renaming && s != nullptr && ! s->factory);
        okBtn->setVisible (renameBox.isVisible());
        resized();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (300);
        search.setBounds (left.removeFromTop (30));
        left.removeFromTop (6);
        int x = 0, y = left.getY();
        for (auto* b : chips)
        {
            const int w = (int) textWidth (theme::font (theme::FontKind::regular, 13.0f), b->text) + 20;
            if (x + w > left.getWidth()) { x = 0; y += 26; }
            b->setBounds (left.getX() + x, y, w, 22);
            x += w + 4;
        }
        left.setTop (y + 28);
        listArea = left;
        r.removeFromLeft (14);
        sideArea = r;
        // buttons at the bottom of the right column
        int bx = r.getX();
        const int by = r.getBottom() - 28;
        for (auto* b : sideBtns)
        {
            if (! b->isVisible()) continue;
            const int w = (int) textWidth (theme::font (theme::FontKind::regular, 14.0f), b->text) + 28;
            b->setBounds (bx, by, w, 28);
            bx += w + 6;
        }
        renameBox.setBounds (r.getX() + 76, by - 40, r.getWidth() - 76 - 56, 28);
        okBtn->setBounds (r.getRight() - 50, by - 40, 50, 28);
        warn.setBounds (r.getX(), by - 66, r.getWidth(), 22);
    }

    void paint (Graphics& g) override
    {
        auto& t = host.context().t();
        g.setColour (t.disp); g.fillRoundedRectangle (listArea.toFloat(), 4.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (listArea);
            if (shown.empty())
                drawText (g, pbType == "Mine" ? "No presets of your own yet. Press Save to add one." : "Nothing matches.", listArea.withHeight (kRow).withTrimmedLeft (10).toFloat(),
                          theme::font (theme::FontKind::regular, 15.0f), t.dim, Justification::centredLeft);
            for (size_t i = 0; i < shown.size(); ++i)
            {
                auto& p = shown[i];
                const bool sel = p.name == pbSel;
                auto row = Rectangle<int> (listArea.getX(), listArea.getY() + (int) i * kRow - (int) scroll, listArea.getWidth(), kRow);
                if (row.getBottom() < listArea.getY() || row.getY() > listArea.getBottom()) continue;
                if (sel) { g.setColour (theme::sel); g.fillRect (row); }
                g.setColour (t.line); g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
                drawText (g, p.name, row.toFloat().withTrimmedLeft (10.0f).withTrimmedRight (110.0f), theme::font (theme::FontKind::regular, 15.0f), sel ? theme::lilac : t.ink, Justification::centredLeft);
                drawText (g, ((p.factory ? String() : String ("mine ") + u8 ("\xc2\xb7") + " ") + p.type).toUpperCase(), row.toFloat().withTrimmedLeft ((float) row.getWidth() - 150.0f).withTrimmedRight (10.0f),
                          theme::font (theme::FontKind::regular, 11.0f), t.dim, Justification::centredRight, 1.0f);
            }
        }
        g.setColour (t.line); g.drawRoundedRectangle (listArea.toFloat().reduced (0.5f), 4.0f, 1.0f);
        const Preset* p = selected();
        if (p == nullptr) { drawText (g, "Pick a preset on the left.", sideArea.withHeight (24).toFloat(), theme::font (theme::FontKind::regular, 15.0f), t.dim, Justification::centredLeft); return; }
        auto r = sideArea.toFloat();
        drawText (g, p->name, r.withHeight (26.0f), theme::font (theme::FontKind::brand, 22.0f), t.ink, Justification::centredLeft);
        drawText (g, (p->factory ? String ("Factory") : String ("Mine")) + " " + u8 ("\xc2\xb7") + " " + p->type, r.withTrimmedTop (32.0f).withHeight (20.0f), theme::font (theme::FontKind::regular, 15.0f), t.ink, Justification::centredLeft);
        AttributedString as; as.setWordWrap (AttributedString::byWord); as.setLineSpacing (2.0f);
        as.append (p->desc.isEmpty() ? String ("Your own chain.") : p->desc, theme::font (theme::FontKind::regular, 15.0f), t.ink);
        as.draw (g, r.withTrimmedTop (58.0f).withHeight (62.0f));
        drawText (g, "CHAIN", r.withTrimmedTop (124.0f).withHeight (13.0f), theme::font (theme::FontKind::regular, 11.0f), t.title, Justification::centredLeft, 1.5f);
        auto cb = Rectangle<float> (r.getX(), r.getY() + 140.0f, r.getWidth(), 92.0f);
        g.setColour (t.disp); g.fillRoundedRectangle (cb, 4.0f);
        g.setColour (t.line); g.drawRoundedRectangle (cb.reduced (0.5f), 4.0f, 1.0f);
        AttributedString cs; cs.setWordWrap (AttributedString::byWord); cs.setLineSpacing (4.0f);
        cs.append (presets::chainText (*p).replace ("||", u8 ("\xe2\x80\x96")), theme::font (theme::FontKind::mono, 12.0f), t.dim);
        cs.draw (g, cb.reduced (8.0f, 6.0f).withTrimmedBottom (14.0f));
        drawText (g, u8 ("\xe2\x80\x96") + " means side by side (parallel)", cb.reduced (8.0f, 4.0f).withTop (cb.getBottom() - 18.0f), theme::font (theme::FontKind::mono, 11.0f), t.dim, Justification::centredLeft);
        if (renaming && ! p->factory) drawText (g, "New name", { r.getX(), r.getBottom() - 28.0f - 40.0f, 70.0f, 28.0f }, theme::font (theme::FontKind::regular, 15.0f), t.ink, Justification::centredLeft);
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (i < 0) return;
        pbSel = shown[(size_t) i].name; renaming = false; warn.setText ({}, dontSendNotification);
        refresh();
    }
    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (i >= 0) { Preset q = shown[(size_t) i]; modal.close(); host.loadPreset (q); }
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        scroll = jlimit (0.0f, (float) std::max (0, contentH() - listH()), scroll - w.deltaY * 120.0f);
        repaint();
    }
    int rowAt (Point<int> p) const
    {
        if (! listArea.contains (p)) return -1;
        const int i = (p.y - listArea.getY() + (int) scroll) / kRow;
        return i >= 0 && i < (int) shown.size() ? i : -1;
    }

    Modal& modal;
    DialogHost& host;
    TextEditor search, renameBox;
    OwnedArray<PButton> chips, sideBtns, owned;
    PButton* okBtn = nullptr;
    Label warn;
    std::vector<Preset> shown;
    Rectangle<int> sideArea, listArea;
    float scroll = 0.0f;
    bool renaming = false, confirmDelete = false;
};
} // namespace

void Modal::openBrowser (bool renameMode)
{
    auto p = std::make_unique<Panel> (*this, host, "Preset browser", 760);
    if (pbSel.isEmpty() || host.store().find (pbSel) == nullptr) pbSel = host.presetName();
    auto* body = p->make<BrowserBody> (*this, host, renameMode);
    p->row ({ body }, { 0 }, 380);
    p->footer ("Import", [this] { openImport(); });
    p->footer ("Export all", [this] { openExport (nullptr); });
    p->footer ("Save current", [this] { openSave (nullptr); });
    p->fit();
    show (std::move (p));
}

// ---- Save, Export, Import ---------------------------------------------------------------------------
void Modal::openSave (const Preset* edit)
{
    auto p = std::make_unique<Panel> (*this, host, "Save preset", 520);
    auto& c = host.context();
    juce::ignoreUnused (edit);
    const Preset* curP = host.store().find (host.presetName());

    auto* nameLabel = p->make<TextBlock> (c, std::vector<Seg> { { "Name" } });
    auto* nameBox = p->make<TextEditor>();
    styleEditor (*nameBox, c, false);
    nameBox->setInputRestrictions (40);
    nameBox->setText (curP != nullptr ? (curP->factory ? "My " + curP->name : curP->name) : String ("My chain"), false);
    nameBox->onEscapeKey = [this] { close(); };
    p->row ({ nameLabel, nameBox }, { 50, 0 }, 28);

    auto* typeLabel = p->make<TextBlock> (c, std::vector<Seg> { { "Type" } });
    auto typeSel = std::make_shared<String> (curP != nullptr ? curP->type : String ("Other"));
    auto chipList = std::make_shared<std::vector<PButton*>>();
    for (auto* ty : presets::types)
    {
        auto* b = p->make<PButton> (c, ty, 13.0f);
        b->radius = 12.0f; b->spacing = 0.0f;
        b->on = *typeSel == ty;
        chipList->push_back (b);
    }
    for (auto* b : *chipList)
        b->onClick = [typeSel, chipList, b] (const ModifierKeys&) { *typeSel = b->text; for (auto* x : *chipList) x->setOn (x == b); };
    {
        std::vector<Component*> items { typeLabel }; std::vector<int> widths { 50 };
        for (size_t i = 0; i < 4; ++i) { items.push_back ((*chipList)[i]); widths.push_back (0); }
        p->row (items, widths, 26);
        std::vector<Component*> items2; std::vector<int> widths2;
        auto* pad = p->make<TextBlock> (c, std::vector<Seg> { { "" } });
        items2.push_back (pad); widths2.push_back (50);
        for (size_t i = 4; i < chipList->size(); ++i) { items2.push_back ((*chipList)[i]); widths2.push_back (0); }
        p->row (items2, widths2, 26);
    }
    p->textRow ({ { "Saves the whole chain: units, order, routing and every knob. Factory presets are never changed; saving makes your own copy.", false, false, true } }, 8);

    auto* warnText = p->make<Label>();
    warnText->setColour (Label::textColourId, theme::red);
    warnText->setFont (theme::font (theme::FontKind::regular, 15.0f));
    auto* overwrite = p->make<PButton> (c, "Overwrite");
    auto* asNew = p->make<PButton> (c, "Save as new");
    overwrite->setVisible (false); asNew->setVisible (false);
    p->row ({ warnText, overwrite, asNew }, { 0, 90, 100 }, 28);

    p->footer ("Cancel", [this] { close(); });
    auto saveFn = [this, nameBox, typeSel, warnText, overwrite, asNew]
    {
        const String name = nameBox->getText().trim();
        overwrite->setVisible (false); asNew->setVisible (false);
        auto commit = [this, typeSel, warnText] (const String& n)
        {
            Preset q = host.currentPreset();
            q.name = n; q.type = *typeSel; q.factory = false;
            if (q.desc.isEmpty()) q.desc = "Your own chain.";
            if (host.store().save (q))
            {
                host.setPresetName (n);
                host.markSaved();
                close();
                host.toast ("Saved " + n);
            }
            else warnText->setText ("Could not write the preset file. Check that your presets folder is writable.", dontSendNotification);
        };
        if (name.isEmpty()) { warnText->setText ("Type a name first.", dontSendNotification); return; }
        for (auto& f : presets::factory())
            if (f.name.equalsIgnoreCase (name)) { warnText->setText ("\"" + f.name + "\" is a factory name. Pick another name; factory presets stay as they are.", dontSendNotification); return; }
        bool clash = false;
        for (auto& u : host.store().user()) if (u.name.equalsIgnoreCase (name)) clash = true;
        if (clash)
        {
            warnText->setText ("A preset called \"" + name + "\" already exists. What now?", dontSendNotification);
            overwrite->setVisible (true); asNew->setVisible (true);
            overwrite->onClick = [commit, name] (const ModifierKeys&) { commit (name); };
            asNew->onClick = [this, commit, name] (const ModifierKeys&)
            {
                String n = name; int i = 2;
                while (host.store().find (n) != nullptr) n = name + " " + String (i++);
                commit (n);
            };
            return;
        }
        commit (name);
    };
    p->footer ("Save", saveFn, true);
    p->fit();
    show (std::move (p));
    nameBox->grabKeyboardFocus();
    nameBox->selectAll();
}

void Modal::openExport (const Preset* one)
{
    auto p = std::make_unique<Panel> (*this, host, "Export", 620);
    auto& c = host.context();
    p->textRow ({ { "Copy this text and keep it as your backup. Import it again later or on another computer." } });
    auto* box = p->make<TextEditor>();
    styleEditor (*box, c, true);
    box->setReadOnly (true);
    std::vector<Preset> list;
    if (one != nullptr) list.push_back (*one); else list = host.store().user();
    box->setText (host.store().exportJson (list), false);
    p->row ({ box }, { 0 }, 230);
    p->footer ("Copy", [this, box] { SystemClipboard::copyTextToClipboard (box->getText()); host.toast ("Copied"); });
    p->footer ("Back", [this] { openBrowser(); });
    p->fit();
    show (std::move (p));
}

void Modal::openImport()
{
    auto p = std::make_unique<Panel> (*this, host, "Import", 620);
    auto& c = host.context();
    p->textRow ({ { "Paste exported preset text here. Unknown settings are ignored and anything invalid is refused." } });
    auto* box = p->make<TextEditor>();
    styleEditor (*box, c, true);
    p->row ({ box }, { 0 }, 200);
    auto* warn = p->make<Label>();
    warn->setColour (Label::textColourId, theme::red);
    warn->setFont (theme::font (theme::FontKind::regular, 15.0f));
    p->row ({ warn }, { 0 }, 24);
    p->footer ("Back", [this] { openBrowser(); });
    p->footer ("Import", [this, box, warn]
    {
        const int n = host.store().importJson (box->getText());
        if (n < 0) { warn->setText ("That text is not a valid CatRack preset export, or it uses a unit this version does not host yet.", dontSendNotification); return; }
        host.toast ("Imported " + String (n) + (n == 1 ? " preset" : " presets"));
        openBrowser();
    }, true);
    p->fit();
    show (std::move (p));
}
} // namespace ui
