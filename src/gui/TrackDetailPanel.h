#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include "FileDragButton.h"
#include <functional>

namespace aimidi
{
/** Left-card track detail from ComposerAI VST v4 — knob, MIDI/SYNTH, actions, drag. */
class TrackDetailPanel : public juce::Component
{
public:
    TrackDetailPanel();

    std::function<void()> onGenerate;
    std::function<void()> onVary;
    std::function<void()> onContinue;
    std::function<void (bool)> onLockChanged;
    std::function<void (bool)> onMuteChanged;
    std::function<void (float)> onVolumeChanged; // 0..1
    std::function<void (int)> onTimbreChanged;   // PartTimbre index / combo id
    std::function<juce::File()> requestMidiFile;

    void setTrack (InstrumentType type, bool hasContent, const juce::String& notesLine);
    void setVolume (float gain01);
    void setLocked (bool locked);
    void setMuted (bool muted);
    void setGenerating (bool busy);
    void setTimbreOptions (const juce::StringArray& names, int selectedId);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void drawKnob (juce::Graphics& g, juce::Rectangle<float> area) const;

    InstrumentType type = InstrumentType::Melody;
    bool hasContent = false;
    bool generating = false;
    float volume = 0.72f;
    juce::String notesLine { "—" };
    juce::Rectangle<float> knobBounds;
    bool draggingKnob = false;
    float dragStartVol = 0.72f;
    int dragStartY = 0;

    juce::Label title;
    juce::Label trackTag { {}, "TRACK" };
    juce::Label midiLabel { {}, "MIDI" };
    juce::Label synthLabel { {}, "SYNTH" };
    juce::Label notesWell;
    juce::Label volLabel;
    juce::ComboBox midiCombo;
    juce::ComboBox synthCombo;
    juce::TextButton generateBtn { "Generate" };
    juce::TextButton varyBtn { "Vary" };
    juce::TextButton continueBtn { "Continue" };
    juce::TextButton lockBtn { "Lock" };
    juce::TextButton muteBtn { "Mute" };
    juce::TextButton exportBtn { "Export" };
    FileDragButton dragBtn { "Drag MIDI to host" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackDetailPanel)
};

} // namespace aimidi
