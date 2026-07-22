#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/CustomLookAndFeel.h"
#include "gui/ChatPanel.h"
#include "gui/InstrumentPanel.h"
#include "gui/DrumKitPanel.h"
#include <array>
#include <memory>

namespace aimidi
{
class AIMidiGenEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer
{
public:
    explicit AIMidiGenEditor (AIMidiGenProcessor&);
    ~AIMidiGenEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void handlePrompt (const juce::String& prompt);
    void refreshPanels();

    AIMidiGenProcessor& processor;
    CustomLookAndFeel   lnf;

    juce::Label   headerLabel;
    juce::Label   subheaderLabel;
    juce::Label   meterLabel;
    juce::Label   meterValueLabel;
    juce::Label   apiStatusLabel;
    juce::TextButton previewButton { "Preview All" };
    juce::TextButton generateAllButton { "Generate All" };
    juce::TextButton dnaButton { "Load MIDI DNA" };
    std::unique_ptr<juce::FileChooser> dnaChooser;
    juce::TextEditor apiKeyField;
    juce::Label   apiKeyLabel { {}, "Claude API key:" };
    juce::Label   styleLabel  { {}, "Style:" };
    juce::ComboBox styleBox;

    void applyStylePreset (int styleIndex);
    void syncStyleBoxToParams();

    ChatPanel chatPanel;
    std::array<std::unique_ptr<InstrumentPanel>, (size_t) InstrumentType::NumTypes> panels;
    DrumKitPanel drumKitPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMidiGenEditor)
};

} // namespace aimidi
