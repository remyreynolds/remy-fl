#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include <vector>

namespace aimidi
{
/** Piano-roll style display of generated MIDI notes across instruments. */
class MidiRollView : public juce::Component
{
public:
    struct NoteDraw
    {
        double startBeats = 0.0;
        double lengthBeats = 1.0;
        int    pitch = 60;
        float  velocity = 0.8f;
        int    colourIndex = 0; // InstrumentType index
        bool   muted = false;
    };

    MidiRollView();

    void setLoopLengthBeats (double beats);
    void setNotes (std::vector<NoteDraw> notes);
    /** Playhead in quarter-note beats; pass < 0 to hide. */
    void setPlayheadBeats (double beats);
    /** Title override for focused-lane mode (empty = "Piano roll"). */
    void setFocusLabel (const juce::String& label);
    void clear();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void recomputePitchRange();
    juce::Rectangle<float> getNoteArea() const;

    juce::Label title;
    juce::Label subtitle;
    std::vector<NoteDraw> notes;
    double loopBeats = 16.0;
    double playheadBeats = -1.0;
    int pitchMin = 36;
    int pitchMax = 84;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRollView)
};

} // namespace aimidi
