#pragma once

#include "MusicInstructions.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <string>

namespace aimidi
{
/** Groove + harmony DNA extracted from a user-supplied MIDI file.

    "MIDI packs as DNA": drop in a loop you love and generation inherits its
    feel — drum onsets become per-piece step patterns (replacing the style
    preset for the pieces the loop actually plays), and the pitched material
    votes on a root + scale which is offered back to the project params.

    Analysis only — the file's notes are never copied into the output. */
struct MidiDna
{
    bool valid = false;
    std::string sourceName;

    // --- groove (drum channel / GM percussion notes) ---
    bool hasDrums = false;
    std::array<std::array<float, 16>, (size_t) DrumPiece::NumPieces> drumSteps {};

    // --- harmony (pitched notes) ---
    bool hasHarmony = false;
    int rootPc = 0;                 // 0..11
    std::string scale = "minor";    // best-matching scale name

    /** Parse + analyze a .mid file. Returns an invalid DNA on failure. */
    static MidiDna analyzeFile (const juce::File& file);

    /** Human-readable one-liner of what was learned (for the chat panel). */
    juce::String describe() const;
};

} // namespace aimidi
