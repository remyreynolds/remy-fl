#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include <functional>

namespace aimidi
{
/** One instrument's control strip: label + the full action set from the spec
    (Generate, Regenerate, Lock, Mute, Solo, Drag MIDI, Export). The "Drag MIDI"
    button initiates an external OS drag so the .mid drops straight into FL. */
class InstrumentPanel : public juce::Component
{
public:
    explicit InstrumentPanel (InstrumentType type);

    // Callbacks wired by the editor:
    std::function<void()>            onGenerate;
    std::function<void (bool)>       onLockChanged;
    std::function<void (bool)>       onMuteChanged;
    // Returns a freshly-written temp .mid file for this instrument to drag/export.
    std::function<juce::File()>      requestMidiFile;

    InstrumentType getType() const { return type; }
    void setHasContent (bool has);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void startMidiDrag();

    InstrumentType type;
    juce::Label     title;
    juce::TextButton generateBtn { "Generate" };
    juce::TextButton lockBtn  { "Lock" };
    juce::TextButton muteBtn  { "Mute" };
    juce::TextButton dragBtn  { "Drag MIDI" };
    juce::TextButton exportBtn{ "Export" };
    bool hasContent = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace aimidi
