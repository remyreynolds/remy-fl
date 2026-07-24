#pragma once

#include "MusicInstructions.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace aimidi
{
/** Role inferred from filename / folder (auto-sort). */
enum class SampleRole
{
    Kick = 0,
    Snare,
    Clap,
    ClosedHat,
    OpenHat,
    Perc,
    Bass,
    Keys,
    Lead,
    Pad,
    Other,
    NumRoles
};

inline const char* toString (SampleRole r)
{
    switch (r)
    {
        case SampleRole::Kick:      return "Kick";
        case SampleRole::Snare:     return "Snare";
        case SampleRole::Clap:      return "Clap";
        case SampleRole::ClosedHat: return "Closed Hat";
        case SampleRole::OpenHat:   return "Open Hat";
        case SampleRole::Perc:      return "Perc";
        case SampleRole::Bass:      return "Bass";
        case SampleRole::Keys:      return "Keys";
        case SampleRole::Lead:      return "Lead";
        case SampleRole::Pad:       return "Pad";
        case SampleRole::Other:     return "Other";
        default:                    return "?";
    }
}

/** Map drum kit piece → sample role for auto-assign. */
inline SampleRole roleForDrumPiece (DrumPiece dp)
{
    switch (dp)
    {
        case DrumPiece::Kick:      return SampleRole::Kick;
        case DrumPiece::Snare:     return SampleRole::Snare;
        case DrumPiece::Clap:      return SampleRole::Clap;
        case DrumPiece::ClosedHat: return SampleRole::ClosedHat;
        case DrumPiece::OpenHat:   return SampleRole::OpenHat;
        case DrumPiece::Ride:      // percussion pieces draw from the Perc pool
        case DrumPiece::Shaker:
        case DrumPiece::Rim:
        case DrumPiece::CongaHi:
        case DrumPiece::CongaLo:   return SampleRole::Perc;
        default:                   return SampleRole::Other;
    }
}

/** Map a pitched instrument lane → preferred sample role, so a Serum/other-synth
    one-shot pack renders sensible defaults per lane (user can still pick any
    sample from the pack — this only orders/pre-selects the menu). */
inline SampleRole roleForInstrumentType (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::Melody:         return SampleRole::Lead;
        case InstrumentType::CounterMelody:  return SampleRole::Lead;
        case InstrumentType::Arp:            return SampleRole::Lead;
        case InstrumentType::Chords:         return SampleRole::Keys;
        case InstrumentType::Bass:           return SampleRole::Bass;
        case InstrumentType::Pad:            return SampleRole::Pad;
        default:                             return SampleRole::Other;
    }
}

struct LoadedSample
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};

struct SampleEntry
{
    juce::String id;       // relative path under samples/
    juce::String name;     // display name
    juce::String packName; // pack folder name
    SampleRole role = SampleRole::Other;
    juce::File file;
    mutable std::shared_ptr<const LoadedSample> audio; // loaded lazily
};

/** Local sample packs for preview. Stored under
    ~/Library/Application Support/AIMidiGen/samples/ */
class SampleLibrary
{
public:
    SampleLibrary();

    void reload();

    juce::File folder() const;
    juce::String statusLine() const;
    int size() const { return (int) entries.size(); }

    const std::vector<SampleEntry>& all() const { return entries; }
    std::vector<const SampleEntry*> samplesFor (SampleRole role) const;
    std::vector<const SampleEntry*> samplesForPack (const juce::String& packName) const;
    juce::StringArray packNames() const;

    /** Copy a pack folder (or loose files) into the library and classify. */
    int importPackFolder (const juce::File& folder, juce::String* errorOut = nullptr);
    int importFiles (const juce::Array<juce::File>& files, const juce::String& packName,
                     juce::String* errorOut = nullptr);

    /** Load audio for an entry (cached). Returns nullptr on failure. */
    std::shared_ptr<const LoadedSample> ensureLoaded (const SampleEntry& entry) const;

    SampleEntry* findById (const juce::String& id);
    const SampleEntry* findById (const juce::String& id) const;

    /** First sample for role (optionally limited to one pack). */
    const SampleEntry* firstForRole (SampleRole role) const;
    const SampleEntry* firstForRoleInPack (SampleRole role, const juce::String& packName) const;

    static SampleRole classifyName (const juce::String& fileOrFolderName);

private:
    std::vector<SampleEntry> entries;
    mutable juce::AudioFormatManager formats;

    void ensureFolder() const;
    void scanPackDir (const juce::File& packDir, const juce::String& packName);
    static bool isAudioFile (const juce::File& f);
};

} // namespace aimidi
