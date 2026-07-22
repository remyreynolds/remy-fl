#pragma once

#include "MusicInstructions.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace aimidi
{
/** Role inferred from MIDI filename / folder for auto-sort. */
enum class MidiRole
{
    Melody = 0,
    Chords,
    Bass,
    Arp,
    Pad,
    CounterMelody,
    Drums,
    Other,
    NumRoles
};

inline const char* toString (MidiRole r)
{
    switch (r)
    {
        case MidiRole::Melody:        return "Melody";
        case MidiRole::Chords:        return "Chords";
        case MidiRole::Bass:          return "Bass";
        case MidiRole::Arp:           return "Arp";
        case MidiRole::Pad:           return "Pad";
        case MidiRole::CounterMelody: return "Counter";
        case MidiRole::Drums:         return "Drums";
        case MidiRole::Other:         return "Other";
        default:                      return "?";
    }
}

inline MidiRole midiRoleForInstrument (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::Melody:        return MidiRole::Melody;
        case InstrumentType::Chords:        return MidiRole::Chords;
        case InstrumentType::Bass:          return MidiRole::Bass;
        case InstrumentType::Arp:           return MidiRole::Arp;
        case InstrumentType::Pad:           return MidiRole::Pad;
        case InstrumentType::CounterMelody: return MidiRole::CounterMelody;
        case InstrumentType::Drums:         return MidiRole::Drums;
        default:                            return MidiRole::Other;
    }
}

struct MidiEntry
{
    juce::String id;       // relative path under midi/
    juce::String name;
    juce::String packName;
    MidiRole role = MidiRole::Other;
    juce::File file;
};

/** Local MIDI loop packs for pitched instruments (and optional drums).
    Stored under ~/Library/Application Support/AIMidiGen/midi/ */
class MidiLibrary
{
public:
    MidiLibrary();

    void reload();
    juce::File folder() const;
    juce::String statusLine() const;
    int size() const { return (int) entries.size(); }

    const std::vector<MidiEntry>& all() const { return entries; }
    std::vector<const MidiEntry*> loopsFor (MidiRole role) const;
    std::vector<const MidiEntry*> loopsForInstrument (InstrumentType type) const;
    std::vector<const MidiEntry*> loopsForInstrumentInPack (InstrumentType type,
                                                            const juce::String& packName) const;
    std::vector<const MidiEntry*> loopsForPack (const juce::String& packName) const;
    juce::StringArray packNames() const;
    /** e.g. "Kit_1", "Kit_2" from filenames like Kit_1_Bass_120BPM_G.mid */
    juce::StringArray kitBundleNames (const juce::String& packName = {}) const;

    int importPackFolder (const juce::File& folder, juce::String* errorOut = nullptr);
    int importFiles (const juce::Array<juce::File>& files, const juce::String& packName,
                     juce::String* errorOut = nullptr);

    const MidiEntry* findById (const juce::String& id) const;
    MidiEntry* findById (const juce::String& id);

    /** Read a .mid into a GeneratedPart (beats relative to bar 0). */
    static bool loadFileToPart (const juce::File& file, InstrumentType type,
                                GeneratedPart& out, juce::String* errorOut = nullptr);

    static MidiRole classifyName (const juce::String& fileOrFolderName);

private:
    std::vector<MidiEntry> entries;

    void ensureFolder() const;
    void scanPackDir (const juce::File& packDir, const juce::String& packName);
    static bool isMidiFile (const juce::File& f);
};

} // namespace aimidi
