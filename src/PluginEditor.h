#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/CustomLookAndFeel.h"
#include "gui/ChatPanel.h"
#include "gui/InstrumentPanel.h"
#include "gui/DrumKitPanel.h"
#include "gui/MidiRollView.h"
#include "gui/ChordDashboardView.h"
#include <array>
#include <memory>

namespace aimidi
{
class AIMidiGenEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,
                        private juce::Timer
{
public:
    explicit AIMidiGenEditor (AIMidiGenProcessor&);
    ~AIMidiGenEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void handlePrompt (const juce::String& prompt);
    void handlePartTransform (InstrumentType type, const juce::String& mode);
    void refreshPanels();
    void refreshMidiRoll();
    void refreshBpmLabel();
    void refreshSoundControls();
    void refreshChordDashboard();
    void refreshSampleControls();
    void refreshMidiLoopControls();
    void refreshProviderUi();
    void applyWorkspaceFocus();
    bool isTypeInFocus (InstrumentType type) const;
    void generateFocusedParts();
    void exportAllTracks();
    void nudgeBpm (int delta);
    void timerCallback() override;
    InstrumentType pickPartForMidiAttach() const;

    AIMidiGenProcessor& processor;
    CustomLookAndFeel   lnf;

    juce::Label   headerLabel;
    juce::Label   subheaderLabel;
    juce::Label   apiStatusLabel;
    juce::Label   meterValueLabel;
    juce::Label   bpmLabel { {}, "BPM" };
    juce::Label   bpmValue;
    juce::TextButton bpmMinus { "−" };
    juce::TextButton bpmPlus  { "+" };
    juce::Label   soundLabel { {}, "Genre" };
    juce::ComboBox genreCombo;
    juce::Label   keyLabel { {}, "Key" };
    juce::ComboBox rootCombo;
    juce::ComboBox scaleCombo;
    juce::Label   focusLabel { {}, "Focus" };
    juce::ComboBox focusCombo;
    juce::Label   packLabel { {}, "Pack" };
    juce::ComboBox packCombo;
    juce::Label   midiPackLabel { {}, "MIDI" };
    juce::ComboBox midiPackCombo;
    juce::ComboBox midiKitCombo;
    juce::TextButton loadMidiKitButton { "Load kit" };
    juce::Label   sampleFilterLabel { {}, "Type" };
    juce::ComboBox sampleFilterCombo;
    juce::TextButton previewButton { "Preview" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton generateAllButton { "Generate all" };
    juce::TextButton exportAllButton { "Export all" };
    juce::TextButton addSoundsButton { "Add sounds" };
    juce::TextButton addMidiButton { "Add MIDI" };
    juce::TextButton soundsFolderButton { "Folder" };
    juce::Label   samplesStatusLabel;
    juce::Label   providerLabel { {}, "AI" };
    juce::ComboBox providerCombo;
    juce::Label   modelLabel { {}, "Model" };
    juce::ComboBox modelCombo;
    juce::TextEditor apiKeyField;
    juce::Label   apiKeyLabel { {}, "API key" };
    bool suppressGenreCallback = false;
    bool suppressKeyCallback = false;
    bool suppressFocusCallback = false;
    bool suppressSampleFilterCallback = false;
    bool suppressPackCallback = false;
    bool suppressMidiPackCallback = false;
    bool suppressProviderCallback = false;
    bool suppressModelCallback = false;

    int readyParts = 0;

    MidiRollView midiRoll;
    ChordDashboardView chordDashboard;
    ChatPanel chatPanel;
    std::array<std::unique_ptr<InstrumentPanel>, (size_t) InstrumentType::NumTypes> panels;
    DrumKitPanel drumKitPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMidiGenEditor)
};

} // namespace aimidi
