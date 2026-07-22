#pragma once

#include "MusicInstructions.h"
#include "MusicTheory.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace aimidi
{
struct ChordHit
{
    double startBeat = 0.0;
    juce::String name; // e.g. "Am", "Fmaj7", "—"
};

/** Pitch-class → note name preferring flats/sharps from project root. */
juce::String pitchClassName (int pc, const std::string& preferRoot);

/** Name a simultaneous pitch set (MIDI notes) in context of project key. */
juce::String nameChordFromPitches (const std::vector<int>& midiPitches,
                                   const MusicParams& params);

/** Collect unique chord changes for a part across the loop (by note start groups). */
std::vector<ChordHit> analysePartChords (const GeneratedPart& part,
                                         const MusicParams& params);

/** Chord sounding at beat t (for playhead "now"). */
juce::String chordAtBeat (const GeneratedPart& part,
                          const MusicParams& params,
                          double beat);

} // namespace aimidi
