#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/PluginProcessor.h"

class Face;

// Plugin window. The faceplate is a fixed 1200 x 640 drawing that is scaled to the window size;
// the window keeps its proportions and can be dragged from any edge or corner.
class CatRackEditor : public juce::AudioProcessorEditor
{
public:
    explicit CatRackEditor (CatRackProcessor&);
    ~CatRackEditor() override;
    void resized() override;
    void resetSize();
    void openDialogForTesting (const juce::String& name); // used by tools/snapshot.cpp
    void tickForTesting (int frames);                       // runs the display refresh without a timer
    void loadPresetForTesting (const juce::String& name);
    void selectForTesting (int slot);

    static constexpr int kW = 1200, kH = 640;

private:
    CatRackProcessor& proc;
    std::unique_ptr<Face> face;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableBorderComponent> border;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatRackEditor)
};
