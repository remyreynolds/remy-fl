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

/** The "Drums" instrument is actually a kit of several independent pieces.
    Producers expect to generate/lock/mute/drag each of these separately
    (you don't want your hi-hats locked just because you liked the kick),
    so the engine and UI treat them as their own mini-parts rather than one
    monolithic blob of notes. */
enum class DrumPiece
{
    Kick, Snare, Clap, ClosedHat, OpenHat,
    Ride, Shaker, Rim, CongaHi, CongaLo,   // percussion — essential for modern
    NumPieces                              // afro/tech house grooves
};

inline const char* toString (DrumPiece p)
{
    switch (p)
    {
        case DrumPiece::Kick:      return "Kick";
        case DrumPiece::Snare:     return "Snare";
        case DrumPiece::Clap:      return "Clap";
        case DrumPiece::ClosedHat: return "Closed Hat";
        case DrumPiece::OpenHat:   return "Open Hat";
        case DrumPiece::Ride:      return "Ride";
        case DrumPiece::Shaker:    return "Shaker";
        case DrumPiece::Rim:       return "Rim";
        case DrumPiece::CongaHi:   return "Conga Hi";
        case DrumPiece::CongaLo:   return "Conga Lo";
        default:                   return "?";
    }
}

/** General MIDI drum-map note number for each piece (channel 10). */
inline int drumPieceMidiNote (DrumPiece p)
{
    switch (p)
    {
        case DrumPiece::Kick:      return 36;
        case DrumPiece::Snare:     return 38;
        case DrumPiece::Clap:      return 39;
        case DrumPiece::ClosedHat: return 42;
        case DrumPiece::OpenHat:   return 46;
        case DrumPiece::Ride:      return 51;
        case DrumPiece::Shaker:    return 70; // GM maracas
        case DrumPiece::Rim:       return 37; // GM side stick
        case DrumPiece::CongaHi:   return 63; // GM open hi conga
        case DrumPiece::CongaLo:   return 64; // GM low conga
        default:                   return 36;
    }
}

/** High-level musical parameters. These come from the UI controls AND/OR
    are filled in by the AI when it parses the prompt. */
struct MusicParams
{
    std::string root  = "F";       // root note name
    std::string scale = "minor";   // scale name (see MusicTheory)
    std::string genre = "Tech House"; // must name a StylePresets entry
    double bpm        = 126.0;
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
