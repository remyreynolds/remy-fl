#pragma once

#include "MusicInstructions.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <random>
#include <vector>

namespace aimidi
{
/** Turns MusicParams into validated, in-key NoteEvents per instrument, and
    bakes them into a juce::MidiMessageSequence for playback / export / drag.

    Phase 1: deterministic rule-based generators (house-oriented) that already
    respect scale, note-length conventions and syncopation. The AIClient will
    later feed richer MusicParams derived from the user's prompt. */
class MidiGenerator
{
public:
    MidiGenerator() = default;

    /** Generate a single instrument part. Respects params.seed for variety. */
    GeneratedPart generate (InstrumentType type, const MusicParams& params);

    /** Generate the full 5-piece drum kit (kick/snare/clap/closed hat/open
        hat) as separate, independently-editable parts. Each piece is already
        validated (scale N/A for drums, but swing/humanize/de-overlap applied). */
    std::array<GeneratedPart, (size_t) DrumPiece::NumPieces>
        generateDrumKit (const MusicParams& params);

    /** Regenerate just one drum-kit piece (e.g. only the kick) without
        touching the pattern of the others. */
    GeneratedPart generateDrumPiece (DrumPiece piece, const MusicParams& params);

    /** Validate + repair a part in place: snap to scale, clamp ranges,
        remove overlaps, clamp velocity. Returns number of notes repaired. */
    int validate (GeneratedPart& part, const MusicParams& params);

    /** Convert a part to a standard MIDI sequence (ticks per quarter = 960). */
    static juce::MidiMessageSequence toSequence (const GeneratedPart& part,
                                                 double bpm);

    /** Write a part to a temp .mid file and return it (for drag & export). */
    static juce::File writeTempMidiFile (const GeneratedPart& part,
                                         const MusicParams& params,
                                         const juce::String& baseName);

    /** Multi-track .mid: one track per non-empty part (MIDI Agent-style export). */
    static juce::File writeTempMultiTrackMidiFile (
        const std::vector<const GeneratedPart*>& parts,
        const MusicParams& params,
        const juce::String& baseName = "AIMidiGen_All");

    static constexpr int ticksPerQuarter = 960;

private:
    GeneratedPart generateChords (const MusicParams&);
    GeneratedPart generateBass   (const MusicParams&);
    GeneratedPart generateMelody (const MusicParams&);
    GeneratedPart generateDrums  (const MusicParams&);
    GeneratedPart generateArp    (const MusicParams&);
    GeneratedPart generatePad    (const MusicParams&);

    std::mt19937 rng { std::random_device{}() };
    float rand01() { return std::uniform_real_distribution<float> (0.f, 1.f) (rng); }
    int   randInt (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }

    /** Draw a house chord progression (scale degrees, one per bar) from a
        curated pool. Same seeded rng state => same progression, so chords,
        bass and pad agree when generated with the same params.seed. */
    std::vector<int> pickProgression();
};

} // namespace aimidi
