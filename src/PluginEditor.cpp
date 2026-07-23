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

    headerLabel.setText ("ComposerAI", juce::dontSendNotification);
    headerLabel.setVisible (false); // painted as brand lockup
    addChildComponent (headerLabel);

    apiStatusLabel.setVisible (false);

    optionsButton.setComponentID ("ghost");
    optionsButton.setTooltip ("Open Settings");
    optionsButton.onClick = [this] { setSurface (Surface::Settings); };
    addAndMakeVisible (optionsButton);

    lastRunTitle.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    lastRunTitle.setColour (juce::Label::textColourId, CustomLookAndFeel::txt3);
    lastRunTitle.setInterceptsMouseClicks (false, false);
    generateSurface.addAndMakeVisible (lastRunTitle);

    lastRunMeta.setFont (CustomLookAndFeel::font (11.5f));
    lastRunMeta.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    lastRunMeta.setJustificationType (juce::Justification::topLeft);
    lastRunMeta.setInterceptsMouseClicks (false, false);
    generateSurface.addAndMakeVisible (lastRunMeta);

    // Transient failure banner — generation errors are otherwise only visible
    // in the Chat surface. Auto-hides after ~6s (timerCallback).
    generateErrorLabel.setFont (CustomLookAndFeel::font (11.5f, juce::Font::bold));
    generateErrorLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::danger);
    generateErrorLabel.setColour (juce::Label::backgroundColourId,
                                  CustomLookAndFeel::bg0.withAlpha (0.92f));
    generateErrorLabel.setJustificationType (juce::Justification::centred);
    generateErrorLabel.setInterceptsMouseClicks (false, false);
    generateSurface.addChildComponent (generateErrorLabel);

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

    generateAllButton.setComponentID ("ghost");
    generateAllButton.setTooltip ("Generate all unlocked parts");
    generateAllButton.onClick = [this] { generateFocusedParts(); };
    generateSurface.addAndMakeVisible (generateAllButton);

    newIdeaButton.setComponentID ("ghost");
    newIdeaButton.setTooltip ("Roll a completely fresh idea: new seed, regenerate every "
                              "unlocked lane via Claude+Brain when connected (or local offline).");
    newIdeaButton.onClick = [this] { runNewIdea(); };
    generateSurface.addAndMakeVisible (newIdeaButton);

    generateLaneButton.setComponentID ("hero"); // only glowing primary per v4
    generateLaneButton.setTooltip ("Generate the focused instrument lane (Claude+Brain when connected)");
    generateLaneButton.onClick = [this] { generateFocusedLane(); };
    generateSurface.addAndMakeVisible (generateLaneButton);

    varyLaneButton.setComponentID ("ghost");
    varyLaneButton.setTooltip ("AI variation of the focused lane (chords preserve key/genre/BPM/bars)");
    varyLaneButton.onClick = [this]
    {
        if (focusedLane == InstrumentType::Drums)
            chatPanel.addAssistantMessage ("Vary drums from the drum kit panel, or chat \"vary the drums\".");
        else if (focusedLane == InstrumentType::Chords)
        {
            chatPanel.setBusy (true);
            juce::Component::SafePointer<AIMidiGenEditor> safe (this);
            processor.varyChordsWithAI ([safe] (AIClient::PatternResponse r)
            {
                if (safe == nullptr) return;
                safe->chatPanel.setBusy (false);
                if (! r.ok)
                {
                    safe->reportGeneration (safe->processor.lastGenerationReport(), true);
                    return;
                }
                safe->chatPanel.addAssistantMessage (
                    r.assistantText.isNotEmpty() ? r.assistantText
                                                 : juce::String ("Generated with Claude — chords varied."));
                safe->refreshLastRunMeta();
                safe->refreshPanels();
            });
        }
        else
            handlePartTransform (focusedLane, "vary");
    };
    generateSurface.addAndMakeVisible (varyLaneButton);

    useLocalButton.setComponentID ("ghost");
    useLocalButton.setTooltip ("After a Claude failure, run the local SongPlan generator instead");
    useLocalButton.setEnabled (false);
    useLocalButton.onClick = [this]
    {
        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        processor.useLocalGeneratorNow ([safe] (GenerationReport report)
        {
            if (safe == nullptr) return;
            safe->reportGeneration (report, false);
            safe->refreshPanels();
        });
    };
    generateSurface.addAndMakeVisible (useLocalButton);

    offlineModeButton.setComponentID ("ghost");
    offlineModeButton.setClickingTogglesState (true);
    offlineModeButton.setTooltip ("Force the local SongPlan engine (skip Claude even if a key is set)");
    offlineModeButton.setToggleState (processor.prefersOfflineGeneration(),
                                      juce::dontSendNotification);
    offlineModeButton.onClick = [this]
    {
        processor.setPreferOfflineGeneration (offlineModeButton.getToggleState());
        chatPanel.addAssistantMessage (offlineModeButton.getToggleState()
            ? "Offline mode on — Generate / New Idea use the local engine only."
            : "Offline mode off — Generate / New Idea use Claude + Brain when a key is set.");
        refreshProviderUi();
    };
    generateSurface.addAndMakeVisible (offlineModeButton);

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

    undoButton.setComponentID ("ghost");
    bpmMinus.setComponentID ("ghost");
    bpmPlus.setComponentID ("ghost");
    undoButton.setTooltip ("Undo last generation");
    undoButton.onClick = [this]
    {
        if (processor.undoLastGeneration())
        {
            // Re-seed editor mute state from the restored snapshot so the next
            // syncEffectiveMutes() doesn't stamp stale mutes over the undo.
            laneSolo.fill (false);
            for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
                baseMute[(size_t) t] = processor.part ((InstrumentType) t).muted;
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
                pieceBaseMute[(size_t) dp] = processor.drumPiece ((DrumPiece) dp).muted;
            syncEffectiveMutes();
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

        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                juce::String err;
                const int n = safe->processor.samples().importPackFolder (dir, &err);
                if (n > 0)
                {
                    const int assigned = safe->processor.autoAssignSamplesFromLibrary (dir.getFileName());
                    safe->processor.rememberDrumKitInBrain (dir.getFileName());
                    safe->chatPanel.setDocsStatus (safe->processor.ai().knowledge().statusLine());
                    safe->chatPanel.addAssistantMessage (
                        "Imported drum kit \"" + dir.getFileName() + "\" (" + juce::String (n)
                        + " files). Wired " + juce::String (assigned)
                        + " drum pieces for preview. Kit memory saved to brain for Claude.");
                }
                else
                {
                    safe->chatPanel.addAssistantMessage (
                        "Could not import pack: " + (err.isNotEmpty() ? err : juce::String ("unknown error")));
                }
                safe->refreshSampleControls();
                safe->samplesStatusLabel.setText (safe->processor.samples().statusLine(),
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

        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                juce::String err;
                const int n = safe->processor.midiLoops().importPackFolder (dir, &err);
                if (n > 0)
                {
                    safe->processor.rememberMidiPackInBrain (dir.getFileName());
                    safe->chatPanel.setDocsStatus (safe->processor.ai().knowledge().statusLine());
                    safe->chatPanel.addAssistantMessage (
                        "Imported " + juce::String (n) + " MIDI loops from \""
                        + dir.getFileName()
                        + "\". Pick a Kit in the MIDI kit menu and hit Load kit, "
                          "or choose loops on each instrument’s MIDI menu.");
                }
                else
                {
                    safe->chatPanel.addAssistantMessage (
                        "Could not import MIDI pack: "
                        + (err.isNotEmpty() ? err : juce::String ("no .mid files found")));
                }
                safe->refreshMidiLoopControls();
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
    previewButton.setTooltip ("Audition the loop with the built-in preview synth (with sidechain pump)");
    previewButton.setClickingTogglesState (true);
    previewButton.onClick = [this]
    {
        const bool on = previewButton.getToggleState();
        processor.togglePreview (on);
        previewButton.setButtonText (on ? "Stop" : "Preview");
        if (! on)
            midiRoll.setPlayheadBeats (-1.0);
    };
    // Editor may reopen while the processor is already previewing — reflect it.
    {
        const bool previewing = processor.isPreviewing();
        previewButton.setToggleState (previewing, juce::dontSendNotification);
        previewButton.setButtonText (previewing ? "Stop" : "Preview");
    }
    addAndMakeVisible (previewButton);

    // Reference MIDI becomes a persistent abstract style profile in the Brain.
    // Analysis only — the loop's notes are never copied or retained.
    dnaButton.setComponentID ("ghost");
    dnaButton.setTooltip ("Study a MIDI reference and save its abstract style to the Brain "
                          "(source notes and riffs are never copied)");
    dnaButton.onClick = [this]
    {
        dnaChooser = std::make_unique<juce::FileChooser> (
            "Add reference MIDI to the Brain", juce::File{}, "*.mid;*.midi");
        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        dnaChooser->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto f = fc.getResult();
                if (f == juce::File{}) return;
                safe->chatPanel.addAssistantMessage (safe->processor.loadDna (f));
                safe->chatPanel.setDocsStatus (safe->processor.ai().knowledge().statusLine());
                safe->chatPanel.addAssistantMessage (safe->processor.lastCriticSummary());
                safe->refreshPanels();
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

        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
            [safe, chooser] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto results = fc.getResults();
                int imported = 0;
                juce::String lastError;
                for (auto& f : results)
                {
                    juce::String err;
                    if (safe->processor.ai().knowledge().importFile (f, &err))
                        ++imported;
                    else if (err.isNotEmpty())
                        lastError = err;
                }

                safe->chatPanel.setDocsStatus (safe->processor.ai().knowledge().statusLine());
                if (imported > 0)
                    safe->chatPanel.addAssistantMessage (
                        "Added " + juce::String (imported)
                        + " theory doc" + (imported == 1 ? "" : "s")
                        + ". Claude will reference them on the next generate.");
                else if (lastError.isNotEmpty())
                    safe->chatPanel.addAssistantMessage ("Could not import doc: " + lastError);
            });
    };
    chatPanel.onShowDocsFolder = [this]
    {
        processor.ai().knowledge().folder().revealToUser();
    };
    chatPanel.setDocsStatus (processor.ai().knowledge().statusLine());
    chatSurface.addAndMakeVisible (chatPanel);

    // First-open quick-start — so a new producer knows the whole workflow
    // without reading anything else.
    chatPanel.addAssistantMessage (
        "Welcome! Quick start:\n"
        "1. Pick a Genre and hit \"New idea\" (or \"Generate all\") — every lane "
        "stays in key and in sync with the shared chord plan.\n"
        "2. Lock any lane you like, reroll the rest.\n"
        "3. Drag a lane (or \"Export all\") straight into your DAW's piano roll.\n"
        "4. \"MIDI DNA\" learns the groove + key of any .mid you load.\n"
        "5. Chat commands are instant — \"make a bassline\", \"new kick\", "
        "\"128 bpm\", \"in F minor\", \"darker\", \"undo\" all run locally with "
        "zero wait. Type \"help\" for the full list.\n"
        "6. Anything else is real AI conversation, grounded in your actual "
        "project (style, key, lanes, critic notes).");

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
            baseMute[(size_t) type] = muted;
            syncEffectiveMutes();
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

        generateSurface.addChildComponent (*panel); // legacy detail — hidden; TrackDetailPanel is v4
        panels[(size_t) t] = std::move (panel);
    }

    trackDetail.onGenerate = [this]
    {
        // Same path as the hero button — Claude+Brain when connected.
        generateFocusedLane();
    };
    trackDetail.onExport = [this]
    {
        auto src = trackDetail.requestMidiFile ? trackDetail.requestMidiFile() : juce::File();
        if (! src.existsAsFile())
        {
            chatPanel.addAssistantMessage ("Nothing to export — generate this track first.");
            return;
        }
        auto chooser = std::make_shared<juce::FileChooser> (
            "Export MIDI",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile (juce::String (toString (focusedLane)) + ".mid"),
            "*.mid");
        juce::Component::SafePointer<AIMidiGenEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [safe, src, chooser] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto dst = fc.getResult();
                if (dst == juce::File()) return;
                if (src.copyFileTo (dst))
                    safe->chatPanel.addAssistantMessage ("Exported MIDI → " + dst.getFileName());
                else
                    safe->chatPanel.addAssistantMessage ("Export failed.");
            });
    };
    trackDetail.onVary = [this]
    {
        if (focusedLane == InstrumentType::Drums)
            chatPanel.addAssistantMessage ("Vary drums from the drum kit panel, or chat \"vary the drums\".");
        else
            handlePartTransform (focusedLane, "vary");
    };
    trackDetail.onContinue = [this]
    {
        if (focusedLane != InstrumentType::Drums)
            handlePartTransform (focusedLane, "continue");
    };
    trackDetail.onLockChanged = [this] (bool locked)
    {
        if (focusedLane == InstrumentType::Drums)
        {
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
                processor.drumPiece ((DrumPiece) dp).locked = locked;
        }
        else
            processor.part (focusedLane).locked = locked;
    };
    trackDetail.onMuteChanged = [this] (bool muted)
    {
        baseMute[(size_t) focusedLane] = muted;
        syncEffectiveMutes();
        refreshLanes();
        refreshMidiRoll();
    };
    trackDetail.onVolumeChanged = [this] (float g)
    {
        if (focusedLane != InstrumentType::Drums)
            processor.setPartGain (focusedLane, g);
    };
    trackDetail.onTimbreChanged = [this] (int selectedId)
    {
        if (focusedLane == InstrumentType::Drums || selectedId < 1) return;
        const auto map = defaultsFor (processor.getGenreMode());
        const auto& opts = map.variants[(size_t) focusedLane];
        if (selectedId - 1 < (int) opts.size())
            processor.setPartTimbre (focusedLane, opts[(size_t) (selectedId - 1)]);
    };
    trackDetail.requestMidiFile = [this] () -> juce::File
    {
        if (focusedLane == InstrumentType::Drums)
            return MidiGenerator::writeTempMidiFile (processor.part (InstrumentType::Drums),
                                                     processor.params(), "Drums");
        return MidiGenerator::writeTempMidiFile (processor.part (focusedLane),
                                                 processor.params(),
                                                 juce::String (toString (focusedLane)));
    };
    generateSurface.addAndMakeVisible (trackDetail);

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto type = (InstrumentType) t;
        auto lane = std::make_unique<InstrumentLane> (type);
        lane->onSelect = [this, type] { setFocusedLane (type); };
        lane->onGenerate = [this, type]
        {
            setFocusedLane (type);
            // Same path as the hero button — Claude+Brain when connected.
            generateFocusedLane();
        };
        lane->onRandomize = [this, type]
        {
            setFocusedLane (type);
            if (type == InstrumentType::Drums)
                processor.generateDrumKit();
            else
                handlePartTransform (type, "vary");
            refreshPanels();
        };
        lane->onMuteChanged = [this, type] (bool muted)
        {
            baseMute[(size_t) type] = muted;
            syncEffectiveMutes();
            refreshTrackDetail();
            refreshMidiRoll();
        };
        lane->onSoloChanged = [this, type] (bool soloed)
        {
            laneSolo[(size_t) type] = soloed;
            syncEffectiveMutes();
            refreshMidiRoll();
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
        pieceBaseMute[(size_t) dp] = muted;
        syncEffectiveMutes(); // combines piece mute with lane mute/solo
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

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums)
            baseMute[(size_t) t] = processor.part (InstrumentType::Drums).muted;
        else
            baseMute[(size_t) t] = processor.part ((InstrumentType) t).muted;
    }
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        pieceBaseMute[(size_t) dp] = processor.drumPiece ((DrumPiece) dp).muted;

    refreshPanels();
    setFocusedLane (InstrumentType::Melody);
    refreshLastRunMeta();
    setWantsKeyboardFocus (true);
    startTimerHz (30); // playhead tracking + generating shimmer
    setResizable (true, true);
    setResizeLimits (980, 780, 2000, 1400);
    setSize (1440, 940); // VST v4 frame
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
    clearGeneratingUi();
    // Solo is an editor-only overlay — drop it and restore base mutes so a
    // reopened editor seeds cleanly from the processor's .muted flags.
    laneSolo.fill (false);
    syncEffectiveMutes();
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
    bpmLabel.setText (hostSync ? "BPM · HOST" : "BPM", juce::dontSendNotification);
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

    if (generatingUi)
        midiRoll.repaint(); // shimmer animation

    if (generateErrorLabel.isVisible()
        && juce::Time::getMillisecondCounter() >= generateErrorHideAtMs)
        generateErrorLabel.setVisible (false);

    // Connected badge breathe glow (2.6s) — only the status chip, not full UI
    if (! statusBadgeBounds.isEmpty() && processor.ai().hasApiKey())
        repaint (statusBadgeBounds.expanded (6));
}

void AIMidiGenEditor::handlePrompt (const juce::String& prompt)
{
    chatPanel.setBusy (true);
    for (auto& panel : panels)
        if (panel) panel->setAiBusy (true);

    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    processor.handleChatTurn (prompt,
        [safe] (AIClient::TurnResponse r)
        {
            if (safe == nullptr) return;

            safe->chatPanel.setBusy (false);
            for (auto& panel : safe->panels)
                if (panel) panel->setAiBusy (false);

            if (! r.ok)
            {
                safe->chatPanel.addAssistantMessage ("Error: "
                    + (r.error.isNotEmpty() ? r.error : juce::String ("Request failed.")));
                safe->refreshPanels();
                return;
            }

            safe->chatPanel.addAssistantMessage (r.assistantText.isNotEmpty()
                                              ? r.assistantText
                                              : (r.generatedMidi ? "MIDI ready in the preview."
                                                                 : "OK."));

            // Only refresh the roll / auto-preview when MIDI was actually made.
            if (! r.generatedMidi)
            {
                safe->refreshPanels();
                return;
            }

            // Cross-part critic: show the producer what was repaired.
            if (safe->processor.lastCriticSummary().isNotEmpty())
                safe->chatPanel.addAssistantMessage (safe->processor.lastCriticSummary());

            safe->chatPanel.clearMidiAttachment();
            safe->refreshBpmLabel();
            safe->refreshPanels();
            safe->refreshSoundControls();

            if (! safe->previewButton.getToggleState())
            {
                safe->previewButton.setToggleState (true, juce::dontSendNotification);
                safe->previewButton.setButtonText ("Stop");
                safe->processor.togglePreview (true);
            }
            else
            {
                safe->processor.togglePreview (false);
                safe->processor.togglePreview (true);
            }

            safe->midiRoll.setPlayheadBeats (0.0);
            safe->chordDashboard.setPlayheadBeats (0.0);
        });
}

void AIMidiGenEditor::handlePartTransform (InstrumentType type, const juce::String& mode)
{
    chatPanel.setBusy (true);
    for (auto& panel : panels)
        if (panel) panel->setAiBusy (true);

    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    processor.transformPartWithAI (type, mode,
        [safe, type, mode] (AIClient::PatternResponse r)
        {
            if (safe == nullptr) return;

            safe->chatPanel.setBusy (false);
            for (auto& panel : safe->panels)
                if (panel) panel->setAiBusy (false);

            if (! r.ok)
            {
                safe->chatPanel.addAssistantMessage ("Error: "
                    + (r.error.isNotEmpty() ? r.error : juce::String ("Transform failed."))
                    + "\nMIDI was not replaced with a silent local result."
                    + (safe->processor.hasPendingLocalOffer()
                           ? " Click “Use local generator” for the offline engine."
                           : juce::String()));
                safe->useLocalButton.setEnabled (safe->processor.hasPendingLocalOffer());
                safe->refreshPanels();
                // Don't leave the previous run's success line up — that reads
                // as if this vary/continue worked.
                safe->lastRunMeta.setText (
                    juce::String (toString (type)) + " · " + mode
                        + " failed — existing MIDI kept",
                    juce::dontSendNotification);
                safe->showGenerateError (
                    juce::String (toString (type)) + " " + mode + " failed: "
                    + (r.error.isNotEmpty() ? r.error : juce::String ("request failed")));
                return;
            }

            // Success — retire any stale "Use local generator" offer.
            safe->useLocalButton.setEnabled (safe->processor.hasPendingLocalOffer());

            safe->chatPanel.addAssistantMessage (
                juce::String ("Generated with Claude — ")
                + (r.assistantText.isNotEmpty()
                       ? r.assistantText
                       : (juce::String (toString (type)) + " "
                          + (mode.equalsIgnoreCase ("continue") ? "continued." : "varied."))));

            safe->refreshBpmLabel();
            safe->refreshPanels();

            if (! safe->previewButton.getToggleState())
            {
                safe->previewButton.setToggleState (true, juce::dontSendNotification);
                safe->previewButton.setButtonText ("Stop");
                safe->processor.togglePreview (true);
            }
            else
            {
                safe->processor.togglePreview (false);
                safe->processor.togglePreview (true);
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

    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [safe, src, chooser] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto dst = fc.getResult();
            if (dst == juce::File()) return;
            if (src.copyFileTo (dst))
                safe->chatPanel.addAssistantMessage ("Exported multi-track MIDI → " + dst.getFileName());
            else
                safe->chatPanel.addAssistantMessage ("Export failed.");
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
        modelCombo.addItem ("claude-sonnet-4-5", 1);
        modelCombo.addItem ("claude-opus-4-1", 2);
        modelCombo.addItem ("claude-haiku-4-5", 3);
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

        if (type == InstrumentType::Drums)
        {
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
            {
                auto& p = processor.drumPiece ((DrumPiece) dp);
                if (! p.notes.empty()) has = true;
                for (auto& n : p.notes)
                    thumb.push_back ({ n.startBeats, n.lengthBeats });
            }
        }
        else
        {
            auto& p = processor.part (type);
            has = ! p.notes.empty();
            for (auto& n : p.notes)
                thumb.push_back ({ n.startBeats, n.lengthBeats });
        }

        lane->setHasContent (has);
        lane->setMuted (baseMute[(size_t) t]);
        lane->setSoloed (laneSolo[(size_t) t]);
        lane->setThumbnailNotes (std::move (thumb), loopBeats);
    }

    const auto name = juce::String (toString (focusedLane));
    generateLaneButton.setButtonText (generatingUi ? "Composing…" : ("Generate " + name));
    varyLaneButton.setEnabled (focusedLane != InstrumentType::Drums);
    refreshTrackDetail();
}

void AIMidiGenEditor::setFocusedLane (InstrumentType type)
{
    focusedLane = type;
    refreshLanes();
    refreshMidiRoll();
    applyWorkspaceFocus();

    // Defer full layout off the mouse/click stack — avoids JUCE re-entrancy aborts
    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr) return;
        safe->layoutGenerateSurface();
        safe->repaint();
    });
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
    trackDetail.setVisible (currentSurface == Surface::Generate);

    generateAllButton.setButtonText ("Generate all");
    const auto name = juce::String (toString (focusedLane));
    generateLaneButton.setButtonText (generatingUi ? "Composing…" : ("Generate " + name));
}

void AIMidiGenEditor::reportGeneration (const GenerationReport& report, bool offerLocal)
{
    useLocalButton.setEnabled (offerLocal && processor.hasPendingLocalOffer());

    if (report.mode == GenerationMode::FailedClaude)
    {
        juce::String msg = "Claude failed";
        if (! report.detail.empty())
            msg << ": " << report.detail;
        msg << "\nMIDI was not replaced with a silent local result.";
        if (offerLocal && processor.hasPendingLocalOffer())
            msg << " Click “Use local generator” if you want the offline engine.";
        chatPanel.addAssistantMessage (msg);
        showGenerateError ("Claude failed"
                           + (report.detail.empty() ? juce::String()
                                                    : ": " + juce::String (report.detail)));
        refreshLastRunMeta();
        return;
    }

    juce::String msg = juce::String (report.statusLine);
    if (report.mode == GenerationMode::ClaudeBrain)
        msg = "Generated with Claude";
    else if (report.mode == GenerationMode::OfflineLocal)
    {
        if (msg.isEmpty() || ! msg.containsIgnoreCase ("local"))
            msg = processor.ai().hasApiKey() && processor.prefersOfflineGeneration()
                ? "Generated locally — offline mode"
                : (processor.ai().hasApiKey()
                       ? "Generated locally — offline fallback chosen"
                       : "Generated locally — no API key");
    }

    msg << " · seed " << (juce::int64) processor.params().seed;
    if (! report.fingerprint.empty())
        msg << " · harmony " << report.fingerprint;
    chatPanel.addAssistantMessage (msg);

    if (processor.lastCriticSummary().isNotEmpty())
        chatPanel.addAssistantMessage (processor.lastCriticSummary());

    refreshLastRunMeta();
}

void AIMidiGenEditor::runNewIdea()
{
    beginGeneratingUi(); // async: callback clears
    chatPanel.setBusy (true);
    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    processor.newIdeaPreferred ([safe] (GenerationReport report)
    {
        if (safe == nullptr) return;
        safe->chatPanel.setBusy (false);
        safe->clearGeneratingUi();
        safe->reportGeneration (report, report.mode == GenerationMode::FailedClaude);
        safe->refreshPanels();
    });
}

void AIMidiGenEditor::generateFocusedLane()
{
    beginGeneratingUi(); // async: callback clears
    chatPanel.setBusy (true);
    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    processor.generatePreferredLane (focusedLane, [safe] (GenerationReport report)
    {
        if (safe == nullptr) return;
        safe->chatPanel.setBusy (false);
        safe->clearGeneratingUi();
        safe->reportGeneration (report, report.mode == GenerationMode::FailedClaude);
        safe->refreshPanels();
    });
}

void AIMidiGenEditor::generateFocusedParts()
{
    beginGeneratingUi(); // both branches clear explicitly
    // Focused "Generate all" with Focus combo filters still uses the local
    // engine for the filtered subset (lane-by-lane Claude would mix plans).
    if (focusCombo.getSelectedId() > 1)
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
        const auto label = processor.prefersOfflineGeneration()
            ? "Generated locally — offline mode"
            : (processor.ai().hasApiKey()
                   ? "Generated locally — focus filter (shared SongPlan)"
                   : "Generated locally — no API key");
        processor.noteOfflineResult (label);
        reportGeneration (processor.lastGenerationReport(), false);
        clearGeneratingUi();
        refreshPanels();
        return;
    }

    chatPanel.setBusy (true);
    juce::Component::SafePointer<AIMidiGenEditor> safe (this);
    processor.generatePreferredAll ([safe] (GenerationReport report)
    {
        if (safe == nullptr) return;
        safe->chatPanel.setBusy (false);
        safe->clearGeneratingUi();
        safe->reportGeneration (report, report.mode == GenerationMode::FailedClaude);
        safe->refreshPanels();
    });
}

bool AIMidiGenEditor::keyPressed (const juce::KeyPress& key)
{
    // Don't steal space (or anything else) while an interactive control has
    // focus — space would otherwise both click the control and toggle preview.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr
            || dynamic_cast<juce::Button*> (focused) != nullptr
            || dynamic_cast<juce::ComboBox*> (focused) != nullptr
            || dynamic_cast<juce::Slider*> (focused) != nullptr)
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
    {
        const auto piece = (DrumPiece) dp;
        drumKitPanel.setPieceHasContent (piece,
            ! processor.drumPiece (piece).notes.empty());
        drumKitPanel.setPieceMuted (piece, pieceBaseMute[(size_t) dp]);
        drumKitPanel.setPieceLocked (piece, processor.drumPiece (piece).locked);
    }

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

    // Title bar (42px) — traffic lights + ComposerAI + Options
    auto titleBar = r.removeFromTop (42);
    titleBarBounds = titleBar;
    CustomLookAndFeel::drawTitleBar (g, titleBar);

    // Header — VST v4 brand bar
    auto headerBar = r.removeFromTop (60);
    {
        juce::ColourGradient hg (juce::Colour (0x0fffffff), 0.0f, (float) headerBar.getY(),
                                 juce::Colour (0x04ffffff), 0.0f, (float) headerBar.getBottom(), false);
        g.setGradientFill (hg);
        g.fillRect (headerBar);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (headerBar.getBottom() - 1, 0.0f, (float) getWidth());
    }

    if (! brandBounds.isEmpty())
        CustomLookAndFeel::drawBrandLockup (g, brandBounds);

    if (! tabTrayBounds.isEmpty())
        CustomLookAndFeel::drawSegmentedTray (g, tabTrayBounds);

    if (! statusBadgeBounds.isEmpty())
        CustomLookAndFeel::drawBadge (g, statusBadgeBounds,
                                     processor.ai().hasApiKey() ? "Connected" : "Offline",
                                     processor.ai().hasApiKey());

    // Generate surface cards (left / right rails)
    if (currentSurface == Surface::Generate)
    {
        if (! leftCardBounds.isEmpty())
            CustomLookAndFeel::drawPanel (g, leftCardBounds);
        if (! rightCardBounds.isEmpty())
            CustomLookAndFeel::drawPanel (g, rightCardBounds);
    }

    // Footer — VST v4 transport dock
    auto footerBar = r.removeFromBottom (64);
    {
        juce::ColourGradient fg (juce::Colour (0x0affffff), 0.0f, (float) footerBar.getY(),
                                 juce::Colour (0x03ffffff), 0.0f, (float) footerBar.getBottom(), false);
        g.setGradientFill (fg);
        g.fillRect (footerBar);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (footerBar.getY(), 0.0f, (float) getWidth());
    }

    if (! bpmWellBounds.isEmpty())
        CustomLookAndFeel::drawInsetWell (g, bpmWellBounds, 9.0f);
}

void AIMidiGenEditor::resized()
{
    auto r = getLocalBounds();

    // ---- Title bar (42px) ----
    auto title = r.removeFromTop (42);
    titleBarBounds = title;
    optionsButton.setBounds (title.removeFromRight (88).reduced (12, 8));

    // ---- Header: brand · TabsList · Connected (VST v4) ----
    auto header = r.removeFromTop (60).reduced (20, 12);
    brandBounds = header.removeFromLeft (178).withSizeKeepingCentre (178, 32);
    headerLabel.setBounds ({});

    statusBadgeBounds = header.removeFromRight (118).withSizeKeepingCentre (118, 30);
    header.removeFromRight (12);
    apiStatusLabel.setBounds ({});

    const int trayW = 360;
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

    // ---- Footer (64px VST v4) ----
    auto footer = r.removeFromBottom (64).reduced (16, 14);
    keyLabel.setBounds (footer.removeFromLeft (28));
    rootCombo.setBounds (footer.removeFromLeft (52).reduced (0, 1));
    footer.removeFromLeft (6);
    scaleCombo.setBounds (footer.removeFromLeft (100).reduced (0, 1));
    footer.removeFromLeft (12);

    // Sunken BPM well: value + "BPM · HOST" + −/+ nudge buttons
    bpmWellBounds = footer.removeFromLeft (172).withSizeKeepingCentre (172, 36);
    auto bpmInner = bpmWellBounds.reduced (10, 4);
    bpmPlus.setBounds (bpmInner.removeFromRight (20).withSizeKeepingCentre (20, 20));
    bpmInner.removeFromRight (4);
    bpmMinus.setBounds (bpmInner.removeFromRight (20).withSizeKeepingCentre (20, 20));
    bpmInner.removeFromRight (6);
    bpmValue.setFont (CustomLookAndFeel::font (20.0f, juce::Font::bold));
    bpmValue.setBounds (bpmInner.removeFromLeft (48));
    bpmLabel.setFont (CustomLookAndFeel::font (9.0f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt3);
    bpmLabel.setBounds (bpmInner);

    exportAllButton.setBounds (footer.removeFromRight (100).withSizeKeepingCentre (100, 36));
    footer.removeFromRight (8);
    previewButton.setBounds (footer.removeFromRight (104).withSizeKeepingCentre (104, 36));
    previewButton.setComponentID ("primary");
    footer.removeFromRight (8);
    hostMidiButton.setBounds (footer.removeFromRight (86).withSizeKeepingCentre (86, 36));
    footer.removeFromRight (6);
    hostSyncButton.setBounds (footer.removeFromRight (92).withSizeKeepingCentre (92, 36));

    // Main body inset below header/footer (v4 also has ~12px top gap before footer via margin)
    r = r.reduced (0, 6);

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
    // VST v4 grid: 308 | 1fr | 232, gap 12
    auto r = generateSurface.getLocalBounds().reduced (12, 0);

    auto railOuter = r.removeFromRight (232);
    r.removeFromRight (12);
    rightCardBounds = juce::Rectangle<int> (railOuter.getX() + generateSurface.getX(),
                                            railOuter.getY() + generateSurface.getY(),
                                            railOuter.getWidth(), railOuter.getHeight());
    auto rail = railOuter.reduced (14, 16);
    {
        soundLabel.setText ("GENRE", juce::dontSendNotification);
        soundLabel.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
        soundLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt3);
        soundLabel.setBounds (rail.removeFromTop (14));
        genreCombo.setBounds (rail.removeFromTop (36));
        rail.removeFromTop (10);
        generateLaneButton.setBounds (rail.removeFromTop (44));
        rail.removeFromTop (8);
        generateAllButton.setBounds (rail.removeFromTop (40));
        rail.removeFromTop (8);
        varyLaneButton.setBounds (rail.removeFromTop (40));
        rail.removeFromTop (8);
        useLocalButton.setBounds (rail.removeFromTop (36));
        rail.removeFromTop (6);
        offlineModeButton.setBounds (rail.removeFromTop (32));
        rail.removeFromTop (8);
        undoButton.setBounds (rail.removeFromTop (40));
        rail.removeFromTop (8);
        newIdeaButton.setBounds (rail.removeFromTop (36));
        rail.removeFromTop (6);
        if (dnaButton.isVisible())
            dnaButton.setBounds (rail.removeFromTop (36));

        // LAST RUN pinned to bottom
        auto meta = rail.removeFromBottom (72);
        lastRunTitle.setBounds (meta.removeFromTop (16));
        lastRunMeta.setBounds (meta);
    }

    auto leftOuter = r.removeFromLeft (308);
    r.removeFromLeft (12);
    leftCardBounds = juce::Rectangle<int> (leftOuter.getX() + generateSurface.getX(),
                                           leftOuter.getY() + generateSurface.getY(),
                                           leftOuter.getWidth(), leftOuter.getHeight());
    auto left = leftOuter.reduced (8, 8);

    const int laneH = InstrumentLane::kHeight;
    const int laneGap = 2;
    const int nLanes = (int) InstrumentType::NumTypes;
    const int lanesH = nLanes * laneH + (nLanes - 1) * laneGap;

    int y = left.getY();
    for (int i = 0; i < nLanes; ++i)
    {
        if (lanes[(size_t) i] == nullptr) continue;
        lanes[(size_t) i]->setBounds (left.getX(), y, left.getWidth(), laneH);
        y += laneH + laneGap;
    }

    // Hide legacy InstrumentPanels — TrackDetailPanel is the v4 detail
    for (auto& panel : panels)
        if (panel) panel->setBounds ({});

    const int detailTop = left.getY() + lanesH + 4;
    const int detailH = juce::jmax (0, left.getBottom() - detailTop);
    trackDetail.setBounds (left.getX(), detailTop, left.getWidth(), detailH);

    // Center: piano roll (~1.6) + chords (~1.0) — VST v4 proportions
    auto center = r;
    const bool drums = focusedLane == InstrumentType::Drums;
    if (drums)
    {
        auto rollArea = center.removeFromTop (juce::jmax (180, center.getHeight() * 42 / 100));
        midiRoll.setBounds (rollArea);
        center.removeFromTop (12);
        drumKitPanel.setBounds (center);
        chordDashboard.setBounds ({});
    }
    else
    {
        const int total = center.getHeight() - 12;
        const int rollH = juce::jmax (220, total * 16 / 26); // 1.6 / (1.6+1.0)
        midiRoll.setBounds (center.removeFromTop (rollH));
        center.removeFromTop (12);
        chordDashboard.setBounds (center);
        drumKitPanel.setBounds ({});
    }

    // Error banner floats over the bottom of the piano roll.
    generateErrorLabel.setBounds (midiRoll.getBounds().removeFromBottom (26).reduced (12, 1));
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

void AIMidiGenEditor::syncEffectiveMutes()
{
    bool anySolo = false;
    for (bool s : laneSolo)
        if (s) { anySolo = true; break; }

    // Preview audio reads piece/part .muted flags only — no sequence rebuild needed.
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const bool eff = baseMute[(size_t) t] || (anySolo && ! laneSolo[(size_t) t]);
        if ((InstrumentType) t == InstrumentType::Drums)
        {
            // Per-piece kit mutes survive lane-level mute/solo: a piece is
            // silent if the user muted IT or the whole drum lane is muted.
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
                processor.drumPiece ((DrumPiece) dp).muted =
                    eff || pieceBaseMute[(size_t) dp];
            processor.part (InstrumentType::Drums).muted = eff;
        }
        else
        {
            processor.part ((InstrumentType) t).muted = eff;
        }
    }
}

void AIMidiGenEditor::beginGeneratingUi()
{
    generatingUi = true;
    // Every caller pairs this with an explicit clearGeneratingUi() — either
    // synchronously or from the async completion callback. No timer fallback,
    // so the busy state can't linger after fast local generates.
    const auto name = juce::String (toString (focusedLane));
    midiRoll.setGenerating (true, name);
    trackDetail.setGenerating (true);
    generateLaneButton.setButtonText ("Composing…");
    generateLaneButton.setEnabled (false);
}

void AIMidiGenEditor::showGenerateError (const juce::String& message)
{
    generateErrorLabel.setText (message, juce::dontSendNotification);
    generateErrorLabel.setVisible (true);
    generateErrorLabel.toFront (false);
    generateErrorHideAtMs = juce::Time::getMillisecondCounter() + 6000;
}

void AIMidiGenEditor::clearGeneratingUi()
{
    if (! generatingUi) return;
    generatingUi = false;
    midiRoll.setGenerating (false);
    trackDetail.setGenerating (false);
    generateLaneButton.setEnabled (true);
    generateLaneButton.setButtonText ("Generate " + juce::String (toString (focusedLane)));
}

void AIMidiGenEditor::refreshLastRunMeta()
{
    const auto& mp = processor.params();
    lastRunSeed = juce::String ((juce::int64) mp.seed);
    const auto name = juce::String (toString (focusedLane));
    const auto& report = processor.lastGenerationReport();
    const juce::String source = report.statusLine.empty()
        ? juce::String ("no runs yet")
        : juce::String (report.statusLine);
    lastRunMeta.setText (name + " · " + juce::String (mp.bars) + " bars · "
                             + juce::String (mp.root) + " " + juce::String (mp.scale)
                             + "\nseed " + lastRunSeed + " · " + source,
                         juce::dontSendNotification);
}

juce::String AIMidiGenEditor::notesLineFor (InstrumentType type)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (type == InstrumentType::Drums)
    {
        int hits = 0;
        for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
            hits += (int) processor.drumPiece ((DrumPiece) dp).notes.size();
        return hits > 0 ? (juce::String (hits) + " hits") : "—";
    }

    const auto& part = processor.part (type);
    if (part.notes.empty()) return "—";

    juce::StringArray out;
    const int n = juce::jmin (6, (int) part.notes.size());
    for (int i = 0; i < n; ++i)
        out.add (names[((part.notes[(size_t) i].pitch % 12) + 12) % 12]);
    auto s = out.joinIntoString (" · ");
    if ((int) part.notes.size() > n) s += " …";
    return s;
}

void AIMidiGenEditor::refreshTrackDetail()
{
    bool has = false;
    bool locked = false;
    if (focusedLane == InstrumentType::Drums)
    {
        for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        {
            auto& p = processor.drumPiece ((DrumPiece) dp);
            if (! p.notes.empty()) has = true;
            if (p.locked) locked = true;
        }
    }
    else
    {
        auto& p = processor.part (focusedLane);
        has = ! p.notes.empty();
        locked = p.locked;
    }

    trackDetail.setTrack (focusedLane, has, notesLineFor (focusedLane));
    trackDetail.setMuted (baseMute[(size_t) focusedLane]);
    trackDetail.setLocked (locked);
    trackDetail.setGenerating (generatingUi);

    if (focusedLane != InstrumentType::Drums)
    {
        trackDetail.setVolume (processor.getPartGain (focusedLane));
        const auto map = defaultsFor (processor.getGenreMode());
        const auto& opts = map.variants[(size_t) focusedLane];
        juce::StringArray names;
        int selectedId = 1;
        const auto cur = processor.getPartTimbre (focusedLane);
        for (int i = 0; i < (int) opts.size(); ++i)
        {
            names.add (toString (opts[(size_t) i]));
            if (opts[(size_t) i] == cur) selectedId = i + 1;
        }
        trackDetail.setTimbreOptions (names, selectedId);
    }
    else
    {
        trackDetail.setVolume (0.72f);
        trackDetail.setTimbreOptions ({ "Drum kit" }, 1);
    }
}

} // namespace aimidi
