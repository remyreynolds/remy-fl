#pragma once

#include "MusicInstructions.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace aimidi
{
enum class GenreMode
{
    House = 0,
    HipHop,
    Pop,
    Classical,
    Techno,
    NumModes
};

enum class PartTimbre
{
    SoftPiano = 0,
    HousePiano,
    ClassicPiano,
    ChordSynth,
    BrightLead,
    WarmPad,
    Pluck,
    Sub808,
    HouseBass,
    AcousticBass,
    Strings,
    SuperSaw,    // Serum-style detuned 7-voice unison saw through a resonant LP
    FilterBass,  // classic house bass: saw+sub through envelope-swept filter
    OrganBass,   // M1-style house organ bass
    NumTimbres
};

enum class DrumKitStyle
{
    House = 0,
    HipHop,
    Pop,
    Classical,
    Techno,
    NumStyles
};

inline const char* toString (GenreMode g)
{
    switch (g)
    {
        case GenreMode::House:     return "House";
        case GenreMode::HipHop:    return "Hip-Hop";
        case GenreMode::Pop:       return "Pop";
        case GenreMode::Classical: return "Classical";
        case GenreMode::Techno:    return "Techno";
        default:                   return "?";
    }
}

inline const char* toString (PartTimbre t)
{
    switch (t)
    {
        case PartTimbre::SoftPiano:    return "Soft Piano";
        case PartTimbre::HousePiano:   return "House Piano";
        case PartTimbre::ClassicPiano: return "Classic Piano";
        case PartTimbre::ChordSynth:   return "Chord Synth";
        case PartTimbre::BrightLead:   return "Bright Lead";
        case PartTimbre::WarmPad:      return "Warm Pad";
        case PartTimbre::Pluck:        return "Pluck";
        case PartTimbre::Sub808:       return "Sub 808";
        case PartTimbre::HouseBass:    return "House Bass";
        case PartTimbre::AcousticBass: return "Acoustic Bass";
        case PartTimbre::Strings:      return "Strings";
        case PartTimbre::SuperSaw:     return "Super Saw";
        case PartTimbre::FilterBass:   return "Filter Bass";
        case PartTimbre::OrganBass:    return "Organ Bass";
        default:                       return "?";
    }
}

inline const char* toString (DrumKitStyle s)
{
    switch (s)
    {
        case DrumKitStyle::House:     return "House Kit";
        case DrumKitStyle::HipHop:    return "Hip-Hop Kit";
        case DrumKitStyle::Pop:       return "Pop Kit";
        case DrumKitStyle::Classical: return "Soft Kit";
        case DrumKitStyle::Techno:    return "Techno Kit";
        default:                      return "?";
    }
}

inline GenreMode genreModeFromIndex (int i)
{
    return (GenreMode) juce::jlimit (0, (int) GenreMode::NumModes - 1, i);
}

inline PartTimbre partTimbreFromIndex (int i)
{
    return (PartTimbre) juce::jlimit (0, (int) PartTimbre::NumTimbres - 1, i);
}

/** Default + allowed timbres for a genre. */
struct GenreTimbreMap
{
    DrumKitStyle drumKit = DrumKitStyle::House;
    std::array<PartTimbre, (size_t) InstrumentType::NumTypes> defaults {};
    std::array<std::vector<PartTimbre>, (size_t) InstrumentType::NumTypes> variants {};
};

/** Preview mix gains 0..1 from production knowledge (hats hot in house, etc.). */
struct GenreMixMap
{
    std::array<float, (size_t) InstrumentType::NumTypes> partGain {};
    std::array<float, (size_t) DrumPiece::NumPieces> drumGain {};
};

GenreTimbreMap defaultsFor (GenreMode genre);
GenreMixMap mixDefaultsFor (GenreMode genre);

/** Best-effort genre from chat / style text. Returns false if unclear. */
bool detectGenreFromText (const juce::String& text, GenreMode& out);

/** MIDI channel (1–6, 10) → InstrumentType for pitched parts. */
InstrumentType instrumentTypeFromChannel (int channel);

} // namespace aimidi
