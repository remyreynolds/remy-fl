#pragma once

#include <string>
#include <vector>

namespace aimidi
{
/** The structured "score" the AI produces from a natural-language prompt.
    Per the spec, the AI NEVER emits raw MIDI — it emits these validated
    instructions, and the MidiGenerator turns them into notes. */

enum class InstrumentType
{
    Melody, Chords, Bass, Drums, CounterMelody, Arp, Pad, NumTypes
};

inline const char* toString (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::Melody:        return "Melody";
        case InstrumentType::Chords:        return "Chords";
        case InstrumentType::Bass:          return "Bass";
        case InstrumentType::Drums:         return "Drums";
        case InstrumentType::CounterMelody: return "Counter Melody";
        case InstrumentType::Arp:           return "Arp";
        case InstrumentType::Pad:           return "Pad";
        default:                            return "?";
    }
}

/** High-level musical parameters. These come from the UI controls AND/OR
    are filled in by the AI when it parses the prompt. */
struct MusicParams
{
    std::string root  = "F";       // root note name
    std::string scale = "minor";   // scale name (see MusicTheory)
    std::string genre = "House";
    double bpm        = 124.0;
    int    bars       = 4;
    int    octave     = 4;         // base octave for the lead register

    // 0..1 normalized creative dials
    float complexity      = 0.5f;
    float energy          = 0.6f;
    float swing           = 0.15f;
    float humanize        = 0.3f;
    float noteDensity     = 0.5f;
    float rhythmComplexity= 0.5f;
    float chordComplexity = 0.5f;

    // seed for reproducible-but-varied regeneration
    unsigned int seed = 0;
};

/** One note event, pre-MIDI, in beats. */
struct NoteEvent
{
    double startBeats = 0.0;
    double lengthBeats = 1.0;
    int    pitch = 60;
    float  velocity = 0.8f; // 0..1
};

/** The result of generating one instrument part. */
struct GeneratedPart
{
    InstrumentType type = InstrumentType::Melody;
    std::vector<NoteEvent> notes;
    bool locked = false;
    bool muted  = false;
};

} // namespace aimidi
