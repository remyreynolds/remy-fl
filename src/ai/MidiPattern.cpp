#include "MidiPattern.h"
#include <cmath>

namespace aimidi
{

juce::String buildClaudeMidiSystemPrompt()
{
    return
R"(You are **Groovewright** in GENERATE mode. Obey the MASTER SYSTEM PROMPT (house brain) and MUSIC THEORY REFERENCES.

Return ONLY a single JSON object. No markdown, no code fences, no prose.

Exact schema:
{
  "bpm": 124,
  "key": "F minor",
  "instrument": "chords",
  "bars": 8,
  "timeSignature": "4/4",
  "notes": [
    { "pitch": "F3", "startBeat": 0.0, "durationBeats": 0.4, "velocity": 92 },
    { "pitch": "Ab3", "startBeat": 0.0, "durationBeats": 0.4, "velocity": 88 },
    { "pitch": "C4", "startBeat": 0.0, "durationBeats": 0.4, "velocity": 90 }
  ]
}

Hard rules:
- pitch = note name + octave (A1, C#2, Bb3) — never raw MIDI integers.
- startBeat >= 0 (beats). durationBeats > 0. velocity 1–127.
- instrument: melody | chords | bass | drums | arp | pad | counter melody.
- Chords: full voicings — multiple notes with the SAME startBeat (min7/min9 etc.).
- House DNA: groove, loopability, leave space, swing via slightly late offbeat startBeats.
- Bass E1–E2, dodge kick downbeats unless sub-hold; chords C3–C5; melody motifs with breath.
- Stay in project key/BPM/bars when locked. bars ≤ 16 (prefer 4 or 8).
- Style of references only — never clone copyrighted riffs.
- JSON only.)";
}

//==============================================================================
int noteNameToMidi (const juce::String& name, juce::String* errorOut)
{
    auto s = name.trim();
    if (s.isEmpty())
    {
        if (errorOut) *errorOut = "Empty pitch name.";
        return -1;
    }

    const juce::juce_wchar letter = (juce::juce_wchar) juce::CharacterFunctions::toUpperCase (s[0]);
    static const int pcForLetter[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (letter < 'A' || letter > 'G')
    {
        if (errorOut) *errorOut = "Pitch must start with A–G: \"" + name + "\"";
        return -1;
    }
    int pc = pcForLetter[(int) (letter - 'A')];

    int i = 1;
    if (i < s.length() && s[i] == '#')
    {
        pc = (pc + 1) % 12;
        ++i;
    }
    else if (i < s.length() && s[i] == 'b')
    {
        pc = (pc + 11) % 12;
        ++i;
    }

    // After optional accidental, parse octave (may be negative, e.g. C-1)
    if (i >= s.length())
    {
        if (errorOut) *errorOut = "Pitch missing octave: \"" + name + "\"";
        return -1;
    }

    const auto octStr = s.substring (i).trim();
    if (! octStr.containsOnly ("-0123456789") || octStr == "-" || octStr.isEmpty())
    {
        if (errorOut) *errorOut = "Invalid octave in pitch: \"" + name + "\"";
        return -1;
    }

    const int octave = octStr.getIntValue();
    // MIDI: C-1 = 0, C4 = 60 → midi = (octave + 1) * 12 + pc
    const int midi = (octave + 1) * 12 + pc;
    if (midi < 0 || midi > 127)
    {
        if (errorOut) *errorOut = "Pitch out of MIDI range: \"" + name + "\"";
        return -1;
    }
    return midi;
}

//==============================================================================
namespace
{
juce::String stripCodeFences (juce::String text)
{
    text = text.trim();
    if (text.startsWith ("```"))
    {
        // ```json\n...\n```
        auto firstNl = text.indexOfChar ('\n');
        if (firstNl >= 0) text = text.substring (firstNl + 1);
        if (text.endsWith ("```"))
            text = text.dropLastCharacters (3);
        text = text.trim();
    }
    return text;
}

/** Pull the outermost JSON object even if Claude adds stray prose. */
juce::String extractJsonObject (juce::String text)
{
    text = stripCodeFences (text);
    const int start = text.indexOfChar ('{');
    const int end   = text.lastIndexOfChar ('}');
    if (start >= 0 && end > start)
        return text.substring (start, end + 1).trim();
    return text;
}

bool parseNoteObject (const juce::var& n, PatternNote& out, juce::String& error)
{
    if (! n.isObject())
    {
        error = "Each note must be a JSON object.";
        return false;
    }

    const auto pitch = n.getProperty ("pitch", {}).toString().trim();
    juce::String pitchErr;
    const int midi = noteNameToMidi (pitch, &pitchErr);
    if (midi < 0)
    {
        error = pitchErr.isNotEmpty() ? pitchErr : ("Invalid pitch: \"" + pitch + "\"");
        return false;
    }

    if (! n.hasProperty ("startBeat"))
    {
        error = "Note missing startBeat.";
        return false;
    }
    if (! n.hasProperty ("durationBeats"))
    {
        error = "Note missing durationBeats.";
        return false;
    }

    const double start = (double) n.getProperty ("startBeat", -1.0);
    const double dur   = (double) n.getProperty ("durationBeats", 0.0);
    int vel = (int) n.getProperty ("velocity", 100);

    if (start < 0.0)
    {
        error = "startBeat must be >= 0.";
        return false;
    }
    if (! (dur > 0.0) || ! std::isfinite (dur) || ! std::isfinite (start))
    {
        error = "durationBeats must be a finite number > 0.";
        return false;
    }
    if (vel < 1 || vel > 127)
    {
        error = "velocity must be 1–127 (got " + juce::String (vel) + ").";
        return false;
    }

    out.pitchName = pitch;
    out.pitchMidi = midi;
    out.startBeat = start;
    out.durationBeats = dur;
    out.velocity = vel;
    return true;
}
} // namespace

MidiPatternParseResult parseClaudeMidiJson (const juce::String& jsonText)
{
    MidiPatternParseResult result;
    const auto text = extractJsonObject (jsonText);

    auto root = juce::JSON::parse (text);
    if (! root.isObject())
    {
        result.error = "Claude output was not valid JSON. Expected a single JSON object.";
        return result;
    }

    auto& p = result.pattern;

    if (! root.hasProperty ("bpm"))
    {
        result.error = "Missing \"bpm\".";
        return result;
    }
    p.bpm = (int) root.getProperty ("bpm", 0);
    if (p.bpm < 60 || p.bpm > 180)
    {
        result.error = "bpm must be between 60 and 180 (got " + juce::String (p.bpm) + ").";
        return result;
    }

    if (! root.hasProperty ("bars"))
    {
        result.error = "Missing \"bars\".";
        return result;
    }
    p.bars = (int) root.getProperty ("bars", 0);
    if (p.bars < 1 || p.bars > 16)
    {
        result.error = "bars must be between 1 and 16 (got " + juce::String (p.bars) + ").";
        return result;
    }

    p.key = root.getProperty ("key", "C minor").toString().trim();
    if (p.key.isEmpty()) p.key = "C minor";

    p.instrument = root.getProperty ("instrument", "bass").toString().trim();
    if (p.instrument.isEmpty()) p.instrument = "bass";

    p.timeSignature = root.getProperty ("timeSignature", "4/4").toString().trim();
    if (p.timeSignature.isEmpty()) p.timeSignature = "4/4";

    auto notesVar = root.getProperty ("notes", {});
    auto* arr = notesVar.getArray();
    if (arr == nullptr || arr->isEmpty())
    {
        result.error = "\"notes\" must be a non-empty array.";
        return result;
    }

    p.notes.reserve ((size_t) arr->size());
    for (int i = 0; i < arr->size(); ++i)
    {
        PatternNote note;
        juce::String err;
        if (! parseNoteObject (arr->getReference (i), note, err))
        {
            result.error = "notes[" + juce::String (i) + "]: " + err;
            return result;
        }
        p.notes.push_back (note);
    }

    result.ok = true;
    return result;
}

//==============================================================================
InstrumentType instrumentTypeFromName (const juce::String& name)
{
    const auto s = name.toLowerCase();
    if (s.contains ("drum") || s.contains ("kit") || s.contains ("perc"))
        return InstrumentType::Drums;
    if (s.contains ("chord") || s.contains ("progression") || s.contains ("harmon"))
        return InstrumentType::Chords;
    if (s.contains ("bass") || s.contains ("808"))
        return InstrumentType::Bass;
    if (s.contains ("counter"))
        return InstrumentType::CounterMelody;
    if (s.contains ("arp"))
        return InstrumentType::Arp;
    if (s.contains ("pad"))
        return InstrumentType::Pad;
    if (s.contains ("melod") || s.contains ("lead"))
        return InstrumentType::Melody;
    return InstrumentType::Bass;
}

void applyKeyStringToParams (const juce::String& key, MusicParams& params)
{
    auto parts = juce::StringArray::fromTokens (key.trim(), " \t", "");
    if (parts.isEmpty()) return;

    params.root = parts[0].trim().toStdString();
    if (parts.size() >= 2)
    {
        auto scale = parts[1].trim().toLowerCase();
        if (scale.contains ("maj")) params.scale = "major";
        else if (scale.contains ("min") || scale.contains ("aeol")) params.scale = "minor";
        else if (scale.contains ("dor")) params.scale = "dorian";
        else if (scale.contains ("phry")) params.scale = "phrygian";
        else if (scale.contains ("lyd")) params.scale = "lydian";
        else if (scale.contains ("mix")) params.scale = "mixolydian";
        else params.scale = scale.toStdString();
    }
}

juce::MidiMessageSequence patternToSequence (const MidiPattern& pattern, int ppq)
{
    juce::MidiMessageSequence seq;
    const int ch = midiChannelFor (instrumentTypeFromName (pattern.instrument));

    for (const auto& n : pattern.notes)
    {
        const double onTick  = n.startBeat * (double) ppq;
        const double offTick = (n.startBeat + n.durationBeats) * (double) ppq;
        const auto vel = (juce::uint8) juce::jlimit (1, 127, n.velocity);
        seq.addEvent (juce::MidiMessage::noteOn  (ch, n.pitchMidi, vel), onTick);
        seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitchMidi),      offTick);
    }

    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

GeneratedPart patternToGeneratedPart (const MidiPattern& pattern)
{
    GeneratedPart part;
    part.type = instrumentTypeFromName (pattern.instrument);
    part.notes.reserve (pattern.notes.size());

    for (const auto& n : pattern.notes)
    {
        NoteEvent e;
        e.startBeats  = n.startBeat;
        e.lengthBeats = n.durationBeats;
        e.pitch       = n.pitchMidi;
        e.velocity    = (float) n.velocity / 127.0f;
        part.notes.push_back (e);
    }
    return part;
}

juce::String midiToNoteName (int midiNote)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int safe = juce::jlimit (0, 127, midiNote);
    const int pc = safe % 12;
    const int oct = safe / 12 - 1;
    return juce::String (names[pc]) + juce::String (oct);
}

juce::String serializePartAsMidiContext (const GeneratedPart& part,
                                         const MusicParams& params,
                                         int maxNotes)
{
    juce::StringArray noteLines;
    const int limit = juce::jmin (maxNotes, (int) part.notes.size());

    for (int i = 0; i < limit; ++i)
    {
        const auto& n = part.notes[(size_t) i];
        noteLines.add ("{\"pitch\":\"" + midiToNoteName (n.pitch)
                       + "\",\"start\":" + juce::String (n.startBeats, 3)
                       + ",\"dur\":" + juce::String (n.lengthBeats, 3)
                       + ",\"vel\":" + juce::String (juce::roundToInt (n.velocity * 127.0f))
                       + "}");
    }

    juce::String out;
    out << "{\"instrument\":\"" << toString (part.type) << "\""
        << ",\"key\":\"" << juce::String (formatKeyString (params)) << "\""
        << ",\"bpm\":" << juce::String (params.bpm, 1)
        << ",\"bars\":" << params.bars
        << ",\"noteCount\":" << (int) part.notes.size()
        << ",\"notes\":[" << noteLines.joinIntoString (",") << "]}";
    return out;
}

} // namespace aimidi
