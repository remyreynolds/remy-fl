#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include "../engine/PreviewSounds.h"
#include "../engine/SampleLibrary.h"
#include "../engine/MidiLibrary.h"
#include "FileDragButton.h"
#include <functional>
#include <vector>

namespace aimidi
{
/** One pitched-instrument strip: MIDI loop picker, synth style, generate/lock/mute. */
class InstrumentPanel : public juce::Component
{
public:
    explicit InstrumentPanel (InstrumentType type);

    std::function<void()>            onGenerate;
    std::function<void()>            onVary;
    std::function<void()>            onContinue;
    std::function<void (bool)>       onLockChanged;
    std::function<void (bool)>       onMuteChanged;
    std::function<void (PartTimbre)> onTimbreChanged;
    std::function<void (float)>      onVolumeChanged;
    std::function<void (juce::String)> onMidiLoopChanged; // empty = keep / AI generate
    std::function<void (juce::String)> onSampleChanged;   // empty id = built-in synth
    std::function<void()>            onMinimizeChanged;
    std::function<juce::File()>      requestMidiFile;

    InstrumentType getType() const { return type; }
    void setHasContent (bool has);
    void setChordSummary (const juce::String& summary);
    void setVolume (float gain01);
    void setSoundOptions (const std::vector<PartTimbre>& options, PartTimbre selected);
    void setMidiLoopOptions (const std::vector<const MidiEntry*>& options,
                             const juce::String& selectedId);
    void setSampleOptions (const std::vector<const SampleEntry*>& options,
                           const juce::String& selectedId);
    void setAiBusy (bool busy);

    bool isMinimized() const { return minimized; }
    void setMinimized (bool shouldMinimize);
    int preferredHeight() const { return minimized ? 34 : 232; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    InstrumentType type;
    juce::Label     title;
    juce::TextButton minimizeBtn { "−" };
    juce::Label     chordLabel;
    juce::Label     volLabel { {}, "Vol" };
    juce::Slider    volSlider;
    juce::Label     midiLabel { {}, "MIDI" };
    juce::ComboBox  midiCombo;
    juce::Label     synthLabel { {}, "Synth" };
    juce::ComboBox  soundCombo;
    juce::Label     sampleLabel { {}, "Sample" };
    juce::ComboBox  sampleCombo;
    juce::TextButton generateBtn { "Generate" };
    juce::TextButton varyBtn    { "Vary" };
    juce::TextButton continueBtn{ "Continue" };
    juce::TextButton lockBtn  { "Lock" };
    juce::TextButton muteBtn  { "Mute" };
    FileDragButton   dragBtn  { "Drag MIDI" };
    juce::TextButton exportBtn{ "Export" };

    bool hasContent = false;
    bool minimized = false;
    bool aiBusy = false;
    bool suppressVolCallback = false;
    bool suppressTimbreCallback = false;
    bool suppressMidiCallback = false;
    bool suppressSampleCallback = false;
    juce::StringArray midiIds;
    juce::StringArray sampleIds;

    void updateMinimizedUi();
    void updateActionEnabled();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace aimidi
