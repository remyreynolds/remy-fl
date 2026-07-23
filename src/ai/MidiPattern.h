#pragma once

#include "../engine/MusicInstructions.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace aimidi
{
/** One note from Claude's MIDI JSON (already validated / converted). */
struct PatternNote
{
    juce::String pitchName;
    int    pitchMidi = 60;
    double startBeat = 0.0;
    double durationBeats = 1.0;
    int    velocity = 100;
};

/** One instrument lane inside a pattern (single-part or full-loop). */
struct MidiPatternPart
{
    juce::String instrument { "bass" };
    std::vector<PatternNote> notes;
};

/** Validated Claude MIDI pattern (strict schema).
    Single-part: top-level instrument + notes (legacy).
    Full loop: parts[] with multiple instruments sharing bpm/key/bars. */
struct MidiPattern
{
    int bpm = 120;
    juce::String key { "C minor" };
    juce::String instrument { "bass" }; // primary / legacy
    int bars = 4;
    juce::String timeSignature { "4/4" };
    std::vector<PatternNote> notes;     // primary / legacy
    std::vector<MidiPatternPart> parts; // multi-instrument loop (optional)

    /** Flattened list of parts to apply (uses parts[] if set, else single lane). */
    std::vector<MidiPatternPart> lanes() const;
    int totalNotes() const;
    juce::String instrumentSummary() const;
};

struct MidiPatternParseResult
{
    bool ok = false;
    juce::String error;
    MidiPattern pattern;
};

/** Stage 2 — system prompt: JSON-only Claude MIDI schema. */
juce::String buildClaudeMidiSystemPrompt();

/** Convert "A1", "C#2", "Bb3" → MIDI note number. Returns -1 on failure. */
int noteNameToMidi (const juce::String& name, juce::String* errorOut = nullptr);

/** Stage 3 — parse + validate Claude's JSON text (not the Anthropic envelope). */
MidiPatternParseResult parseClaudeMidiJson (const juce::String& jsonText);

/** Map instrument string → InstrumentType (best effort). */
InstrumentType instrumentTypeFromName (const juce::String& name);

/** Stage 4 — PPQ MidiMessageSequence (noteOn/noteOff, sorted). */
juce::MidiMessageSequence patternToSequence (const MidiPattern& pattern,
                                             int ppq = 960);

/** Apply pattern into a GeneratedPart for the existing drag/export/preview path. */
GeneratedPart patternToGeneratedPart (const MidiPattern& pattern);
GeneratedPart patternPartToGeneratedPart (const MidiPatternPart& part,
                                          int bpm, int bars);

/** Best-effort key string ("A minor") → MusicParams root/scale. */
void applyKeyStringToParams (const juce::String& key, MusicParams& params);

/** MIDI note number → "C3", "F#2", etc. */
juce::String midiToNoteName (int midiNote);

/** Compact harmony signature used to detect repeated AI chord progressions.
    Empty when the pattern has no chord lane. */
juce::String chordProgressionFingerprint (const MidiPattern& pattern);

/** Compact note dump for AI continue/vary prompts. */
juce::String serializePartAsMidiContext (const GeneratedPart& part,
                                         const MusicParams& params,
                                         int maxNotes = 80);

} // namespace aimidi
