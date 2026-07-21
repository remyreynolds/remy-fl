#include "PluginEditor.h"

namespace aimidi
{

AIMidiGenEditor::AIMidiGenEditor (AIMidiGenProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);

    headerLabel.setText ("AI MIDI Gen", juce::dontSendNotification);
    headerLabel.setFont (juce::Font (20.0f, juce::Font::bold));
    addAndMakeVisible (headerLabel);

    generateAllButton.onClick = [this]
    {
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
            processor.generatePart ((InstrumentType) t);
        refreshPanels();
    };
    addAndMakeVisible (generateAllButton);

    previewButton.setClickingTogglesState (true);
    previewButton.onClick = [this]
    {
        const bool on = previewButton.getToggleState();
        processor.togglePreview (on);
        previewButton.setButtonText (on ? "■ Stop" : "▶ Preview All");
    };
    addAndMakeVisible (previewButton);

    apiKeyLabel.setFont (juce::Font (12.0f));
    addAndMakeVisible (apiKeyLabel);
    apiKeyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    apiKeyField.setTextToShowWhenEmpty ("sk-ant-…", CustomLookAndFeel::text.withAlpha (0.35f));
    if (processor.ai().hasApiKey()) apiKeyField.setText ("(loaded from env)", false);
    apiKeyField.onFocusLost = [this]
    {
        auto k = apiKeyField.getText().trim();
        if (k.isNotEmpty() && ! k.startsWith ("(")) processor.ai().setApiKey (k);
    };
    addAndMakeVisible (apiKeyField);

    chatPanel.onSend = [this] (juce::String prompt) { handlePrompt (prompt); };
    addAndMakeVisible (chatPanel);

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        auto panel = std::make_unique<InstrumentPanel> ((InstrumentType) t);
        const auto type = (InstrumentType) t;

        panel->onGenerate = [this, type]
        {
            processor.generatePart (type);
            refreshPanels();
        };
        panel->onLockChanged = [this, type] (bool locked)
        {
            processor.part (type).locked = locked;
        };
        panel->onMuteChanged = [this, type] (bool muted)
        {
            processor.part (type).muted = muted;
        };
        panel->requestMidiFile = [this, type] () -> juce::File
        {
            return MidiGenerator::writeTempMidiFile (processor.part (type),
                                                     processor.params(),
                                                     juce::String (toString (type)));
        };

        addAndMakeVisible (*panel);
        panels[(size_t) t] = std::move (panel);
    }

    refreshPanels();
    setResizable (true, true);
    setResizeLimits (760, 520, 1600, 1100);
    setSize (960, 640);
}

AIMidiGenEditor::~AIMidiGenEditor()
{
    setLookAndFeel (nullptr);
}

void AIMidiGenEditor::handlePrompt (const juce::String& prompt)
{
    chatPanel.setBusy (true);
    processor.regenerateFromAI (prompt,
        [this] (AIClient::Response r)
        {
            chatPanel.setBusy (false);
            if (r.error.isNotEmpty())
                chatPanel.addAssistantMessage ("⚠ " + r.error);
            chatPanel.addAssistantMessage (r.assistantText);
            refreshPanels();
        });
}

void AIMidiGenEditor::refreshPanels()
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        panels[(size_t) t]->setHasContent (! processor.part ((InstrumentType) t).notes.empty());
}

void AIMidiGenEditor::paint (juce::Graphics& g)
{
    g.fillAll (CustomLookAndFeel::bg);
}

void AIMidiGenEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Header row
    auto header = r.removeFromTop (34);
    headerLabel.setBounds (header.removeFromLeft (170));
    previewButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    generateAllButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (12);
    apiKeyField.setBounds (header.removeFromRight (200));
    header.removeFromRight (4);
    apiKeyLabel.setBounds (header.removeFromRight (100));

    r.removeFromTop (10);

    // Left: chat. Right: instrument grid.
    auto chatArea = r.removeFromLeft (juce::jmax (260, r.getWidth() / 3));
    chatPanel.setBounds (chatArea);
    r.removeFromLeft (10);

    // Grid of instrument panels (2 columns).
    const int cols = 2;
    const int n = (int) InstrumentType::NumTypes;
    const int rows = (n + cols - 1) / cols;
    const int gap = 8;
    const int cellW = (r.getWidth() - gap * (cols - 1)) / cols;
    const int cellH = (r.getHeight() - gap * (rows - 1)) / rows;

    for (int i = 0; i < n; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        panels[(size_t) i]->setBounds (r.getX() + col * (cellW + gap),
                                       r.getY() + row * (cellH + gap),
                                       cellW, cellH);
    }
}

} // namespace aimidi
