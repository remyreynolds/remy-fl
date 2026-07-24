#include "SampleLibrary.h"
#include <algorithm>

namespace aimidi
{

SampleLibrary::SampleLibrary()
{
    formats.registerBasicFormats();
    ensureFolder();
    reload();
}

juce::File SampleLibrary::folder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AIMidiGen")
        .getChildFile ("samples");
}

void SampleLibrary::ensureFolder() const
{
    folder().createDirectory();
}

bool SampleLibrary::isAudioFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".ogg" || ext == ".mp3";
}

SampleRole SampleLibrary::classifyName (const juce::String& fileOrFolderName)
{
    const auto n = fileOrFolderName.toLowerCase().trim();
    const auto token = n.upToFirstOccurrenceOf (" ", false, false)
                          .upToFirstOccurrenceOf ("(", false, false)
                          .upToFirstOccurrenceOf ("_", false, false)
                          .upToFirstOccurrenceOf ("-", false, false);

    // More specific first
    if (n.contains ("open") && (n.contains ("hat") || n.contains ("hh") || n.contains ("hihat")))
        return SampleRole::OpenHat;
    if (n.contains ("closed") && (n.contains ("hat") || n.contains ("hh") || n.contains ("hihat")))
        return SampleRole::ClosedHat;
    if (token == "oh" || n.contains ("oh_") || n.contains ("oh-") || n.contains ("_oh")
        || n.contains ("openhat") || n.startsWith ("oh ") || n.startsWith ("oh("))
        return SampleRole::OpenHat;
    if (token == "hh" || token == "ch" || n.contains ("ch_") || n.contains ("ch-")
        || n.contains ("_ch") || n.contains ("closedhat") || n.contains ("hihat")
        || n.contains ("hi-hat") || n.contains ("hi_hat") || n.contains ("hat")
        || n.contains ("hh_") || n.startsWith ("hh ") || n.startsWith ("hh("))
        return SampleRole::ClosedHat;

    if (n.contains ("kick") || token == "kick" || n.contains ("bd_") || n.contains ("bassdrum")
        || n.contains ("bass drum"))
        return SampleRole::Kick;
    if (n.contains ("snare") || token == "snare" || n.contains ("sd_") || n.contains ("snr"))
        return SampleRole::Snare;
    if (n.contains ("clap") || token == "clap" || n.contains ("clp"))
        return SampleRole::Clap;
    if (n.contains ("perc") || n.contains ("rim") || n.contains ("tom") || n.contains ("shaker")
        || n.contains ("conga") || n.contains ("bongo") || n.contains ("cowbell")
        || n.contains ("tamb") || n.contains ("ride"))
        return SampleRole::Perc;

    if (n.contains ("808") || n.contains ("sub") || n.contains ("bass"))
        return SampleRole::Bass;
    if (n.contains ("piano") || n.contains ("keys") || n.contains ("rhodes")
        || n.contains ("chord") || n.contains ("epiano") || n.contains ("e-piano"))
        return SampleRole::Keys;
    if (n.contains ("arp") || n.contains ("growl") || n.contains ("wobble") || n.contains ("grow")
        || n.contains ("acid") || n.contains ("screech") || n.contains ("laser"))
        return SampleRole::Lead;
    if (n.contains ("lead") || n.contains ("synth") || n.contains ("pluck") || n.contains ("stab"))
        return SampleRole::Lead;

    if (n.contains ("pad") || n.contains ("atmos") || n.contains ("ambient") || n.contains ("drone")
        || n.contains ("swell") || n.contains ("airy"))
        return SampleRole::Pad;

    return SampleRole::Other;
}

void SampleLibrary::scanPackDir (const juce::File& packDir, const juce::String& packName)
{
    for (const auto& entry : juce::RangedDirectoryIterator (packDir, true, "*",
                                                            juce::File::findFiles))
    {
        const auto f = entry.getFile();
        if (! isAudioFile (f))
            continue;

        SampleEntry s;
        s.file = f;
        s.packName = packName;
        s.name = f.getFileNameWithoutExtension();
        s.id = packName + "/" + f.getRelativePathFrom (packDir).replaceCharacter ('\\', '/');

        // Prefer folder name hints (e.g. pack/Kicks/foo.wav)
        const auto parentRole = classifyName (f.getParentDirectory().getFileName());
        const auto fileRole = classifyName (s.name);
        s.role = (fileRole != SampleRole::Other) ? fileRole
               : (parentRole != SampleRole::Other) ? parentRole
               : SampleRole::Other;

        entries.push_back (std::move (s));
    }
}

void SampleLibrary::reload()
{
    entries.clear();
    ensureFolder();

    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false, "*",
                                                            juce::File::findDirectories))
    {
        const auto packDir = entry.getFile();
        scanPackDir (packDir, packDir.getFileName());
    }

    // Loose files at root → "Loose" pack
    juce::Array<juce::File> loose;
    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false, "*",
                                                            juce::File::findFiles))
    {
        if (isAudioFile (entry.getFile()))
            loose.add (entry.getFile());
    }
    if (loose.size() > 0)
    {
        for (auto& f : loose)
        {
            SampleEntry s;
            s.file = f;
            s.packName = "Loose";
            s.name = f.getFileNameWithoutExtension();
            s.id = "Loose/" + f.getFileName();
            s.role = classifyName (s.name);
            entries.push_back (std::move (s));
        }
    }

    std::sort (entries.begin(), entries.end(),
               [] (const SampleEntry& a, const SampleEntry& b)
               {
                   if ((int) a.role != (int) b.role) return (int) a.role < (int) b.role;
                   if (a.packName != b.packName) return a.packName < b.packName;
                   return a.name < b.name;
               });
}

juce::String SampleLibrary::statusLine() const
{
    if (entries.empty())
        return "No sound packs — click Add sounds";

    auto packs = packNames();
    juce::String s;
    s << juce::String ((int) entries.size()) << " sounds";
    if (packs.size() == 1)
        s << " · " << packs[0];
    else if (packs.size() > 1)
        s << " · " << packs.size() << " packs";
    return s;
}

juce::StringArray SampleLibrary::packNames() const
{
    juce::StringArray names;
    for (auto& e : entries)
        if (e.packName.isNotEmpty())
            names.addIfNotAlreadyThere (e.packName);
    names.sort (true);
    return names;
}

std::vector<const SampleEntry*> SampleLibrary::samplesFor (SampleRole role) const
{
    std::vector<const SampleEntry*> out;
    for (auto& e : entries)
        if (e.role == role)
            out.push_back (&e);
    return out;
}

std::vector<const SampleEntry*> SampleLibrary::samplesForPack (const juce::String& packName) const
{
    std::vector<const SampleEntry*> out;
    if (packName.isEmpty())
    {
        for (auto& e : entries)
            out.push_back (&e);
        return out;
    }
    for (auto& e : entries)
        if (e.packName == packName)
            out.push_back (&e);
    return out;
}

const SampleEntry* SampleLibrary::firstForRole (SampleRole role) const
{
    for (auto& e : entries)
        if (e.role == role)
            return &e;
    return nullptr;
}

const SampleEntry* SampleLibrary::firstForRoleInPack (SampleRole role, const juce::String& packName) const
{
    for (auto& e : entries)
        if (e.role == role && (packName.isEmpty() || e.packName == packName))
            return &e;
    return nullptr;
}

SampleEntry* SampleLibrary::findById (const juce::String& id)
{
    for (auto& e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

const SampleEntry* SampleLibrary::findById (const juce::String& id) const
{
    for (auto& e : entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

std::shared_ptr<const LoadedSample> SampleLibrary::ensureLoaded (const SampleEntry& entry) const
{
    if (entry.audio != nullptr)
        return entry.audio;

    if (! entry.file.existsAsFile())
        return nullptr;

    // File-based reader first (most reliable for pack WAVs on macOS).
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.file));
    if (reader == nullptr)
    {
        if (auto stream = std::unique_ptr<juce::FileInputStream> (entry.file.createInputStream()))
            if (stream->openedOk())
                reader.reset (formats.createReaderFor (std::unique_ptr<juce::InputStream> (stream.release())));
    }
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return nullptr;

    const int n = (int) juce::jmin ((juce::int64) reader->lengthInSamples, (juce::int64) 48000 * 8); // cap 8s
    const int srcCh = (int) juce::jmax (1, (int) reader->numChannels);
    const int dstCh = juce::jmin (2, srcCh);

    juce::AudioBuffer<float> temp (srcCh, n);
    reader->read (&temp, 0, n, 0, true, true);

    auto loaded = std::make_shared<LoadedSample>();
    loaded->sampleRate = reader->sampleRate;
    loaded->buffer.setSize (dstCh, n, false, true, true);
    for (int c = 0; c < dstCh; ++c)
        loaded->buffer.copyFrom (c, 0, temp, c, 0, n);

    float peak = 0.0f;
    for (int c = 0; c < loaded->buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, loaded->buffer.getMagnitude (c, 0, n));

    if (peak < 1.0e-7f)
        return nullptr;

    // Always normalize pack samples so preview level is consistent and audible.
    loaded->buffer.applyGain (0.9f / peak);

    entry.audio = loaded;
    return loaded;
}

int SampleLibrary::importPackFolder (const juce::File& srcFolder, juce::String* errorOut)
{
    if (! srcFolder.isDirectory())
    {
        if (errorOut) *errorOut = "Not a folder.";
        return 0;
    }

    ensureFolder();
    auto packName = srcFolder.getFileName();
    if (packName.isEmpty()) packName = "Pack";

    auto dest = folder().getChildFile (packName);
    int suffix = 2;
    while (dest.exists())
    {
        dest = folder().getChildFile (packName + "-" + juce::String (suffix++));
    }

    if (! srcFolder.copyDirectoryTo (dest))
    {
        if (errorOut) *errorOut = "Could not copy pack into the app samples folder.";
        return 0;
    }

    // Drop non-audio junk optional — keep all, scan filters audio
    reload();

    int count = 0;
    for (auto& e : entries)
        if (e.packName == dest.getFileName())
            ++count;

    if (count == 0 && errorOut)
        *errorOut = "No audio files (.wav/.aiff/.flac/.ogg) found in that folder.";

    return count;
}

int SampleLibrary::importFiles (const juce::Array<juce::File>& files,
                                const juce::String& packName,
                                juce::String* errorOut)
{
    ensureFolder();
    auto name = packName.isNotEmpty() ? packName : "Import";
    auto dest = folder().getChildFile (name);
    dest.createDirectory();

    int imported = 0;
    for (auto& f : files)
    {
        if (! isAudioFile (f)) continue;
        auto target = dest.getChildFile (f.getFileName());
        if (f.copyFileTo (target))
            ++imported;
    }

    reload();
    if (imported == 0 && errorOut)
        *errorOut = "No supported audio files selected.";
    return imported;
}

} // namespace aimidi
