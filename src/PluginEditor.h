#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/CustomLookAndFeel.h"
#include "gui/ChatPanel.h"
#include "gui/InstrumentPanel.h"
#include "gui/InstrumentLane.h"
#include "gui/TrackDetailPanel.h"
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
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Surface { Generate, Browse, Chat, Settings };

    void setSurface (Surface s);
    void layoutGenerateSurface();
    void layoutBrowseSurface();
    void layoutChatSurface();
    void layoutSettingsSurface();
    void handlePrompt (const juce::String& prompt);
    void handlePartTransform (InstrumentType type, const juce::String& mode);
    void refreshPanels();
    void refreshMidiRoll();
    void refreshLanes();
    void setFocusedLane (InstrumentType type);
    void refreshBpmLabel();
    void refreshSoundControls();
    void refreshChordDashboard();
    void refreshSampleControls();
    void refreshMidiLoopControls();
    void refreshProviderUi();
    void refreshTrackDetail();
    void refreshLastRunMeta();
    void applyWorkspaceFocus();
    void syncEffectiveMutes();
    void beginGeneratingUi();
    void clearGeneratingUi();
    void showGenerateError (const juce::String& message);
    bool isTypeInFocus (InstrumentType type) const;
    void generateFocusedParts();
    void generateFocusedLane();
    void runNewIdea();
    void reportGeneration (const GenerationReport& report, bool offerLocal);
    void exportAllTracks();
    void nudgeBpm (int delta);
    void showHelpOverlay();
    void timerCallback() override;
    InstrumentType pickPartForMidiAttach() const;
    juce::String notesLineFor (InstrumentType type);

    AIMidiGenProcessor& processor;
    CustomLookAndFeel   lnf;
    juce::TooltipWindow tooltipWindow { this, 650 }; // tooltips need a host window

    // Surface containers (declared before their children so they outlive them)
    Surface currentSurface = Surface::Generate;
    juce::Component generateSurface;
    juce::Component browseSurface;
    juce::Component chatSurface;
    juce::Component settingsSurface;

    // Header tabs
    juce::TextButton generateTabButton { "Generate" };
    juce::TextButton browseTabButton { "Browse" };
    juce::TextButton chatTabButton { "Chat" };
    juce::TextButton settingsTabButton { "Settings" };

    juce::Label   headerLabel;
    juce::Label   apiStatusLabel;
    juce::TextButton optionsButton { "Options" };
    juce::TextButton helpButton { "?" };
    juce::Label   lastRunTitle { {}, "LAST RUN" };
    juce::Label   lastRunMeta;
    juce::Rectangle<int> titleBarBounds;
    juce::Rectangle<int> tabTrayBounds;
    juce::Rectangle<int> statusBadgeBounds;
    juce::Rectangle<int> brandBounds;
    juce::Rectangle<int> leftCardBounds;
    juce::Rectangle<int> rightCardBounds;
    juce::Rectangle<int> bpmWellBounds;
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
    juce::TextButton newIdeaButton { "New idea" };   // fresh seed + generate all + critic
    juce::TextButton generateLaneButton { "Generate" };
    juce::TextButton varyLaneButton { "Vary" };
    juce::TextButton useLocalButton { "Use local generator" };
    juce::TextButton offlineModeButton { "Offline" };
    juce::TextButton hostSyncButton { "Host BPM" };
    juce::TextButton hostMidiButton { "MIDI out" };
    FileDragButton exportAllButton { "Export all" };
    juce::TextButton addSoundsButton { "Add sounds" };
    juce::TextButton addMidiButton { "Add MIDI" };
    juce::TextButton soundsFolderButton { "Folder" };
    juce::Label   samplesStatusLabel;
    juce::Label   providerLabel { {}, "AI" };
    juce::ComboBox providerCombo;
    juce::Label   modelLabel { {}, "Model" };
    juce::ComboBox modelCombo;
    juce::TextButton dnaButton { "MIDI DNA" };       // learn groove/key from a .mid
    std::unique_ptr<juce::FileChooser> dnaChooser;
    juce::TextEditor apiKeyField;
    juce::Label   apiKeyLabel { {}, "API key" };
    juce::TextButton testKeyButton { "Test key" };
    juce::Label   keyTestStatus;
    bool suppressGenreCallback = false;
    bool suppressKeyCallback = false;
    bool suppressFocusCallback = false;
    bool suppressSampleFilterCallback = false;
    bool suppressPackCallback = false;
    bool suppressMidiPackCallback = false;
    bool suppressProviderCallback = false;
    bool suppressModelCallback = false;

    int readyParts = 0;
    InstrumentType focusedLane = InstrumentType::Melody;
    juce::String lastSoundPicksGenre; // last genre announced as "Sound picks" in chat
    std::array<bool, (size_t) InstrumentType::NumTypes> laneSolo {};
    std::array<bool, (size_t) InstrumentType::NumTypes> baseMute {}; // user mute before solo overlay
    std::array<bool, (size_t) DrumPiece::NumPieces> pieceBaseMute {}; // per-piece kit mutes (survive lane mute/solo)
    bool generatingUi = false;
    juce::String lastRunSeed;
    juce::Label generateErrorLabel;          // transient failure banner on Generate surface
    juce::uint32 generateErrorHideAtMs = 0;  // auto-hide deadline (~6s)

    /** Dimmed full-editor overlay with the quick-start / shortcuts panel.
        Shown on first run and any time via the "?" header button. */
    class HelpOverlay : public juce::Component
    {
    public:
        HelpOverlay();
        std::function<void()> onDismiss;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> panelArea() const;
        juce::TextButton gotItButton { "Got it" };
    };
    std::unique_ptr<HelpOverlay> helpOverlay;

    MidiRollView midiRoll;
    ChordDashboardView chordDashboard;
    ChatPanel chatPanel;
    TrackDetailPanel trackDetail;
    std::array<std::unique_ptr<InstrumentPanel>, (size_t) InstrumentType::NumTypes> panels;
    std::array<std::unique_ptr<InstrumentLane>, (size_t) InstrumentType::NumTypes> lanes;
    DrumKitPanel drumKitPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMidiGenEditor)
};

} // namespace aimidi
