#pragma once
#include "Widgets.h"
#include "Presets.h"
#include "../plugin/PluginProcessor.h"

namespace ui
{
// What the dialogs need from the editor.
struct DialogHost
{
    virtual ~DialogHost() = default;
    virtual CatRackProcessor& processor() = 0;
    virtual presets::Store& store() = 0;
    virtual Ctx& context() = 0;
    virtual void loadPreset (const Preset&) = 0;
    virtual Preset currentPreset() = 0;      // the whole chain as a preset (name left empty)
    virtual String presetName() = 0;
    virtual void setPresetName (const String&) = 0;
    virtual bool dirty() = 0;
    virtual void markSaved() = 0;
    virtual void toast (const String&) = 0;
    virtual void settingsChanged() = 0;      // look, info bubbles or power saving changed
    virtual void resetWindow() = 0;
    virtual void addUnit (int type) = 0;
    virtual void clearRack() = 0;
    virtual int unitCount() = 0;
};

class Panel;

// Dimmed layer over the whole faceplate that holds one dialog at a time.
class Modal : public Component
{
public:
    explicit Modal (DialogHost& h);
    ~Modal() override;
    void openSettings();
    void openGuide();
    void openAbout();
    void openBrowser (bool renameMode = false);
    void openSave (const Preset* edit);
    void openExport (const Preset* one);   // nullptr exports all of yours
    void openImport();
    void openAdd();
    void openClear();
    void close();
    bool isOpen() const { return isVisible(); }

    void paint (Graphics& g) override;
    void resized() override;
    void mouseDown (const MouseEvent& e) override;
    bool keyPressed (const KeyPress& k) override;
    DialogHost& host;
private:
    void show (std::unique_ptr<Panel> p);
    std::unique_ptr<Panel> panel, dying;
};
} // namespace ui
