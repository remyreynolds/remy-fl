#include "PluginEditor.h"

namespace aimidi
{

AIMidiGenEditor::AIMidiGenEditor (AIMidiGenProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);

    headerLabel.setText ("AI MIDI Gen", juce::dontSendNotification);
    headerLabel.setFont (CustomLookAndFeel::font (22.0f, juce::Font::bold));
    addAndMakeVisible (headerLabel);

    subheaderLabel.setText ("Claude-powered MIDI workspace for FL Studio", juce::dontSendNotification);
    subheaderLabel.setFont (CustomLookAndFeel::font (12.5f));
    subheaderLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::muted);
    addAndMakeVisible (subheaderLabel);

    meterLabel.setText ("Parts ready", juce::dontSendNotification);
    meterLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    meterLabel.setJustificationType (juce::Justification::centred);
    meterLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::muted);
    addAndMakeVisible (meterLabel);

    meterValueLabel.setFont (CustomLookAndFeel::font (18.0f, juce::Font::bold));
    meterValueLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (meterValueLabel);

    apiStatusLabel.setFont (CustomLookAndFeel::font (11.5f));
    apiStatusLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::muted);
    addAndMakeVisible (apiStatusLabel);

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
        previewButton.setButtonText (on ? "Stop" : "Preview All");
    };
    addAndMakeVisible (previewButton);

    apiKeyLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    apiKeyLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::muted);
    addAndMakeVisible (apiKeyLabel);
    apiKeyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    apiKeyField.setTextToShowWhenEmpty ("sk-ant-…", CustomLookAndFeel::text.withAlpha (0.35f));
    if (processor.ai().hasApiKey()) apiKeyField.setText ("(loaded from env)", false);
    apiKeyField.onFocusLost = [this]
    {
        auto k = apiKeyField.getText().trim();
        if (k.isNotEmpty() && ! k.startsWith ("(")) processor.ai().setApiKey (k);
        apiStatusLabel.setText (processor.ai().hasApiKey() ? "Connected" : "Add key to generate",
                                juce::dontSendNotification);
    };
    addAndMakeVisible (apiKeyField);

    chatPanel.onSend = [this] (juce::String prompt) { handlePrompt (prompt); };
    addAndMakeVisible (chatPanel);

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums)
            continue; // Drums gets its own multi-piece panel below.

        auto panel = std::make_unique<InstrumentPanel> (type);

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

    // Drums: kick/snare/clap/closed hat/open hat as independent pieces,
    // each with its own Generate/Lock/Mute/Drag, plus a whole-loop shortcut.
    drumKitPanel.onGeneratePiece = [this] (DrumPiece dp)
    {
        processor.generateDrumPiece (dp);
        refreshPanels();
    };
    drumKitPanel.onGenerateAll = [this]
    {
        processor.generateDrumKit();
        refreshPanels();
    };
    drumKitPanel.onLockChanged = [this] (DrumPiece dp, bool locked)
    {
        processor.drumPiece (dp).locked = locked;
    };
    drumKitPanel.onMuteChanged = [this] (DrumPiece dp, bool muted)
    {
        processor.drumPiece (dp).muted = muted;
        processor.rebuildDrumMasterPart();
    };
    drumKitPanel.requestPieceMidiFile = [this] (DrumPiece dp) -> juce::File
    {
        return MidiGenerator::writeTempMidiFile (processor.drumPiece (dp),
                                                 processor.params(),
                                                 juce::String (toString (dp)));
    };
    drumKitPanel.requestFullKitMidiFile = [this] () -> juce::File
    {
        return MidiGenerator::writeTempMidiFile (processor.part (InstrumentType::Drums),
                                                 processor.params(),
                                                 "Drums");
    };
    addAndMakeVisible (drumKitPanel);

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
    int readyParts = 0;
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const bool hasNotes = ! processor.part ((InstrumentType) t).notes.empty();
        if (hasNotes) ++readyParts;

        if ((InstrumentType) t == InstrumentType::Drums) continue;
        panels[(size_t) t]->setHasContent (hasNotes);
    }

    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        drumKitPanel.setPieceHasContent ((DrumPiece) dp,
            ! processor.drumPiece ((DrumPiece) dp).notes.empty());

    meterValueLabel.setText (juce::String (readyParts) + "/7", juce::dontSendNotification);
    apiStatusLabel.setText (processor.ai().hasApiKey() ? "Connected" : "Add key to generate",
                            juce::dontSendNotification);
    repaint();
}

void AIMidiGenEditor::paint (juce::Graphics& g)
{
    g.fillAll (CustomLookAndFeel::bg);

    auto r = getLocalBounds().reduced (14);
    auto header = r.removeFromTop (86);
    CustomLookAndFeel::drawPanel (g, header);

    auto meter = header.removeFromRight (96).reduced (18, 12).toFloat();
    const float start = juce::MathConstants<float>::pi * 1.20f;
    const float end = juce::MathConstants<float>::pi * 3.80f;
    const float ready = meterValueLabel.getText().upToFirstOccurrenceOf ("/", false, false).getFloatValue();
    juce::Path baseArc, valueArc;
    baseArc.addCentredArc (meter.getCentreX(), meter.getCentreY(), 25.0f, 25.0f, 0.0f, start, end, true);
    valueArc.addCentredArc (meter.getCentreX(), meter.getCentreY(), 25.0f, 25.0f, 0.0f,
                            start, start + (end - start) * juce::jlimit (0.0f, 1.0f, ready / 7.0f), true);
    g.setColour (CustomLookAndFeel::divider);
    g.strokePath (baseArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (CustomLookAndFeel::accent);
    g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AIMidiGenEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto header = r.removeFromTop (86).reduced (14, 10);
    auto meter = header.removeFromRight (96);
    meterValueLabel.setBounds (meter.withSizeKeepingCentre (54, 24).translated (0, -3));
    meterLabel.setBounds (meter.removeFromBottom (18));

    auto topLine = header.removeFromTop (28);
    headerLabel.setBounds (topLine.removeFromLeft (190));
    generateAllButton.setBounds (topLine.removeFromRight (118));
    topLine.removeFromRight (8);
    previewButton.setBounds (topLine.removeFromRight (112));

    subheaderLabel.setBounds (header.removeFromTop (20));
    header.removeFromTop (7);
    apiKeyLabel.setBounds (header.removeFromLeft (92));
    apiKeyField.setBounds (header.removeFromLeft (260));
    header.removeFromLeft (10);
    apiStatusLabel.setBounds (header.removeFromLeft (140));

    r.removeFromTop (12);

    // Left: chat. Right: instrument grid.
    auto chatArea = r.removeFromLeft (juce::jmax (260, r.getWidth() / 3));
    chatPanel.setBounds (chatArea);
    r.removeFromLeft (12);

    // Grid of instrument panels (2 columns).
    const int cols = 2;
    const int n = (int) InstrumentType::NumTypes;
    const int rows = (n + cols - 1) / cols;
    const int gap = 10;
    const int cellW = (r.getWidth() - gap * (cols - 1)) / cols;
    const int cellH = (r.getHeight() - gap * (rows - 1)) / rows;

    for (int i = 0; i < n; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        juce::Rectangle<int> cell (r.getX() + col * (cellW + gap),
                                   r.getY() + row * (cellH + gap),
                                   cellW, cellH);

        if ((InstrumentType) i == InstrumentType::Drums)
            drumKitPanel.setBounds (cell);
        else
            panels[(size_t) i]->setBounds (cell);
    }
}

} // namespace aimidi
