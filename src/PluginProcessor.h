#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/MidiGenerator.h"
#include "engine/MidiDna.h"
#include "engine/PreviewSynth.h"
#include "engine/PreviewSounds.h"
#include "engine/SampleLibrary.h"
#include "engine/MidiLibrary.h"
#include "ai/AIClient.h"
#include "engine/GenerationReport.h"
#include <array>
#include <functional>
#include <vector>

namespace aimidi
{
/** The plugin core. Owns the project state (params + generated parts),
    the generation engine, the AI client, a built-in preview synth (piano +
    basic kit) for in-app listening, and MIDI-out for drag/DAW use. */
class AIMidiGenProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    AIMidiGenProcessor();
    ~AIMidiGenProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AI MIDI Gen"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // ---- Project API used by the editor ----
    MusicParams& params() { return projectParams; }
    GeneratedPart& part (InstrumentType t) { return parts[(size_t) t]; }

    void generatePart (InstrumentType t, bool recordUndo = true);
    /** One undo step for regenerating every unlocked pitched/drum part. */
    void generateAllParts (bool recordUndo = true);

    /** Offline / local SongPlan engine (never claims to be Claude). */
    void generateAllPartsOffline (const juce::String& reasonLabel = "Generated locally — no API key");
    void generatePartOffline (InstrumentType t, bool recordUndo = true,
                              const juce::String& reasonLabel = "Generated locally — no API key");
    /** Record an offline result after MIDI was already written (e.g. focus filter). */
    void noteOfflineResult (const juce::String& reasonLabel);
    /** Preferred Generate path: Claude+Brain when a key is present and offline
        mode is off; otherwise local. On Claude failure does NOT silently fall
        back — sets lastGeneration to FailedClaude and leaves MIDI unchanged. */
    void generatePreferredAll (std::function<void (GenerationReport)> onDone);
    void generatePreferredLane (InstrumentType t,
                                std::function<void (GenerationReport)> onDone);

    /** New Idea: fresh seed + meaningfully different harmonic fingerprint. */
    void newIdeaPreferred (std::function<void (GenerationReport)> onDone);

    /** Vary chords via Claude while locking key / genre / BPM / bars. */
    void varyChordsWithAI (std::function<void (AIClient::PatternResponse)> onDone);

    /** After a Claude failure, run the pending local generation if the user
        explicitly chooses offline. */
    void useLocalGeneratorNow (std::function<void (GenerationReport)> onDone = nullptr);
    bool hasPendingLocalOffer() const { return pendingLocalOffer; }

    /** When true, Generate / New Idea always use the local engine even if a key exists. */
    void setPreferOfflineGeneration (bool offline) { preferOfflineGeneration = offline; }
    bool prefersOfflineGeneration() const { return preferOfflineGeneration; }

    const GenerationReport& lastGenerationReport() const { return lastGeneration; }
    juce::String lastHarmonyFingerprint() const { return lastHarmonyFp; }

    /** Chat Generate: Claude → validated note JSON → part notes (drag/export ready). */
    void regenerateFromAI (const juce::String& prompt,
                           std::function<void (AIClient::PatternResponse)> onDone);

    /** MIDI Agent-style: vary or continue an existing part via the active LLM. */
    void transformPartWithAI (InstrumentType t,
                              const juce::String& mode, // "vary" | "continue"
                              std::function<void (AIClient::PatternResponse)> onDone);

    /** Compact JSON of a part for chat "Use MIDI" attachment. */
    juce::String midiContextForPart (InstrumentType t) const;

    /** Multi-track temp MIDI of all non-empty parts. */
    juce::File exportAllPartsMidiFile() const;

    /** Chat box: local-first. Commands ("make a bassline", "128 bpm", "undo")
        execute instantly via the deterministic engine — no network. Only real
        conversation falls through to the AI, grounded with a live project brief. */
    void handleChatTurn (const juce::String& prompt,
                         std::function<void (AIClient::TurnResponse)> onDone);

    /** Try to execute the message as an instant local command (ChatDirector).
        Returns true if handled; fills `out` with the reply. */
    bool tryLocalChatCommand (const juce::String& text, AIClient::TurnResponse& out);

    /** Compact "PROJECT STATE" snapshot (style, key, BPM, lanes, critic) used
        to ground every AI chat turn. */
    juce::String buildProjectContextBrief() const;

    // ---- Drum kit: kick/snare/clap/closed hat/open hat as separate,
    //      independently generatable/lockable/mutable/draggable parts. ----
    GeneratedPart& drumPiece (DrumPiece dp) { return drumKit[(size_t) dp]; }
    void generateDrumKit (bool recordUndo = true);   // regenerate all unlocked pieces
    void generateDrumPiece (DrumPiece dp, bool recordUndo = true);
    void rebuildDrumMasterPart();           // recompute the merged "whole loop" view in part(Drums)

    void togglePreview (bool shouldPlay);
    bool isPreviewing() const { return previewing.load(); }

    /** Set project tempo (clamped). Preview playhead speed follows immediately. */
    void setBpm (double bpm);
    double getBpm() const { return projectParams.bpm; }

    /** When true, project BPM follows the host playhead (DAW tempo). */
    void setHostTempoSync (bool shouldSync);
    bool isHostTempoSync() const { return hostTempoSync.load(); }
    /** When true and host is playing, emit MIDI from parts (MIDI out to DAW). */
    void setHostMidiOut (bool shouldEmit);
    bool isHostMidiOut() const { return hostMidiOut.load(); }
    /** Audition through built-in preview synth (can run with or without host MIDI out). */
    void setPreviewAudition (bool shouldAudition);
    bool isPreviewAudition() const { return previewAudition.load(); }
    /** Read host playhead; returns false if unavailable (e.g. standalone idle). */
    bool getHostTransport (bool& isPlaying, double& ppqPosition, double& bpm) const;

    /** Current preview / host-relative position in quarter-note beats within the loop. */
    double getPreviewPositionBeats() const { return previewPpqPos.load(); }
    double getLoopLengthBeats() const { return projectParams.bars * 4.0; }

    // ---- Genre playback sounds ----
    GenreMode getGenreMode() const { return genreMode; }
    PartTimbre getPartTimbre (InstrumentType t) const { return partTimbres[(size_t) t]; }
    void setGenreMode (GenreMode mode, bool applyDefaults = true);
    void setPartTimbre (InstrumentType t, PartTimbre timbre);
    /** If text mentions a genre, switch mode + defaults. Returns true if switched. */
    bool autoDetectGenreFromText (const juce::String& text);

    /** Preview mix gain 0..1 (genre defaults from mix knowledge; user can override). */
    float getPartGain (InstrumentType t) const { return partGains[(size_t) t]; }
    float getDrumGain (DrumPiece dp) const { return drumGains[(size_t) dp]; }
    void setPartGain (InstrumentType t, float gain01);
    void setDrumGain (DrumPiece dp, float gain01);
    void applyMixDefaults();

    // ---- Sample packs (preview) ----
    SampleLibrary& samples() { return sampleLibrary; }
    const SampleLibrary& samples() const { return sampleLibrary; }
    MidiLibrary& midiLoops() { return midiLibrary; }
    const MidiLibrary& midiLoops() const { return midiLibrary; }

    juce::String getDrumSampleId (DrumPiece dp) const { return drumSampleIds[(size_t) dp]; }
    juce::String getPartSampleId (InstrumentType t) const { return partSampleIds[(size_t) t]; }
    juce::String getPartMidiLoopId (InstrumentType t) const { return partMidiLoopIds[(size_t) t]; }
    void setDrumSampleId (DrumPiece dp, const juce::String& id);
    void setPartSampleId (InstrumentType t, const juce::String& id);
    /** Load a MIDI pack loop into a pitched instrument (or clear with empty id). */
    bool applyMidiLoopToPart (InstrumentType t, const juce::String& midiId,
                              juce::String* errorOut = nullptr);
    /** Load a matching Bass/Chord/Lead/Guitar set (e.g. Kit_1) into the pitched parts. */
    int applyMidiKitBundle (const juce::String& packName, const juce::String& kitTag,
                            juce::String* errorOut = nullptr);
    /** Save MIDI pack inventory + funky-house usage notes into the knowledge brain. */
    bool rememberMidiPackInBrain (const juce::String& packName = {});
    juce::String buildMidiPackMemoryDoc (const juce::String& packName = {}) const;
    /** Assign pack WAVs to drum pieces only (melody/bass/chords stay synth). */
    int autoAssignSamplesFromLibrary (const juce::String& packName = {});
    /** Save active drum kit + drum-loop theory into the local knowledge brain. */
    bool rememberDrumKitInBrain (const juce::String& packName = {});
    juce::String buildDrumKitMemoryDoc (const juce::String& packName = {}) const;
    void syncSamplesToSynth();
    bool hasAnySampleAssignment() const;
    int countLoadedPreviewSamples() const;

    /** Snapshot parts before a destructive generate; Undo restores the last snapshot. */
    void pushUndoSnapshot();
    bool canUndo() const { return ! undoStack.empty(); }
    bool undoLastGeneration();

    AIClient& ai() { return aiClient; }

    // ---- MIDI-pack DNA: learn groove + harmony from a user MIDI file. ----
    /** Analyze the file, adopt its groove/harmony for future generation,
        regenerate unlocked parts, and return a human-readable report for
        the chat panel. */
    juce::String loadDna (const juce::File& midiFile);
    bool hasDna() const { return dna.valid; }

    // ---- Cross-part critic: runs after every generation path. ----
    const juce::String& lastCriticSummary() const { return criticSummary; }

private:
    struct PartSnapshot
    {
        MusicParams params;
        std::array<GeneratedPart, (size_t) InstrumentType::NumTypes> parts;
        std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> drumKit;
        // Undo must also revert style-switch side effects (timbres, mix, mode):
        GenreMode genreMode {};
        std::array<PartTimbre, (size_t) InstrumentType::NumTypes> partTimbres {};
        std::array<float, (size_t) InstrumentType::NumTypes> partGains {};
        std::array<float, (size_t) DrumPiece::NumPieces> drumGains {};
        juce::String criticSummary;
    };

    MusicParams projectParams;
    std::array<GeneratedPart, (size_t) InstrumentType::NumTypes> parts;
    std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> drumKit;
    std::vector<PartSnapshot> undoStack;

    MidiGenerator generator;
    AIClient      aiClient;
    PreviewSynth  previewSynth;
    SampleLibrary sampleLibrary;
    MidiLibrary   midiLibrary;
    std::array<juce::String, (size_t) DrumPiece::NumPieces> drumSampleIds {};
    std::array<juce::String, (size_t) InstrumentType::NumTypes> partSampleIds {};
    std::array<juce::String, (size_t) InstrumentType::NumTypes> partMidiLoopIds {};

    GenreMode genreMode { GenreMode::House };
    std::array<PartTimbre, (size_t) InstrumentType::NumTypes> partTimbres {};
    std::array<float, (size_t) InstrumentType::NumTypes> partGains {};
    std::array<float, (size_t) DrumPiece::NumPieces> drumGains {};

    MidiDna dna;                 // groove/harmony learned from a MIDI pack
    juce::String criticSummary;  // what the critic did on the last pass
    GenerationReport lastGeneration;
    juce::String lastHarmonyFp;
    bool preferOfflineGeneration = false;
    bool pendingLocalOffer = false;
    enum class PendingLocalAction { None, All, Lane, NewIdea };
    PendingLocalAction pendingLocalAction = PendingLocalAction::None;
    InstrumentType pendingLocalLane = InstrumentType::Chords;

    void runCritic();            // review + repair the arrangement in place
    void recordOfflineGeneration (const juce::String& reasonLabel);
    void recordClaudeSuccess (const MidiPattern& pattern, const juce::String& assistant);
    void recordClaudeFailure (const juce::String& error,
                              PendingLocalAction action,
                              InstrumentType lane = InstrumentType::Chords);
    juce::String buildClaudeGeneratePrompt (InstrumentType focusOrAll) const;
    juce::String currentSongPlanFingerprint() const;
    bool rollSeedUntilFingerprintChanges (const juce::String& previousFp, int maxTries = 12);

    // --- preview playback ---
    std::atomic<bool> previewing { false };
    std::atomic<bool> hostTempoSync { true };
    std::atomic<bool> hostMidiOut { true };
    std::atomic<bool> previewAudition { true };
    double sampleRateHz = 44100.0;
    std::atomic<double> previewPpqPos { 0.0 }; // quarter notes within the loop
    std::array<juce::MidiMessageSequence, (size_t) InstrumentType::NumTypes> previewSeqs;
    std::array<int, (size_t) InstrumentType::NumTypes> previewIdx {};
    std::array<juce::MidiMessageSequence, (size_t) DrumPiece::NumPieces> drumPreviewSeqs;
    std::array<int, (size_t) DrumPiece::NumPieces> drumPreviewIdx {};

    void rebuildPreviewSequences();
    void collectPreviewMidi (juce::MidiBuffer& dest, int numSamples);
    /** Emit notes for a host-aligned PPQ window (loop-wrapped). */
    void collectMidiForPpqWindow (juce::MidiBuffer& dest, int numSamples,
                                  double startPpqInLoop, double ppqPerSample);
    void syncPreviewSounds();
    void pullHostTempoIfNeeded();
    void applyMidiPattern (MidiPattern& pattern, juce::String& errorOut);

    /** Guards preview sequence rebuild vs audio-thread playback. */
    juce::CriticalSection processLock;

    // ---- Real-time-safety bridge (audio thread never mutates project state) ----
    /** Audio-thread-readable mirrors, refreshed by rebuildPreviewSequences()/setBpm(). */
    std::atomic<double> liveBpm  { 124.0 };
    std::atomic<int>    liveBars { 4 };
    /** Audio thread requests message-thread work (seq rebuild, host-bpm adopt). */
    std::atomic<bool>   seqRebuildRequested { false };
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMidiGenProcessor)
};

} // namespace aimidi
