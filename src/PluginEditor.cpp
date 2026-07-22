#include "PluginEditor.h"
#include "engine/ChordAnalysis.h"
#include <cmath>

namespace aimidi
{

AIMidiGenEditor::AIMidiGenEditor (AIMidiGenProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);

    // Surface containers — only the active one is visible.
    addChildComponent (generateSurface);
    addChildComponent (browseSurface);
    addChildComponent (chatSurface);
    addChildComponent (settingsSurface);

    generateTabButton.onClick = [this] { setSurface (Surface::Generate); };
    browseTabButton.onClick   = [this] { setSurface (Surface::Browse); };
    chatTabButton.onClick     = [this] { setSurface (Surface::Chat); };
    settingsTabButton.onClick = [this] { setSurface (Surface::Settings); };
    generateTabButton.setComponentID ("tab");
    browseTabButton.setComponentID ("tab");
    chatTabButton.setComponentID ("tab");
    settingsTabButton.setComponentID ("tab");
    generateTabButton.setClickingTogglesState (true);
    browseTabButton.setClickingTogglesState (true);
    chatTabButton.setClickingTogglesState (true);
    settingsTabButton.setClickingTogglesState (true);
    generateTabButton.setRadioGroupId (1001);
    browseTabButton.setRadioGroupId (1001);
    chatTabButton.setRadioGroupId (1001);
    settingsTabButton.setRadioGroupId (1001);
    addAndMakeVisible (generateTabButton);
    addAndMakeVisible (browseTabButton);
    addAndMakeVisible (chatTabButton);
    addAndMakeVisible (settingsTabButton);

    headerLabel.setText ("MIDI AGENT", juce::dontSendNotification);
    headerLabel.setFont (CustomLookAndFeel::font (12.0f, juce::Font::bold));
    headerLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (headerLabel);

    apiStatusLabel.setVisible (false);

    bpmLabel.setFont (CustomLookAndFeel::font (11.0f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    bpmLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (bpmLabel);

    bpmValue.setFont (CustomLookAndFeel::font (13.0f, juce::Font::bold));
    bpmValue.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    bpmValue.setJustificationType (juce::Justification::centred);
    bpmValue.setEditable (true, true, false);
    bpmValue.onTextChange = [this]
    {
        const int v = bpmValue.getText().retainCharacters ("0123456789").getIntValue();
        if (v > 0)
        {
            processor.setBpm ((double) v);
            refreshBpmLabel();
        }
    };
    addAndMakeVisible (bpmValue);

    bpmMinus.onClick = [this] { nudgeBpm (-1); };
    bpmPlus.onClick  = [this] { nudgeBpm (+1); };
    addAndMakeVisible (bpmMinus);
    addAndMakeVisible (bpmPlus);
    refreshBpmLabel();

    generateAllButton.setComponentID ("outline");
    generateAllButton.setTooltip ("Generate all unlocked parts");
    generateAllButton.onClick = [this] { generateFocusedParts(); };
    generateSurface.addAndMakeVisible (generateAllButton);

    generateLaneButton.setComponentID ("primary");
    generateLaneButton.setTooltip ("Generate the focused instrument lane");
    generateLaneButton.onClick = [this] { generateFocusedLane(); };
    generateSurface.addAndMakeVisible (generateLaneButton);

    varyLaneButton.setComponentID ("outline");
    varyLaneButton.setTooltip ("AI variation of the focused lane");
    varyLaneButton.onClick = [this]
    {
        if (focusedLane == InstrumentType::Drums)
            chatPanel.addAssistantMessage ("Vary drums from the drum kit panel, or chat \"vary the drums\".");
        else
            handlePartTransform (focusedLane, "vary");
    };
    generateSurface.addAndMakeVisible (varyLaneButton);

    hostSyncButton.setComponentID ("ghost");
    hostSyncButton.setClickingTogglesState (true);
    hostSyncButton.setToggleState (true, juce::dontSendNotification);
    hostSyncButton.setTooltip ("Follow the DAW project tempo");
    hostSyncButton.onClick = [this]
    {
        processor.setHostTempoSync (hostSyncButton.getToggleState());
        refreshBpmLabel();
    };
    addAndMakeVisible (hostSyncButton);

    hostMidiButton.setComponentID ("ghost");
    hostMidiButton.setClickingTogglesState (true);
    hostMidiButton.setToggleState (true, juce::dontSendNotification);
    hostMidiButton.setTooltip ("Emit MIDI to the host when the DAW is playing");
    hostMidiButton.onClick = [this]
    {
        processor.setHostMidiOut (hostMidiButton.getToggleState());
    };
    addAndMakeVisible (hostMidiButton);

    exportAllButton.setComponentID ("outline");
    exportAllButton.setTooltip ("Drag the full multi-track MIDI into FL Studio, or click to save it as a file");
    exportAllButton.onClick = [this] { exportAllTracks(); };
    exportAllButton.getFileToDrag = [this] () -> juce::File
    {
        return processor.exportAllPartsMidiFile();
    };
    addAndMakeVisible (exportAllButton);

    undoButton.setComponentID ("outline");
    bpmMinus.setComponentID ("ghost");
    bpmPlus.setComponentID ("ghost");
    undoButton.setTooltip ("Undo last generation");
    undoButton.onClick = [this]
    {
        if (processor.undoLastGeneration())
        {
            refreshBpmLabel();
            refreshPanels();
        }
    };
    generateSurface.addAndMakeVisible (undoButton);

    addSoundsButton.setComponentID ("outline");
    addSoundsButton.setTooltip ("Import a sound pack folder (kicks, hats, etc.) — auto-sorted for preview");
    addSoundsButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Import sound pack folder",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
            "*", true); // directories

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                juce::String err;
                const int n = processor.samples().importPackFolder (dir, &err);
                if (n > 0)
                {
                    const int assigned = processor.autoAssignSamplesFromLibrary (dir.getFileName());
                    processor.rememberDrumKitInBrain (dir.getFileName());
                    chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
                    chatPanel.addAssistantMessage (
                        "Imported drum kit \"" + dir.getFileName() + "\" (" + juce::String (n)
                        + " files). Wired " + juce::String (assigned)
                        + " drum pieces for preview. Kit memory saved to brain for Claude.");
                }
                else
                {
                    chatPanel.addAssistantMessage (
                        "Could not import pack: " + (err.isNotEmpty() ? err : juce::String ("unknown error")));
                }
                refreshSampleControls();
                samplesStatusLabel.setText (processor.samples().statusLine(),
                                            juce::dontSendNotification);
            });
    };
    browseSurface.addAndMakeVisible (addSoundsButton);

    addMidiButton.setComponentID ("outline");
    addMidiButton.setTooltip ("Import a folder of .mid loops for Melody / Bass / Chords / etc.");
    addMidiButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Import MIDI pack folder",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
            "*", true);

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                juce::String err;
                const int n = processor.midiLoops().importPackFolder (dir, &err);
                if (n > 0)
                {
                    processor.rememberMidiPackInBrain (dir.getFileName());
                    chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
                    chatPanel.addAssistantMessage (
                        "Imported " + juce::String (n) + " MIDI loops from \""
                        + dir.getFileName()
                        + "\". Pick a Kit in the MIDI kit menu and hit Load kit, "
                          "or choose loops on each instrument’s MIDI menu.");
                }
                else
                {
                    chatPanel.addAssistantMessage (
                        "Could not import MIDI pack: "
                        + (err.isNotEmpty() ? err : juce::String ("no .mid files found")));
                }
                refreshMidiLoopControls();
            });
    };
    browseSurface.addAndMakeVisible (addMidiButton);

    midiPackLabel.setText ("MIDI pack", juce::dontSendNotification);
    midiPackLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    midiPackLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    midiPackLabel.setJustificationType (juce::Justification::centredRight);
    browseSurface.addAndMakeVisible (midiPackLabel);

    midiPackCombo.setTextWhenNothingSelected ("No MIDI packs");
    midiPackCombo.setTooltip ("Your imported MIDI packs (Funky House, etc.)");
    midiPackCombo.onChange = [this]
    {
        if (suppressMidiPackCallback) return;
        refreshMidiLoopControls();
    };
    browseSurface.addAndMakeVisible (midiPackCombo);

    midiKitCombo.setTextWhenNothingSelected ("Kit_1…");
    midiKitCombo.setTooltip ("Matching Bass+Chord+Lead+Guitar set");
    browseSurface.addAndMakeVisible (midiKitCombo);

    loadMidiKitButton.setButtonText ("Load kit");
    loadMidiKitButton.setComponentID ("primary");
    loadMidiKitButton.setTooltip ("Load Bass/Chords/Lead/Guitar from this kit into preview");
    loadMidiKitButton.onClick = [this]
    {
        juce::String pack;
        if (midiPackCombo.getSelectedId() > 1)
            pack = midiPackCombo.getItemText (midiPackCombo.getSelectedItemIndex());
        else if (processor.midiLoops().packNames().size() > 0)
            pack = processor.midiLoops().packNames()[0];

        const juce::String kit = midiKitCombo.getText();
        juce::String err;
        const int n = processor.applyMidiKitBundle (pack, kit, &err);
        if (n > 0)
        {
            processor.rememberMidiPackInBrain (pack);
            chatPanel.addAssistantMessage (
                "Loaded " + kit + " from \"" + pack + "\" (" + juce::String (n)
                + " parts). Hit Preview.");
            refreshBpmLabel();
            refreshPanels();
        }
        else
        {
            chatPanel.addAssistantMessage (
                "Could not load kit: " + (err.isNotEmpty() ? err : juce::String ("nothing found")));
        }
    };
    browseSurface.addAndMakeVisible (loadMidiKitButton);

    soundsFolderButton.setTooltip ("Open samples folder in Finder, then rescan");
    soundsFolderButton.onClick = [this]
    {
        processor.samples().folder().revealToUser();
        processor.samples().reload();
        if (! processor.hasAnySampleAssignment() && processor.samples().size() > 0)
            processor.autoAssignSamplesFromLibrary();
        refreshSampleControls();
    };
    browseSurface.addAndMakeVisible (soundsFolderButton);

    samplesStatusLabel.setFont (CustomLookAndFeel::font (11.0f));
    samplesStatusLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    samplesStatusLabel.setText (processor.samples().statusLine(), juce::dontSendNotification);
    browseSurface.addAndMakeVisible (samplesStatusLabel);

    packLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    packLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    packLabel.setJustificationType (juce::Justification::centredRight);
    browseSurface.addAndMakeVisible (packLabel);

    packCombo.setTextWhenNothingSelected ("No packs yet");
    packCombo.setTooltip ("Your imported sound packs — selecting one loads those WAVs into preview");
    packCombo.onChange = [this]
    {
        if (suppressPackCallback) return;
        const int id = packCombo.getSelectedId();
        juce::String packName;
        if (id > 1)
            packName = packCombo.getItemText (packCombo.getSelectedItemIndex());

        // Make this pack’s files the drum preview only; save kit into the brain
        if (packName.isNotEmpty() || processor.samples().size() > 0)
        {
            const int n = processor.autoAssignSamplesFromLibrary (packName);
            processor.rememberDrumKitInBrain (packName);
            chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
            chatPanel.addAssistantMessage (
                "Drum kit loaded for Drums only (" + juce::String (n)
                + " pieces). Melody/bass/chords stay synth. Kit + drum theory saved to brain.");
        }
        refreshSampleControls();
    };
    browseSurface.addAndMakeVisible (packCombo);

    previewButton.setComponentID ("ghost");
    previewButton.setClickingTogglesState (true);
    previewButton.onClick = [this]
    {
        const bool on = previewButton.getToggleState();
        processor.togglePreview (on);
        previewButton.setButtonText (on ? "Stop" : "Preview");
        if (! on)
            midiRoll.setPlayheadBeats (-1.0);
    };
    addAndMakeVisible (previewButton);

    // MIDI-pack DNA: drop in a loop you love and generation inherits its
    // groove + key. Analysis only — the loop's notes are never copied.
    dnaButton.setComponentID ("ghost");
    dnaButton.setTooltip ("Learn groove + key from a MIDI loop — generation "
                          "inherits its feel (the loop's notes are never copied)");
    dnaButton.onClick = [this]
    {
        dnaChooser = std::make_unique<juce::FileChooser> (
            "Pick a MIDI loop to learn from", juce::File{}, "*.mid;*.midi");
        dnaChooser->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File{}) return;
                chatPanel.addAssistantMessage (processor.loadDna (f));
                chatPanel.addAssistantMessage (processor.lastCriticSummary());
                refreshPanels();
            });
    };
    generateSurface.addAndMakeVisible (dnaButton);

    soundLabel.setText ("Genre", juce::dontSendNotification);
    soundLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    soundLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    soundLabel.setJustificationType (juce::Justification::centredRight);
    generateSurface.addAndMakeVisible (soundLabel);

    for (int g = 0; g < (int) GenreMode::NumModes; ++g)
        genreCombo.addItem (toString ((GenreMode) g), g + 1);
    genreCombo.setTextWhenNothingSelected ("House");
    genreCombo.setTooltip ("Preview sounds for this genre (House piano, 808s, etc.)");
    genreCombo.onChange = [this]
    {
        if (suppressGenreCallback) return;
        const int id = genreCombo.getSelectedId();
        if (id > 0)
        {
            processor.setGenreMode (genreModeFromIndex (id - 1), true);
            refreshSoundControls();
        }
    };
    generateSurface.addAndMakeVisible (genreCombo);
    genreCombo.toFront (false);

    keyLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    keyLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    keyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (keyLabel);

    const char* roots[] = { "C", "C#", "Db", "D", "D#", "Eb", "E", "F",
                            "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B" };
    for (int i = 0; i < (int) (sizeof (roots) / sizeof (roots[0])); ++i)
        rootCombo.addItem (roots[i], i + 1);
    rootCombo.setTooltip ("Project key root — generations stay in this key");
    rootCombo.onChange = [this]
    {
        if (suppressKeyCallback) return;
        if (rootCombo.getSelectedId() > 0)
            processor.params().root = rootCombo.getText().toStdString();
    };
    addAndMakeVisible (rootCombo);

    const char* scales[] = {
        "major", "minor", "dorian", "phrygian", "lydian",
        "mixolydian", "locrian", "harmonicMinor",
        "minorPentatonic", "majorPentatonic"
    };
    for (int i = 0; i < (int) (sizeof (scales) / sizeof (scales[0])); ++i)
        scaleCombo.addItem (scales[i], i + 1);
    scaleCombo.setTooltip ("Project scale — generations stay in this scale");
    scaleCombo.onChange = [this]
    {
        if (suppressKeyCallback) return;
        if (scaleCombo.getSelectedId() > 0)
            processor.params().scale = scaleCombo.getText().toStdString();
    };
    addAndMakeVisible (scaleCombo);

    focusLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    focusLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    focusLabel.setJustificationType (juce::Justification::centredRight);
    chatSurface.addAndMakeVisible (focusLabel);

    focusCombo.addItem ("All", 1);
    focusCombo.addItem ("Drums only", 2);
    focusCombo.addItem ("Harmonic (Chords/Pad)", 3);
    focusCombo.addItem ("Bass + Drums", 4);
    focusCombo.addItem ("Leads (Melody/Arp/Counter)", 5);
    focusCombo.addItem ("Bass only", 6);
    focusCombo.setSelectedId (1, juce::dontSendNotification);
    focusCombo.setTooltip ("Show only the instruments you want to work on");
    focusCombo.onChange = [this]
    {
        if (suppressFocusCallback) return;
        applyWorkspaceFocus();
        resized();
    };
    chatSurface.addAndMakeVisible (focusCombo);

    sampleFilterLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    sampleFilterLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    sampleFilterLabel.setJustificationType (juce::Justification::centredRight);
    browseSurface.addAndMakeVisible (sampleFilterLabel);

    sampleFilterCombo.addItem ("All types", 1);
    for (int r = 0; r < (int) SampleRole::NumRoles; ++r)
        sampleFilterCombo.addItem (toString ((SampleRole) r), r + 2);
    sampleFilterCombo.setSelectedId (1, juce::dontSendNotification);
    sampleFilterCombo.setTooltip ("Filter sample dropdowns by Kick / Hat / etc.");
    sampleFilterCombo.onChange = [this]
    {
        if (suppressSampleFilterCallback) return;
        refreshSampleControls();
    };
    browseSurface.addAndMakeVisible (sampleFilterCombo);

    apiKeyLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    apiKeyLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    settingsSurface.addAndMakeVisible (apiKeyLabel);

    providerLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    providerLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    settingsSurface.addAndMakeVisible (providerLabel);

    providerCombo.addItem ("Claude", 1);
    providerCombo.addItem ("OpenAI", 2);
    providerCombo.setTooltip ("LLM provider (BYOK — your API key)");
    providerCombo.onChange = [this]
    {
        if (suppressProviderCallback) return;
        const auto p = providerCombo.getSelectedId() == 2
                           ? AIClient::Provider::OpenAI
                           : AIClient::Provider::Claude;
        processor.ai().setProvider (p);
        refreshProviderUi();
        refreshPanels();
    };
    settingsSurface.addAndMakeVisible (providerCombo);

    modelLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    modelLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    settingsSurface.addAndMakeVisible (modelLabel);

    modelCombo.setTooltip ("Claude model — Opus is strongest for musical MIDI; Sonnet 5 is the new default");
    modelCombo.onChange = [this]
    {
        if (suppressModelCallback) return;
        if (processor.ai().getProvider() == AIClient::Provider::OpenAI)
            processor.ai().setOpenAiModel (modelCombo.getText());
        else
            processor.ai().setClaudeModel (modelCombo.getText());
        refreshPanels();
    };
    settingsSurface.addAndMakeVisible (modelCombo);

    apiKeyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    apiKeyField.setTextToShowWhenEmpty (processor.ai().apiKeyPlaceholder(),
                                        CustomLookAndFeel::txt2.withAlpha (0.55f));
    if (processor.ai().hasApiKey())
        apiKeyField.setText (processor.ai().apiKeyFromEnvironment()
                                 ? "(loaded from env)"
                                 : "(saved)",
                             false);
    apiKeyField.onFocusLost = [this]
    {
        auto k = apiKeyField.getText().trim();
        if (k.isNotEmpty() && ! k.startsWith ("("))
        {
            processor.ai().setApiKey (k);
            apiKeyField.setText ("(saved)", false);
        }
        refreshPanels();
    };
    settingsSurface.addAndMakeVisible (apiKeyField);
    refreshProviderUi();

    generateSurface.addAndMakeVisible (midiRoll);
    generateSurface.addAndMakeVisible (chordDashboard);

    chatPanel.onSend = [this] (juce::String prompt) { handlePrompt (prompt); };
    chatPanel.onRequestMidiAttach = [this] () -> juce::String
    {
        const auto type = pickPartForMidiAttach();
        if (processor.part (type).notes.empty())
            return {};
        return processor.midiContextForPart (type);
    };
    chatPanel.onAddDocs = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Upload music theory docs",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
            "*.txt;*.md;*.text;*.markdown");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto results = fc.getResults();
                int imported = 0;
                juce::String lastError;
                for (auto& f : results)
                {
                    juce::String err;
                    if (processor.ai().knowledge().importFile (f, &err))
                        ++imported;
                    else if (err.isNotEmpty())
                        lastError = err;
                }

                chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
                if (imported > 0)
                    chatPanel.addAssistantMessage (
                        "Added " + juce::String (imported)
                        + " theory doc" + (imported == 1 ? "" : "s")
                        + ". Claude will reference them on the next generate.");
                else if (lastError.isNotEmpty())
                    chatPanel.addAssistantMessage ("Could not import doc: " + lastError);
            });
    };
    chatPanel.onShowDocsFolder = [this]
    {
        processor.ai().knowledge().folder().revealToUser();
    };
    chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
    chatSurface.addAndMakeVisible (chatPanel);

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums)
            continue;

        auto panel = std::make_unique<InstrumentPanel> (type);

        panel->onGenerate = [this, type]
        {
            processor.generatePart (type);
            refreshPanels();
        };
        panel->onVary = [this, type] { handlePartTransform (type, "vary"); };
        panel->onContinue = [this, type] { handlePartTransform (type, "continue"); };
        panel->onLockChanged = [this, type] (bool locked)
        {
            processor.part (type).locked = locked;
        };
        panel->onMuteChanged = [this, type] (bool muted)
        {
            processor.part (type).muted = muted;
            refreshMidiRoll();
        };
        panel->onTimbreChanged = [this, type] (PartTimbre timbre)
        {
            processor.setPartTimbre (type, timbre);
        };
        panel->onVolumeChanged = [this, type] (float g)
        {
            processor.setPartGain (type, g);
        };
        panel->onMidiLoopChanged = [this, type] (juce::String id)
        {
            juce::String err;
            if (! processor.applyMidiLoopToPart (type, id, &err))
            {
                if (err.isNotEmpty())
                    chatPanel.addAssistantMessage ("MIDI load: " + err);
                return;
            }
            if (id.isNotEmpty())
                chatPanel.addAssistantMessage (
                    juce::String (toString (type)) + " loaded MIDI loop — Preview to hear it.");
            refreshPanels();
        };
        panel->onMinimizeChanged = [this] { resized(); };
        panel->requestMidiFile = [this, type] () -> juce::File
        {
            return MidiGenerator::writeTempMidiFile (processor.part (type),
                                                     processor.params(),
                                                     juce::String (toString (type)));
        };

        generateSurface.addChildComponent (*panel); // detail panel — shown for focused pitched lane
        panels[(size_t) t] = std::move (panel);
    }

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        auto lane = std::make_unique<InstrumentLane> (type);
        lane->onSelect = [this, type] { setFocusedLane (type); };
        lane->onGenerate = [this, type]
        {
            if (type == InstrumentType::Drums)
                processor.generateDrumKit();
            else
                processor.generatePart (type);
            refreshPanels();
        };
        lane->onLockChanged = [this, type] (bool locked)
        {
            if (type == InstrumentType::Drums)
            {
                for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
                    processor.drumPiece ((DrumPiece) dp).locked = locked;
            }
            else
                processor.part (type).locked = locked;
        };
        lane->onMuteChanged = [this, type] (bool muted)
        {
            if (type == InstrumentType::Drums)
            {
                for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
                    processor.drumPiece ((DrumPiece) dp).muted = muted;
                processor.rebuildDrumMasterPart();
            }
            else
                processor.part (type).muted = muted;
            refreshMidiRoll();
        };
        lane->requestMidiFile = [this, type] () -> juce::File
        {
            if (type == InstrumentType::Drums)
                return MidiGenerator::writeTempMidiFile (processor.part (InstrumentType::Drums),
                                                         processor.params(), "Drums");
            return MidiGenerator::writeTempMidiFile (processor.part (type),
                                                     processor.params(),
                                                     juce::String (toString (type)));
        };
        generateSurface.addAndMakeVisible (*lane);
        lanes[(size_t) t] = std::move (lane);
    }

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
        refreshMidiRoll();
    };
    drumKitPanel.onVolumeChanged = [this] (DrumPiece dp, float g)
    {
        processor.setDrumGain (dp, g);
    };
    drumKitPanel.onSampleChanged = [this] (DrumPiece dp, juce::String id)
    {
        processor.setDrumSampleId (dp, id);
        if (id.isNotEmpty())
        {
            auto* e = processor.samples().findById (id);
            const bool ok = e != nullptr
                            && processor.samples().ensureLoaded (*e) != nullptr;
            if (! ok)
                chatPanel.addAssistantMessage (
                    "Couldn't load sample for " + juce::String (toString (dp))
                    + " — check the file still exists in Packs.");
        }
    };
    drumKitPanel.onMinimizeChanged = [this] { resized(); };
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
    generateSurface.addAndMakeVisible (drumKitPanel);

    // Packs already on disk — assign their WAVs as the preview instruments.
    if (processor.samples().size() > 0)
    {
        auto packs = processor.samples().packNames();
        juce::String list;
        for (int i = 0; i < packs.size(); ++i)
        {
            if (i > 0) list << ", ";
            list << "\"" << packs[i] << "\"";
        }

        const juce::String primary = packs.isEmpty() ? juce::String() : packs[0];
        const int n = processor.autoAssignSamplesFromLibrary (primary);
        processor.rememberDrumKitInBrain (primary);
        chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
        const int loaded = processor.countLoadedPreviewSamples();
        chatPanel.addAssistantMessage (
            "Drums use pack: " + list + " (" + juce::String (loaded)
            + " WAVs loaded). Melody/chords/bass use synth + theory. "
            "Kit memory is in the brain. Focus → Drums only → Gen drums → Preview.");
    }
    else
    {
        chatPanel.addAssistantMessage (
            "No sound packs yet. Click Add sounds and choose a kit folder (with Kick/Snare/etc).");
    }

    if (processor.midiLoops().size() > 0)
    {
        auto midiPacks = processor.midiLoops().packNames();
        const juce::String primaryMidi = midiPacks.isEmpty() ? juce::String() : midiPacks[0];
        processor.rememberMidiPackInBrain (primaryMidi);
        chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
        juce::String midiList;
        for (int i = 0; i < midiPacks.size(); ++i)
        {
            if (i > 0) midiList << ", ";
            midiList << "\"" << midiPacks[i] << "\"";
        }
        chatPanel.addAssistantMessage (
            "MIDI packs ready: " + midiList + " ("
            + juce::String (processor.midiLoops().size())
            + " loops). Choose MIDI pack → Kit_1… → Load kit, or pick loops on each instrument.");
    }

    refreshPanels();
    setFocusedLane (InstrumentType::Chords);
    setWantsKeyboardFocus (true);
    startTimerHz (30); // playhead tracking
    setResizable (true, true);
    setResizeLimits (980, 780, 2000, 1400);
    setSize (1440, 960);
    setSurface (Surface::Generate);
}

void AIMidiGenEditor::setSurface (Surface s)
{
    currentSurface = s;
    generateSurface.setVisible (s == Surface::Generate);
    browseSurface.setVisible (s == Surface::Browse);
    chatSurface.setVisible (s == Surface::Chat);
    settingsSurface.setVisible (s == Surface::Settings);
    generateTabButton.setToggleState (s == Surface::Generate, juce::dontSendNotification);
    browseTabButton.setToggleState (s == Surface::Browse, juce::dontSendNotification);
    chatTabButton.setToggleState (s == Surface::Chat, juce::dontSendNotification);
    settingsTabButton.setToggleState (s == Surface::Settings, juce::dontSendNotification);
    resized();
    repaint();
}

AIMidiGenEditor::~AIMidiGenEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AIMidiGenEditor::nudgeBpm (int delta)
{
    processor.setBpm (processor.getBpm() + (double) delta);
    refreshBpmLabel();
}

void AIMidiGenEditor::refreshBpmLabel()
{
    bpmValue.setText (juce::String ((int) std::round (processor.getBpm())),
                      juce::dontSendNotification);

    const bool hostSync = processor.isHostTempoSync();
    bpmValue.setEditable (! hostSync, ! hostSync, false);
    bpmMinus.setEnabled (! hostSync);
    bpmPlus.setEnabled (! hostSync);
    bpmLabel.setText (hostSync ? "HOST" : "BPM", juce::dontSendNotification);
    hostSyncButton.setToggleState (hostSync, juce::dontSendNotification);
    hostMidiButton.setToggleState (processor.isHostMidiOut(), juce::dontSendNotification);
}

void AIMidiGenEditor::timerCallback()
{
    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = processor.getBpm();
    const bool haveHost = processor.getHostTransport (hostPlaying, hostPpq, hostBpm);

    if (processor.isPreviewing())
    {
        const double beats = processor.getPreviewPositionBeats();
        midiRoll.setPlayheadBeats (beats);
        chordDashboard.setPlayheadBeats (beats);
    }
    else if (haveHost && hostPlaying && processor.isHostMidiOut())
    {
        const double loop = juce::jmax (1.0, processor.getLoopLengthBeats());
        const double beats = std::fmod (hostPpq, loop);
        midiRoll.setPlayheadBeats (beats);
        chordDashboard.setPlayheadBeats (beats);
    }
    else if (! previewButton.getToggleState())
    {
        midiRoll.setPlayheadBeats (-1.0);
        chordDashboard.setPlayheadBeats (-1.0);
    }

    if (processor.isHostTempoSync() && haveHost && hostBpm > 0.0
        && std::abs (hostBpm - processor.getBpm()) > 0.05)
        refreshBpmLabel();

    undoButton.setEnabled (processor.canUndo());
}

void AIMidiGenEditor::handlePrompt (const juce::String& prompt)
{
    chatPanel.setBusy (true);
    for (auto& panel : panels)
        if (panel) panel->setAiBusy (true);

    processor.handleChatTurn (prompt,
        [this] (AIClient::TurnResponse r)
        {
            chatPanel.setBusy (false);
            for (auto& panel : panels)
                if (panel) panel->setAiBusy (false);

            if (! r.ok)
            {
                chatPanel.addAssistantMessage ("Error: "
                    + (r.error.isNotEmpty() ? r.error : juce::String ("Request failed.")));
                refreshPanels();
                return;
            }

            chatPanel.addAssistantMessage (r.assistantText.isNotEmpty()
                                              ? r.assistantText
                                              : (r.generatedMidi ? "MIDI ready in the preview."
                                                                 : "OK."));

            // Only refresh the roll / auto-preview when MIDI was actually made.
            if (! r.generatedMidi)
            {
                refreshPanels();
                return;
            }

            // Cross-part critic: show the producer what was repaired.
            if (processor.lastCriticSummary().isNotEmpty())
                chatPanel.addAssistantMessage (processor.lastCriticSummary());

            chatPanel.clearMidiAttachment();
            refreshBpmLabel();
            refreshPanels();
            refreshSoundControls();

            if (! previewButton.getToggleState())
            {
                previewButton.setToggleState (true, juce::dontSendNotification);
                previewButton.setButtonText ("Stop");
                processor.togglePreview (true);
            }
            else
            {
                processor.togglePreview (false);
                processor.togglePreview (true);
            }

            midiRoll.setPlayheadBeats (0.0);
            chordDashboard.setPlayheadBeats (0.0);
        });
}

void AIMidiGenEditor::handlePartTransform (InstrumentType type, const juce::String& mode)
{
    chatPanel.setBusy (true);
    for (auto& panel : panels)
        if (panel) panel->setAiBusy (true);

    processor.transformPartWithAI (type, mode,
        [this, type, mode] (AIClient::PatternResponse r)
        {
            chatPanel.setBusy (false);
            for (auto& panel : panels)
                if (panel) panel->setAiBusy (false);

            if (! r.ok)
            {
                chatPanel.addAssistantMessage ("Error: "
                    + (r.error.isNotEmpty() ? r.error : juce::String ("Transform failed.")));
                refreshPanels();
                return;
            }

            chatPanel.addAssistantMessage (
                r.assistantText.isNotEmpty()
                    ? r.assistantText
                    : (juce::String (toString (type)) + " "
                       + (mode.equalsIgnoreCase ("continue") ? "continued." : "varied.")));

            refreshBpmLabel();
            refreshPanels();

            if (! previewButton.getToggleState())
            {
                previewButton.setToggleState (true, juce::dontSendNotification);
                previewButton.setButtonText ("Stop");
                processor.togglePreview (true);
            }
            else
            {
                processor.togglePreview (false);
                processor.togglePreview (true);
            }
        });
}

InstrumentType AIMidiGenEditor::pickPartForMidiAttach() const
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums) continue;
        if (! isTypeInFocus (type)) continue;
        if (! processor.part (type).notes.empty())
            return type;
    }

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums) continue;
        if (! processor.part (type).notes.empty())
            return type;
    }

    return InstrumentType::Chords;
}

void AIMidiGenEditor::exportAllTracks()
{
    auto src = processor.exportAllPartsMidiFile();
    if (! src.existsAsFile())
    {
        chatPanel.addAssistantMessage ("Nothing to export — generate some parts first.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> (
        "Export multi-track MIDI",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile ("AIMidiGen_All.mid"),
        "*.mid");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [this, src, chooser] (const juce::FileChooser& fc)
        {
            auto dst = fc.getResult();
            if (dst == juce::File()) return;
            if (src.copyFileTo (dst))
                chatPanel.addAssistantMessage ("Exported multi-track MIDI → " + dst.getFileName());
            else
                chatPanel.addAssistantMessage ("Export failed.");
        });
}

void AIMidiGenEditor::refreshProviderUi()
{
    suppressProviderCallback = true;
    providerCombo.setSelectedId (
        processor.ai().getProvider() == AIClient::Provider::OpenAI ? 2 : 1,
        juce::dontSendNotification);
    suppressProviderCallback = false;

    suppressModelCallback = true;
    modelCombo.clear (juce::dontSendNotification);
    juce::String selected;

    if (processor.ai().getProvider() == AIClient::Provider::OpenAI)
    {
        modelCombo.addItem ("gpt-4o", 1);
        modelCombo.addItem ("gpt-4.1", 2);
        modelCombo.addItem ("o4-mini", 3);
        selected = processor.ai().getOpenAiModel();
    }
    else
    {
        modelCombo.addItem ("claude-sonnet-5", 1);
        modelCombo.addItem ("claude-opus-4-8", 2);
        modelCombo.addItem ("claude-sonnet-4-6", 3);
        selected = processor.ai().getClaudeModel();
    }

    int selectId = 1;
    for (int i = 1; i <= modelCombo.getNumItems(); ++i)
        if (modelCombo.getItemText (i - 1) == selected)
            selectId = i;
    modelCombo.setSelectedId (selectId, juce::dontSendNotification);
    suppressModelCallback = false;

    // Persist whatever is selected if settings had an unknown older id.
    if (processor.ai().getProvider() == AIClient::Provider::OpenAI)
        processor.ai().setOpenAiModel (modelCombo.getText());
    else
        processor.ai().setClaudeModel (modelCombo.getText());

    apiKeyField.setTextToShowWhenEmpty (processor.ai().apiKeyPlaceholder(),
                                        CustomLookAndFeel::txt2.withAlpha (0.55f));

    if (processor.ai().hasApiKey())
        apiKeyField.setText (processor.ai().apiKeyFromEnvironment()
                                 ? "(loaded from env)"
                                 : "(saved)",
                             false);
    else if (apiKeyField.getText().startsWith ("("))
        apiKeyField.clear();
}

void AIMidiGenEditor::refreshMidiRoll()
{
    std::vector<MidiRollView::NoteDraw> drawNotes;
    const auto focus = focusedLane;

    if (focus == InstrumentType::Drums)
    {
        for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        {
            auto& part = processor.drumPiece ((DrumPiece) dp);
            for (auto& n : part.notes)
                drawNotes.push_back ({ n.startBeats, n.lengthBeats, n.pitch, n.velocity,
                                       (int) InstrumentType::Drums, part.muted });
        }
    }
    else
    {
        auto& part = processor.part (focus);
        for (auto& n : part.notes)
            drawNotes.push_back ({ n.startBeats, n.lengthBeats, n.pitch, n.velocity,
                                   (int) focus, part.muted });
    }

    midiRoll.setFocusLabel (juce::String (toString (focus)) + " · Piano roll");
    midiRoll.setLoopLengthBeats (processor.getLoopLengthBeats());
    midiRoll.setNotes (std::move (drawNotes));
}

void AIMidiGenEditor::refreshLanes()
{
    const double loopBeats = processor.getLoopLengthBeats();
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        auto* lane = lanes[(size_t) t].get();
        if (lane == nullptr) continue;
        const auto type = (InstrumentType) t;
        lane->setSelected (type == focusedLane);

        std::vector<std::pair<double, double>> thumb;
        bool has = false;
        bool locked = false;
        bool muted = false;

        if (type == InstrumentType::Drums)
        {
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
            {
                auto& p = processor.drumPiece ((DrumPiece) dp);
                if (! p.notes.empty()) has = true;
                if (p.locked) locked = true;
                if (p.muted) muted = true;
                for (auto& n : p.notes)
                    thumb.push_back ({ n.startBeats, n.lengthBeats });
            }
        }
        else
        {
            auto& p = processor.part (type);
            has = ! p.notes.empty();
            locked = p.locked;
            muted = p.muted;
            for (auto& n : p.notes)
                thumb.push_back ({ n.startBeats, n.lengthBeats });
        }

        lane->setHasContent (has);
        lane->setLocked (locked);
        lane->setMuted (muted);
        lane->setThumbnailNotes (std::move (thumb), loopBeats);
    }

    generateLaneButton.setButtonText ("Generate " + juce::String (toString (focusedLane)));
    varyLaneButton.setEnabled (focusedLane != InstrumentType::Drums);
}

void AIMidiGenEditor::setFocusedLane (InstrumentType type)
{
    focusedLane = type;
    refreshLanes();
    refreshMidiRoll();
    resized();
    repaint();
}

void AIMidiGenEditor::refreshSoundControls()
{
    suppressGenreCallback = true;
    genreCombo.setSelectedId ((int) processor.getGenreMode() + 1, juce::dontSendNotification);
    suppressGenreCallback = false;

    suppressKeyCallback = true;
    {
        const auto root = juce::String (processor.params().root);
        bool foundRoot = false;
        for (int i = 1; i <= rootCombo.getNumItems(); ++i)
            if (rootCombo.getItemText (i - 1).equalsIgnoreCase (root))
            {
                rootCombo.setSelectedId (i, juce::dontSendNotification);
                foundRoot = true;
                break;
            }
        if (! foundRoot)
            rootCombo.setText (root, juce::dontSendNotification);

        const auto scale = juce::String (processor.params().scale);
        bool foundScale = false;
        for (int i = 1; i <= scaleCombo.getNumItems(); ++i)
            if (scaleCombo.getItemText (i - 1).equalsIgnoreCase (scale))
            {
                scaleCombo.setSelectedId (i, juce::dontSendNotification);
                foundScale = true;
                break;
            }
        if (! foundScale)
            scaleCombo.setText (scale, juce::dontSendNotification);
    }
    suppressKeyCallback = false;

    const auto map = defaultsFor (processor.getGenreMode());
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums) continue;
        if (panels[(size_t) t] == nullptr) continue;
        panels[(size_t) t]->setSoundOptions (map.variants[(size_t) t],
                                             processor.getPartTimbre ((InstrumentType) t));
        panels[(size_t) t]->setVolume (processor.getPartGain ((InstrumentType) t));
    }

    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        drumKitPanel.setPieceVolume ((DrumPiece) dp,
                                     processor.getDrumGain ((DrumPiece) dp));

    // Sound picks: whenever the effective genre changes (combo, chat
    // auto-detect, or AI), tell the producer what patches/kits to load in
    // the DAW — the MIDI-first answer to "default sounds per style".
    {
        const auto genre = juce::String (processor.params().genre);
        if (genre != lastSoundPicksGenre)
        {
            const bool firstRun = lastSoundPicksGenre.isEmpty();
            lastSoundPicksGenre = genre;
            if (! firstRun)
                if (const auto* st = findStyleOrNull (genre.toStdString()))
                    chatPanel.addAssistantMessage ("Sound picks — " + juce::String (st->name)
                                                   + ": " + juce::String (st->sounds));
        }
    }

    refreshSampleControls();
}

void AIMidiGenEditor::refreshSampleControls()
{
    samplesStatusLabel.setText (processor.samples().statusLine(),
                                juce::dontSendNotification);

    // Rebuild Pack menu from what's on disk.
    juce::String selectedPack;
    {
        suppressPackCallback = true;
        const juce::String prevName = packCombo.getNumItems() > 0 ? packCombo.getText() : juce::String();

        packCombo.clear (juce::dontSendNotification);
        packCombo.addItem ("All packs", 1);
        auto packs = processor.samples().packNames();
        int selectId = 1;
        for (int i = 0; i < packs.size(); ++i)
        {
            const int id = i + 2;
            packCombo.addItem (packs[i], id);
            if (prevName.isNotEmpty() && packs[i] == prevName)
                selectId = id;
        }
        if (selectId == 1 && packs.size() == 1)
            selectId = 2; // auto-select the only pack
        packCombo.setSelectedId (selectId, juce::dontSendNotification);

        if (selectId > 1)
        {
            const int idx = packCombo.getSelectedItemIndex();
            if (idx >= 0)
                selectedPack = packCombo.getItemText (idx);
        }
        suppressPackCallback = false;
    }

    auto buildOpts = [this, &selectedPack] (SampleRole prefer) -> std::vector<const SampleEntry*>
    {
        std::vector<const SampleEntry*> pool;
        if (selectedPack.isNotEmpty())
            pool = processor.samples().samplesForPack (selectedPack);
        else
            for (auto& e : processor.samples().all())
                pool.push_back (&e);

        // Optional Type filter — if it would empty the list, ignore it so menus never go blank
        const int filterId = sampleFilterCombo.getSelectedId();
        if (filterId > 1)
        {
            const auto role = (SampleRole) (filterId - 2);
            std::vector<const SampleEntry*> filtered;
            for (auto* e : pool)
                if (e != nullptr && e->role == role)
                    filtered.push_back (e);
            if (! filtered.empty())
                pool.swap (filtered);
        }

        // Preferred role first, then everything else in the pack
        std::vector<const SampleEntry*> opts;
        opts.reserve (pool.size());
        for (auto* e : pool)
            if (e != nullptr && e->role == prefer)
                opts.push_back (e);
        for (auto* e : pool)
            if (e != nullptr && e->role != prefer)
                opts.push_back (e);

        // Absolute fallback: never leave an instrument with an empty menu if library has sounds
        if (opts.empty())
            for (auto& e : processor.samples().all())
                opts.push_back (&e);

        return opts;
    };

    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        const auto piece = (DrumPiece) dp;
        drumKitPanel.setSampleOptions (piece,
                                       buildOpts (roleForDrumPiece (piece)),
                                       processor.getDrumSampleId (piece));
    }

    // Pitched instruments never use drum-kit one-shots
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        // no-op: InstrumentPanel no longer has pack sample pickers
        juce::ignoreUnused (t);
    }

    processor.syncSamplesToSynth();
    refreshMidiLoopControls();
}

void AIMidiGenEditor::refreshMidiLoopControls()
{
    processor.midiLoops().reload();

    juce::String selectedPack;
    {
        suppressMidiPackCallback = true;
        const juce::String prev = midiPackCombo.getNumItems() > 0 ? midiPackCombo.getText() : juce::String();
        midiPackCombo.clear (juce::dontSendNotification);
        midiPackCombo.addItem ("All MIDI packs", 1);
        auto packs = processor.midiLoops().packNames();
        int selectId = 1;
        for (int i = 0; i < packs.size(); ++i)
        {
            const int id = i + 2;
            midiPackCombo.addItem (packs[i], id);
            if (prev.isNotEmpty() && packs[i] == prev)
                selectId = id;
        }
        // Prefer a real pack over "All" when only one exists
        if (packs.size() >= 1 && (selectId == 1 || prev.isEmpty() || prev == "All MIDI packs"
                                  || prev == "No MIDI packs" || prev == "MIDI pack"))
            selectId = 2;
        midiPackCombo.setSelectedId (selectId, juce::dontSendNotification);
        if (selectId > 1)
            selectedPack = midiPackCombo.getItemText (midiPackCombo.getSelectedItemIndex());
        suppressMidiPackCallback = false;
    }

    {
        const juce::String prevKit = midiKitCombo.getText();
        midiKitCombo.clear (juce::dontSendNotification);
        auto kits = processor.midiLoops().kitBundleNames (selectedPack);
        int selectId = 0;
        for (int i = 0; i < kits.size(); ++i)
        {
            midiKitCombo.addItem (kits[i], i + 1);
            if (prevKit == kits[i])
                selectId = i + 1;
        }
        if (selectId == 0 && kits.size() > 0)
            selectId = 1;
        if (selectId > 0)
            midiKitCombo.setSelectedId (selectId, juce::dontSendNotification);
        loadMidiKitButton.setEnabled (kits.size() > 0);
    }

    samplesStatusLabel.setText (
        processor.samples().statusLine() + " · " + processor.midiLoops().statusLine(),
        juce::dontSendNotification);

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums) continue;
        if (panels[(size_t) t] == nullptr) continue;
        panels[(size_t) t]->setMidiLoopOptions (
            processor.midiLoops().loopsForInstrumentInPack (type, selectedPack),
            processor.getPartMidiLoopId (type));
    }
}

bool AIMidiGenEditor::isTypeInFocus (InstrumentType type) const
{
    switch (focusCombo.getSelectedId())
    {
        case 2: return type == InstrumentType::Drums;
        case 3: return type == InstrumentType::Chords || type == InstrumentType::Pad;
        case 4: return type == InstrumentType::Bass || type == InstrumentType::Drums;
        case 5: return type == InstrumentType::Melody
                    || type == InstrumentType::Arp
                    || type == InstrumentType::CounterMelody;
        case 6: return type == InstrumentType::Bass;
        default: return true; // All
    }
}

void AIMidiGenEditor::applyWorkspaceFocus()
{
    // Lanes always visible on Generate; detail panels / drums follow focused lane
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if (lanes[(size_t) t] != nullptr)
            lanes[(size_t) t]->setVisible (true);
        if (panels[(size_t) t] != nullptr)
            panels[(size_t) t]->setVisible (false);
    }

    const bool drums = focusedLane == InstrumentType::Drums;
    drumKitPanel.setVisible (drums && currentSurface == Surface::Generate);
    chordDashboard.setVisible (! drums && currentSurface == Surface::Generate);

    if (! drums && panels[(size_t) focusedLane] != nullptr
        && currentSurface == Surface::Generate)
        panels[(size_t) focusedLane]->setVisible (true);

    generateAllButton.setButtonText ("Generate all");
    generateLaneButton.setButtonText ("Generate " + juce::String (toString (focusedLane)));
}

void AIMidiGenEditor::generateFocusedLane()
{
    if (focusedLane == InstrumentType::Drums)
        processor.generateDrumKit();
    else
        processor.generatePart (focusedLane);
    refreshPanels();
}

void AIMidiGenEditor::generateFocusedParts()
{
    // "Generate all" — still respect chat Focus combo filters when not All
    if (focusCombo.getSelectedId() <= 1)
    {
        processor.generateAllParts();
    }
    else
    {
        processor.pushUndoSnapshot();
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        {
            const auto type = (InstrumentType) t;
            if (! isTypeInFocus (type)) continue;
            if (type == InstrumentType::Drums)
                processor.generateDrumKit (false);
            else
                processor.generatePart (type, false);
        }
    }
    refreshPanels();
}

bool AIMidiGenEditor::keyPressed (const juce::KeyPress& key)
{
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return false;

    if (key == juce::KeyPress::spaceKey)
    {
        const bool on = ! previewButton.getToggleState();
        previewButton.setToggleState (on, juce::dontSendNotification);
        processor.togglePreview (on);
        previewButton.setButtonText (on ? "Stop" : "Preview");
        if (! on)
            midiRoll.setPlayheadBeats (-1.0);
        return true;
    }
    return false;
}

void AIMidiGenEditor::refreshChordDashboard()
{
    chordDashboard.setLoopBars (processor.params().bars);
    const double now = processor.isPreviewing()
                           ? processor.getPreviewPositionBeats()
                           : 0.0;

    const InstrumentType tracked[] = {
        InstrumentType::Chords, InstrumentType::Pad,
        InstrumentType::Bass, InstrumentType::Melody
    };

    for (auto type : tracked)
    {
        auto& part = processor.part (type);
        auto hits = analysePartChords (part, processor.params());
        auto nowName = chordAtBeat (part, processor.params(), now);
        chordDashboard.setPartChords (type, hits, nowName);

        if (type == InstrumentType::Drums) continue;
        if (panels[(size_t) type] == nullptr) continue;

        juce::String summary;
        const int maxShow = 6;
        for (int i = 0; i < (int) hits.size() && i < maxShow; ++i)
        {
            if (i) summary << " · ";
            summary << hits[(size_t) i].name;
        }
        if ((int) hits.size() > maxShow)
            summary << " …";
        if (summary.isEmpty())
            summary = part.notes.empty() ? "—" : juce::String ((int) part.notes.size()) + " notes";
        panels[(size_t) type]->setChordSummary (summary);
    }

    // Remaining pitched panels (arp / counter)
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        if (type == InstrumentType::Drums || panels[(size_t) t] == nullptr) continue;
        if (type == InstrumentType::Chords || type == InstrumentType::Pad
            || type == InstrumentType::Bass || type == InstrumentType::Melody)
            continue;

        auto hits = analysePartChords (processor.part (type), processor.params());
        juce::String summary;
        for (int i = 0; i < (int) hits.size() && i < 4; ++i)
        {
            if (i) summary << " · ";
            summary << hits[(size_t) i].name;
        }
        if (summary.isEmpty())
            summary = processor.part (type).notes.empty()
                          ? "—"
                          : juce::String ((int) processor.part (type).notes.size()) + " notes";
        panels[(size_t) t]->setChordSummary (summary);
    }
}

void AIMidiGenEditor::refreshPanels()
{
    readyParts = 0;
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

    apiStatusLabel.setText (processor.ai().hasApiKey()
                                ? (processor.ai().providerDisplayName() + " · "
                                   + (processor.ai().getProvider() == AIClient::Provider::OpenAI
                                          ? processor.ai().getOpenAiModel()
                                          : processor.ai().getClaudeModel()))
                                : ("Add " + processor.ai().providerDisplayName() + " key"),
                            juce::dontSendNotification);

    chatPanel.setModelLabel (processor.ai().getProvider() == AIClient::Provider::OpenAI
                                 ? processor.ai().getOpenAiModel()
                                 : processor.ai().getClaudeModel());
    refreshBpmLabel();
    refreshSoundControls();
    refreshChordDashboard();
    undoButton.setEnabled (processor.canUndo());

    refreshLanes();
    refreshMidiRoll();
    applyWorkspaceFocus();
    repaint();
}

void AIMidiGenEditor::paint (juce::Graphics& g)
{
    CustomLookAndFeel::fillBackdrop (g, getLocalBounds());

    auto r = getLocalBounds();

    // Header — companion app-header
    auto headerBar = r.removeFromTop (58);
    g.setColour (CustomLookAndFeel::bg1.withAlpha (0.97f));
    g.fillRect (headerBar);
    g.setColour (CustomLookAndFeel::line);
    g.drawHorizontalLine (headerBar.getBottom() - 1, 0.0f, (float) getWidth());

    if (! tabTrayBounds.isEmpty())
        CustomLookAndFeel::drawSegmentedTray (g, tabTrayBounds);

    if (! statusBadgeBounds.isEmpty())
        CustomLookAndFeel::drawBadge (g, statusBadgeBounds,
                                     processor.ai().hasApiKey() ? "connected" : "offline",
                                     processor.ai().hasApiKey());

    // Footer — compact transport dock
    auto footerBar = r.removeFromBottom (52);
    g.setColour (CustomLookAndFeel::bg1.withAlpha (0.97f));
    g.fillRect (footerBar);
    g.setColour (CustomLookAndFeel::line);
    g.drawHorizontalLine (footerBar.getY(), 0.0f, (float) getWidth());
}

void AIMidiGenEditor::resized()
{
    auto r = getLocalBounds();

    // ---- Header: brand · TabsList · badge ----
    auto header = r.removeFromTop (58).reduced (16, 11);
    headerLabel.setBounds (header.removeFromLeft (108));

    statusBadgeBounds = header.removeFromRight (108).withSizeKeepingCentre (108, 28);
    header.removeFromRight (12);
    apiStatusLabel.setBounds ({});

    // Center the tab tray like the companion nav
    const int trayW = 348;
    tabTrayBounds = juce::Rectangle<int> (header.getCentreX() - trayW / 2,
                                          header.getY(),
                                          trayW,
                                          header.getHeight()).reduced (0, 1);
    auto tabs = tabTrayBounds.reduced (3, 3);
    const int tw = tabs.getWidth() / 4;
    generateTabButton.setBounds (tabs.removeFromLeft (tw));
    browseTabButton.setBounds (tabs.removeFromLeft (tw));
    chatTabButton.setBounds (tabs.removeFromLeft (tw));
    settingsTabButton.setBounds (tabs);

    // ---- Footer ----
    auto footer = r.removeFromBottom (52).reduced (16, 11);
    keyLabel.setBounds (footer.removeFromLeft (28));
    rootCombo.setBounds (footer.removeFromLeft (52).reduced (0, 1));
    footer.removeFromLeft (4);
    scaleCombo.setBounds (footer.removeFromLeft (108).reduced (0, 1));
    footer.removeFromLeft (16);

    auto bpmArea = footer.removeFromLeft (132);
    bpmMinus.setBounds (bpmArea.removeFromLeft (28));
    bpmArea.removeFromLeft (2);
    bpmPlus.setBounds (bpmArea.removeFromRight (28));
    bpmArea.removeFromRight (2);
    auto valueCol = bpmArea;
    bpmValue.setBounds (valueCol.removeFromTop (14));
    bpmLabel.setBounds (valueCol);

    exportAllButton.setBounds (footer.removeFromRight (96).withSizeKeepingCentre (96, 30));
    footer.removeFromRight (8);
    previewButton.setBounds (footer.removeFromRight (96).withSizeKeepingCentre (96, 30));
    previewButton.setComponentID ("primary");
    footer.removeFromRight (10);
    hostMidiButton.setBounds (footer.removeFromRight (78).withSizeKeepingCentre (78, 28));
    footer.removeFromRight (6);
    hostSyncButton.setBounds (footer.removeFromRight (88).withSizeKeepingCentre (88, 28));

    generateSurface.setBounds (r);
    browseSurface.setBounds (r);
    chatSurface.setBounds (r);
    settingsSurface.setBounds (r);

    layoutGenerateSurface();
    layoutBrowseSurface();
    layoutChatSurface();
    layoutSettingsSurface();
}

void AIMidiGenEditor::layoutGenerateSurface()
{
    auto r = generateSurface.getLocalBounds().reduced (16, 14);

    // Right rail: Generate lane / all / vary / undo / genre
    auto rail = r.removeFromRight (200);
    r.removeFromRight (14);
    {
        auto genreRow = rail.removeFromTop (32);
        soundLabel.setBounds (genreRow.removeFromLeft (44));
        genreCombo.setBounds (genreRow.reduced (0, 2));
        rail.removeFromTop (12);
        generateLaneButton.setBounds (rail.removeFromTop (40));
        rail.removeFromTop (8);
        generateAllButton.setBounds (rail.removeFromTop (34));
        rail.removeFromTop (8);
        varyLaneButton.setBounds (rail.removeFromTop (32));
        rail.removeFromTop (8);
        undoButton.setBounds (rail.removeFromTop (32));
        rail.removeFromTop (8);
        dnaButton.setBounds (rail.removeFromTop (32));
    }

    // Left: slim instrument lanes
    const int laneH = InstrumentLane::kHeight;
    const int laneGap = 6;
    const int nLanes = (int) InstrumentType::NumTypes;
    const int lanesH = nLanes * laneH + (nLanes - 1) * laneGap;
    auto left = r.removeFromLeft (juce::jlimit (240, 320, r.getWidth() * 26 / 100));
    r.removeFromLeft (14);

    int y = left.getY();
    for (int i = 0; i < nLanes; ++i)
    {
        if (lanes[(size_t) i] == nullptr) continue;
        lanes[(size_t) i]->setBounds (left.getX(), y, left.getWidth(), laneH);
        y += laneH + laneGap;
    }

    // Detail strip under lanes (focused pitched instrument controls)
    if (focusedLane != InstrumentType::Drums
        && panels[(size_t) focusedLane] != nullptr
        && panels[(size_t) focusedLane]->isVisible())
    {
        const int detailTop = left.getY() + lanesH + 10;
        const int detailH = juce::jmax (0, left.getBottom() - detailTop);
        if (detailH > 40)
            panels[(size_t) focusedLane]->setBounds (left.getX(), detailTop,
                                                     left.getWidth(), detailH);
    }

    // Center: focused piano roll + chord dash or drum kit
    auto center = r;
    const bool drums = focusedLane == InstrumentType::Drums;
    if (drums)
    {
        auto rollArea = center.removeFromTop (juce::jmax (180, center.getHeight() * 42 / 100));
        midiRoll.setBounds (rollArea);
        center.removeFromTop (10);
        drumKitPanel.setBounds (center);
        chordDashboard.setBounds ({});
    }
    else
    {
        auto rollArea = center.removeFromTop (juce::jmax (240, center.getHeight() * 62 / 100));
        midiRoll.setBounds (rollArea);
        center.removeFromTop (10);
        chordDashboard.setBounds (center);
        drumKitPanel.setBounds ({});
    }
}

void AIMidiGenEditor::layoutBrowseSurface()
{
    auto r = browseSurface.getLocalBounds().reduced (16, 12);
    const int gap = 8;

    auto row1 = r.removeFromTop (30);
    midiPackLabel.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    midiPackCombo.setBounds (row1.removeFromLeft (juce::jmin (260, row1.getWidth())).reduced (0, 1));
    r.removeFromTop (gap);

    auto row2 = r.removeFromTop (30);
    row2.removeFromLeft (76);
    midiKitCombo.setBounds (row2.removeFromLeft (120).reduced (0, 1));
    row2.removeFromLeft (6);
    loadMidiKitButton.setBounds (row2.removeFromLeft (80).withSizeKeepingCentre (80, 28));
    row2.removeFromLeft (6);
    addMidiButton.setBounds (row2.removeFromLeft (80).withSizeKeepingCentre (80, 28));
    r.removeFromTop (gap);

    auto row3 = r.removeFromTop (30);
    packLabel.setText ("Sounds", juce::dontSendNotification);
    packLabel.setBounds (row3.removeFromLeft (70));
    row3.removeFromLeft (6);
    packCombo.setBounds (row3.removeFromLeft (200).reduced (0, 1));
    row3.removeFromLeft (6);
    addSoundsButton.setBounds (row3.removeFromLeft (94).withSizeKeepingCentre (94, 28));
    row3.removeFromLeft (6);
    soundsFolderButton.setBounds (row3.removeFromLeft (64).withSizeKeepingCentre (64, 28));
    r.removeFromTop (gap);

    auto row4 = r.removeFromTop (30);
    sampleFilterLabel.setBounds (row4.removeFromLeft (70));
    row4.removeFromLeft (6);
    sampleFilterCombo.setBounds (row4.removeFromLeft (140).reduced (0, 1));
    r.removeFromTop (gap);

    auto row5 = r.removeFromTop (28);
    samplesStatusLabel.setBounds (row5);
}

void AIMidiGenEditor::layoutChatSurface()
{
    auto r = chatSurface.getLocalBounds().reduced (12, 10);

    auto row1 = r.removeFromTop (30);
    focusLabel.setBounds (row1.removeFromLeft (50));
    row1.removeFromLeft (6);
    focusCombo.setBounds (row1.removeFromLeft (180).reduced (0, 1));
    r.removeFromTop (8);

    chatPanel.setBounds (r);
}

void AIMidiGenEditor::layoutSettingsSurface()
{
    auto r = settingsSurface.getLocalBounds().reduced (16, 12);
    const int gap = 8;

    auto row1 = r.removeFromTop (30);
    providerLabel.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    providerCombo.setBounds (row1.removeFromLeft (140).reduced (0, 1));
    r.removeFromTop (gap);

    auto row2 = r.removeFromTop (30);
    modelLabel.setBounds (row2.removeFromLeft (70));
    row2.removeFromLeft (6);
    modelCombo.setBounds (row2.removeFromLeft (200).reduced (0, 1));
    r.removeFromTop (gap);

    auto row3 = r.removeFromTop (30);
    apiKeyLabel.setBounds (row3.removeFromLeft (70));
    row3.removeFromLeft (6);
    apiKeyField.setBounds (row3.removeFromLeft (260));
}

} // namespace aimidi
