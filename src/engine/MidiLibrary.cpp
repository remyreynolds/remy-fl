#include "MidiLibrary.h"
#include <algorithm>
#include <map>

namespace aimidi
{

MidiLibrary::MidiLibrary()
{
    ensureFolder();
    reload();
}

juce::File MidiLibrary::folder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AIMidiGen")
        .getChildFile ("midi");
}

void MidiLibrary::ensureFolder() const
{
    folder().createDirectory();
}

bool MidiLibrary::isMidiFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".mid" || ext == ".midi";
}

MidiRole MidiLibrary::classifyName (const juce::String& fileOrFolderName)
{
    const auto n = fileOrFolderName.toLowerCase();

    if (n.contains ("drum") || n.contains ("kit") && (n.contains ("kick") || n.contains ("hat"))
        || n.contains ("snare") || n.contains ("clap"))
        return MidiRole::Drums;
    if (n.contains ("chord") || n.contains ("keys") || n.contains ("piano")
        || n.contains ("stab") || n.contains ("harmony") || n.contains ("_chord"))
        return MidiRole::Chords;
    if (n.contains ("bass") || n.contains ("808") || n.contains ("sub") || n.contains ("_bass"))
        return MidiRole::Bass;
    if (n.contains ("arp") || n.contains ("arpeggio"))
        return MidiRole::Arp;
    if (n.contains ("pad") || n.contains ("atmos") || n.contains ("string"))
        return MidiRole::Pad;
    if (n.contains ("counter"))
        return MidiRole::CounterMelody;
    // Funky-house packs often label guitar as a second melodic line
    if (n.contains ("guitar"))
        return MidiRole::CounterMelody;
    if (n.contains ("melody") || n.contains ("lead") || n.contains ("top")
        || n.contains ("riff") || n.contains ("hook") || n.contains ("_lead"))
        return MidiRole::Melody;

    return MidiRole::Other;
}

void MidiLibrary::scanPackDir (const juce::File& packDir, const juce::String& packName)
{
    for (const auto& entry : juce::RangedDirectoryIterator (packDir, true, "*",
                                                            juce::File::findFiles))
    {
        const auto f = entry.getFile();
        if (! isMidiFile (f))
            continue;

        MidiEntry m;
        m.file = f;
        m.packName = packName;
        m.name = f.getFileNameWithoutExtension();
        m.id = packName + "/" + f.getRelativePathFrom (packDir).replaceCharacter ('\\', '/');

        const auto parentRole = classifyName (f.getParentDirectory().getFileName());
        const auto fileRole = classifyName (m.name);
        m.role = (fileRole != MidiRole::Other) ? fileRole
               : (parentRole != MidiRole::Other) ? parentRole
               : MidiRole::Other;

        entries.push_back (std::move (m));
    }
}

void MidiLibrary::reload()
{
    entries.clear();
    ensureFolder();

    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false, "*",
                                                            juce::File::findDirectories))
        scanPackDir (entry.getFile(), entry.getFile().getFileName());

    juce::Array<juce::File> loose;
    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false, "*",
                                                            juce::File::findFiles))
        if (isMidiFile (entry.getFile()))
            loose.add (entry.getFile());

    for (auto& f : loose)
    {
        MidiEntry m;
        m.file = f;
        m.packName = "Loose";
        m.name = f.getFileNameWithoutExtension();
        m.id = "Loose/" + f.getFileName();
        m.role = classifyName (m.name);
        entries.push_back (std::move (m));
    }

    std::sort (entries.begin(), entries.end(),
               [] (const MidiEntry& a, const MidiEntry& b)
               {
                   if ((int) a.role != (int) b.role) return (int) a.role < (int) b.role;
                   if (a.packName != b.packName) return a.packName < b.packName;
                   return a.name < b.name;
               });
}

juce::String MidiLibrary::statusLine() const
{
    if (entries.empty())
        return "No MIDI packs";
    auto packs = packNames();
    juce::String s;
    s << juce::String ((int) entries.size()) << " MIDI loops";
    if (packs.size() == 1)
        s << " · " << packs[0];
    else if (packs.size() > 1)
        s << " · " << packs.size() << " packs";
    return s;
}

juce::StringArray MidiLibrary::packNames() const
{
    juce::StringArray names;
    for (auto& e : entries)
        if (e.packName.isNotEmpty())
            names.addIfNotAlreadyThere (e.packName);
    names.sort (true);
    return names;
}

juce::StringArray MidiLibrary::kitBundleNames (const juce::String& packName) const
{
    juce::StringArray kits;
    for (auto& e : entries)
    {
        if (packName.isNotEmpty() && e.packName != packName)
            continue;
        // Match Kit_1, Kit_12, etc. at start of filename
        if (e.name.startsWithIgnoreCase ("Kit_"))
        {
            auto rest = e.name.substring (4);
            juce::String num;
            for (int i = 0; i < rest.length() && juce::CharacterFunctions::isDigit (rest[i]); ++i)
                num << rest[i];
            if (num.isNotEmpty())
                kits.addIfNotAlreadyThere ("Kit_" + num);
        }
    }
    kits.sort (false);
    return kits;
}

std::vector<const MidiEntry*> MidiLibrary::loopsForPack (const juce::String& packName) const
{
    std::vector<const MidiEntry*> out;
    for (auto& e : entries)
        if (packName.isEmpty() || e.packName == packName)
            out.push_back (&e);
    return out;
}

std::vector<const MidiEntry*> MidiLibrary::loopsFor (MidiRole role) const
{
    std::vector<const MidiEntry*> out;
    for (auto& e : entries)
        if (e.role == role)
            out.push_back (&e);
    return out;
}

std::vector<const MidiEntry*> MidiLibrary::loopsForInstrument (InstrumentType type) const
{
    return loopsForInstrumentInPack (type, {});
}

std::vector<const MidiEntry*> MidiLibrary::loopsForInstrumentInPack (InstrumentType type,
                                                                    const juce::String& packName) const
{
    const auto role = midiRoleForInstrument (type);
    std::vector<const MidiEntry*> preferred;
    std::vector<const MidiEntry*> rest;
    for (auto& e : entries)
    {
        if (packName.isNotEmpty() && e.packName != packName)
            continue;
        if (e.role == role)
            preferred.push_back (&e);
        else
            rest.push_back (&e);
    }
    preferred.insert (preferred.end(), rest.begin(), rest.end());
    if (preferred.empty() && packName.isNotEmpty())
        return loopsForInstrumentInPack (type, {}); // fall back to all packs
    return preferred;
}

MidiEntry* MidiLibrary::findById (const juce::String& id)
{
    for (auto& e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

const MidiEntry* MidiLibrary::findById (const juce::String& id) const
{
    for (auto& e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

int MidiLibrary::importPackFolder (const juce::File& srcFolder, juce::String* errorOut)
{
    if (! srcFolder.isDirectory())
    {
        if (errorOut) *errorOut = "Not a folder.";
        return 0;
    }

    ensureFolder();
    auto packName = srcFolder.getFileName();
    if (packName.isEmpty()) packName = "MIDI Pack";

    auto dest = folder().getChildFile (packName);
    if (dest.exists())
        dest.deleteRecursively();
    if (! srcFolder.copyDirectoryTo (dest))
    {
        if (errorOut) *errorOut = "Could not copy MIDI pack folder.";
        return 0;
    }

    reload();
    int n = 0;
    for (auto& e : entries)
        if (e.packName == packName)
            ++n;
    if (n == 0 && errorOut)
        *errorOut = "No .mid / .midi files found in that folder.";
    return n;
}

int MidiLibrary::importFiles (const juce::Array<juce::File>& files, const juce::String& packName,
                              juce::String* errorOut)
{
    ensureFolder();
    auto destDir = folder().getChildFile (packName.isNotEmpty() ? packName : "Loose");
    destDir.createDirectory();

    int n = 0;
    for (auto& f : files)
    {
        if (! isMidiFile (f)) continue;
        auto dest = destDir.getChildFile (f.getFileName());
        if (f.copyFileTo (dest))
            ++n;
    }
    reload();
    if (n == 0 && errorOut)
        *errorOut = "No MIDI files imported.";
    return n;
}

bool MidiLibrary::loadFileToPart (const juce::File& file, InstrumentType type,
                                  GeneratedPart& out, juce::String* errorOut)
{
    if (! file.existsAsFile())
    {
        if (errorOut) *errorOut = "MIDI file missing.";
        return false;
    }

    juce::FileInputStream stream (file);
    if (! stream.openedOk())
    {
        if (errorOut) *errorOut = "Could not open MIDI file.";
        return false;
    }

    juce::MidiFile mf;
    if (! mf.readFrom (stream))
    {
        if (errorOut) *errorOut = "Invalid MIDI file.";
        return false;
    }

    short timeFormat = mf.getTimeFormat();
    const double tpq = timeFormat > 0 ? (double) timeFormat : 960.0;

    out = {};
    out.type = type;
    out.notes.clear();

    struct OpenNote { double startBeats; float velocity; };
    std::map<int, OpenNote> open; // pitch → start

    auto flushNote = [&] (int pitch, double endBeats)
    {
        auto it = open.find (pitch);
        if (it == open.end()) return;
        NoteEvent n;
        n.pitch = pitch;
        n.startBeats = it->second.startBeats;
        n.lengthBeats = juce::jmax (0.05, endBeats - it->second.startBeats);
        n.velocity = it->second.velocity;
        out.notes.push_back (n);
        open.erase (it);
    };

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        auto* seq = mf.getTrack (t);
        if (seq == nullptr) continue;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            auto msg = seq->getEventPointer (i)->message;
            const double beats = seq->getEventTime (i) / tpq;

            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                const int pitch = msg.getNoteNumber();
                if (open.count (pitch))
                    flushNote (pitch, beats);
                open[pitch] = { beats, msg.getVelocity() / 127.0f };
            }
            else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
            {
                flushNote (msg.getNoteNumber(), beats);
            }
        }
    }

    // Close any hanging notes (~1 beat)
    for (auto it = open.begin(); it != open.end();)
    {
        NoteEvent n;
        n.pitch = it->first;
        n.startBeats = it->second.startBeats;
        n.lengthBeats = 1.0;
        n.velocity = it->second.velocity;
        out.notes.push_back (n);
        it = open.erase (it);
    }

    std::sort (out.notes.begin(), out.notes.end(),
               [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeats < b.startBeats; });

    if (out.notes.empty())
    {
        if (errorOut) *errorOut = "MIDI file has no notes.";
        return false;
    }
    return true;
}

} // namespace aimidi
