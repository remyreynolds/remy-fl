#include "MidiPattern.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace aimidi
{

juce::String buildClaudeMidiSystemPrompt()
{
    return
R"(You are **Groovewright** in GENERATE mode. Obey the MASTER SYSTEM PROMPT (THE BRAIN) and MUSIC THEORY REFERENCES.
The BRAIN covers house sub-styles AND hip-hop/trap, techno, pop, and classical defaults —
apply whichever section matches the requested/current project genre. Never default to house
conventions (four-on-the-floor kicks, house chord palette, house swing) for a non-house genre.
When (and only when) the requested/current project genre IS house, the HOUSE BRAIN PDF
(parameter cards, progression libraries, drum libraries, velocity bands, swing ranges,
generation recipes) is the top-priority authority for that section — where it specifies a
value or range, it overrides your general music knowledge. Obey the MASTER SYSTEM PROMPT and
any secondary MUSIC THEORY REFERENCES only when they do not contradict the PDF.

Return ONLY a single JSON object. No markdown, no code fences, no prose.

## Single-part schema (one instrument)
{
  "bpm": 124,
  "key": "F minor",
  "instrument": "chords",
  "bars": 8,
  "timeSignature": "4/4",
  "notes": [
    { "pitch": "F3", "startBeat": 0.0, "durationBeats": 0.4, "velocity": 92 }
  ]
}

## Full-loop schema (PREFERRED when user asks for a loop / groove / track / arrangement)
{
  "bpm": 124,
  "key": "F minor",
  "bars": 8,
  "timeSignature": "4/4",
  "parts": [
    { "instrument": "chords", "notes": [ { "pitch": "F3", "startBeat": 0.0, "durationBeats": 1.0, "velocity": 88 } ] },
    { "instrument": "bass",   "notes": [ { "pitch": "F1", "startBeat": 0.5, "durationBeats": 0.4, "velocity": 105 } ] },
    { "instrument": "melody", "notes": [ { "pitch": "C5", "startBeat": 0.0, "durationBeats": 0.5, "velocity": 96 } ] },
    { "instrument": "drums",  "notes": [ { "pitch": "C2", "startBeat": 0.0, "durationBeats": 0.2, "velocity": 118 } ] }
  ]
}

When to use which:
- User asks for ONE role (bassline, chords only, melody, arp) → single-part schema.
- User asks for a loop / full groove / track / arrangement / "chords and bass" / "make everything" → parts[] with 2–5 instruments.
- Default full loop = chords + bass + melody + drums (hats/kick). Add arp or pad if asked.

Hard rules:
- pitch = note name + octave (A1, C#2, Bb3) — never raw MIDI integers.
- startBeat >= 0 (beats). durationBeats > 0. velocity 1–127.
- instrument: melody | chords | bass | drums | arp | pad | counter melody.
- Chords: full voicings — multiple notes with the SAME startBeat (min7/min9 etc.), when the
  genre calls for extended harmony; simpler triads for genres that want simpler harmony.
- Genre-authentic DNA: groove, loopability, leave space; swing/timing feel and drum pattern
  must match the requested genre — see MASTER SYSTEM PROMPT §5 genre defaults. Do NOT force
  a four-on-the-floor house pocket onto hip-hop/trap, techno-breaks, pop, or classical parts.
- Bass register/behavior follows genre (house/pop ~E1–E2 dodging kick downbeats unless a sub
  hold is wanted; hip-hop/trap 808 sub often glides/holds under the root); chords C3–C5;
  melody motifs with breath.
- Drums: GM-ish pitches ok (C2 kick, D2 snare/clap, F#2 closed hat, A#2 open hat). Pattern
  shape follows the genre: four-on-the-floor pocket for house/techno; syncopated kick+snare
  with hi-hat rolls for hip-hop/trap; the genre's native backbeat/arrangement otherwise.
- All parts share the same key/BPM/bars and must loop cleanly together.
- Stay in project key/BPM/bars when locked. bars ≤ 16 (prefer 4 or 8).
- Style of references only — never clone copyrighted riffs.
- Harmony metadata REQUIRED whenever chords (or a full loop) are generated:
  "progression": "i–VI–III–VII",
  "chords": ["Fm7", "Dbmaj7", "Eb7", "Cm7"]
  The chords[] symbols must match the chord-lane voicings (same bar count).
- JSON only.)";
}

std::vector<MidiPatternPart> MidiPattern::lanes() const
{
    if (! parts.empty())
        return parts;
    MidiPatternPart single;
    single.instrument = instrument;
    single.notes = notes;
    return { single };
}

int MidiPattern::totalNotes() const
{
    int n = 0;
    for (const auto& lane : lanes())
        n += (int) lane.notes.size();
    return n;
}

juce::String MidiPattern::instrumentSummary() const
{
    juce::StringArray names;
    for (const auto& lane : lanes())
        if (lane.instrument.isNotEmpty())
            names.addIfNotAlreadyThere (lane.instrument);
    return names.joinIntoString (", ");
}

juce::String chordProgressionFingerprint (const MidiPattern& pattern)
{
    // lanes() returns by value — keep the vector alive for the whole function
    // so the selected lane pointer cannot dangle.
    const auto lanes = pattern.lanes();
    const MidiPatternPart* chords = nullptr;
    for (const auto& lane : lanes)
        if (lane.instrument.containsIgnoreCase ("chord")
            || lane.instrument.containsIgnoreCase ("pad"))
        {
            chords = &lane;
            break;
        }

    if (chords == nullptr || chords->notes.empty())
        return {};

    // Quantize simultaneous voicings to a 1/16-note boundary, then describe
    // each chord by bass pitch class + pitch-class set. This catches the same
    // harmony even when Claude changes octave, velocity, or tiny timing details.
    std::map<int, std::vector<int>> events;
    for (const auto& note : chords->notes)
        events[(int) std::lround (note.startBeat * 4.0)].push_back (note.pitchMidi);

    juce::StringArray signature;
    for (auto& [step, pitches] : events)
    {
        if (pitches.size() < 2)
            continue;
        std::sort (pitches.begin(), pitches.end());
        juce::StringArray pcs;
        for (const auto pitch : pitches)
            pcs.addIfNotAlreadyThere (juce::String ((pitch % 12 + 12) % 12));
        // Pitch-class only for the bass slot too, so octave shifts don't change
        // the fingerprint (pitches.front() % 12 is already that after sorting).
        signature.add (juce::String (step) + ":" + juce::String ((pitches.front() % 12 + 12) % 12)
                       + "[" + pcs.joinIntoString (",") + "]");
    }
    return signature.joinIntoString ("|");
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
    // Clamp instead of rejecting: one out-of-range velocity from the model
    // shouldn't throw away an otherwise good multi-part pattern.
    vel = juce::jlimit (1, 127, vel);

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
    // Clamp instead of rejecting: an out-of-range tempo from the model
    // shouldn't throw away an otherwise good pattern.
    p.bpm = juce::jlimit (60, 200, (int) root.getProperty ("bpm", 0));

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

    p.progression = root.getProperty ("progression", "").toString().trim();
    if (auto* chordArr = root.getProperty ("chords", {}).getArray())
    {
        for (const auto& c : *chordArr)
        {
            const auto s = c.toString().trim();
            if (s.isNotEmpty())
                p.chordSymbols.add (s);
        }
    }

    auto parseNotesArray = [] (const juce::var& notesVar, std::vector<PatternNote>& out,
                               juce::String& error, const juce::String& label) -> bool
    {
        auto* arr = notesVar.getArray();
        if (arr == nullptr || arr->isEmpty())
        {
            error = label + " must be a non-empty array.";
            return false;
        }
        out.clear();
        out.reserve ((size_t) arr->size());
        for (int i = 0; i < arr->size(); ++i)
        {
            PatternNote note;
            juce::String err;
            if (! parseNoteObject (arr->getReference (i), note, err))
            {
                error = label + "[" + juce::String (i) + "]: " + err;
                return false;
            }
            out.push_back (note);
        }
        return true;
    };

    auto partsVar = root.getProperty ("parts", {});
    if (auto* partsArr = partsVar.getArray(); partsArr != nullptr && ! partsArr->isEmpty())
    {
        p.parts.reserve ((size_t) partsArr->size());
        for (int i = 0; i < partsArr->size(); ++i)
        {
            const auto& partVar = partsArr->getReference (i);
            if (! partVar.isObject())
            {
                result.error = "parts[" + juce::String (i) + "] must be an object.";
                return result;
            }
            MidiPatternPart lane;
            lane.instrument = partVar.getProperty ("instrument", "").toString().trim();
            if (lane.instrument.isEmpty())
            {
                result.error = "parts[" + juce::String (i) + "] missing instrument.";
                return result;
            }
            juce::String err;
            if (! parseNotesArray (partVar.getProperty ("notes", {}), lane.notes, err,
                                   "parts[" + juce::String (i) + "].notes"))
            {
                result.error = err;
                return result;
            }
            p.parts.push_back (std::move (lane));
        }
        // Keep legacy top-level fields pointing at the first lane for callers.
        p.instrument = p.parts.front().instrument;
        p.notes = p.parts.front().notes;
    }
    else
    {
        juce::String err;
        if (! parseNotesArray (root.getProperty ("notes", {}), p.notes, err, "\"notes\""))
        {
            result.error = err;
            return result;
        }
    }

    // Chord-bearing responses must carry explicit harmony metadata so the UI
    // and avoidance memory are not guessing from MIDI alone.
    {
        bool hasChordLane = p.instrument.containsIgnoreCase ("chord");
        for (const auto& lane : p.parts)
            if (lane.instrument.containsIgnoreCase ("chord"))
                hasChordLane = true;

        if (hasChordLane
            && p.progression.isEmpty()
            && p.chordSymbols.isEmpty())
        {
            result.error = "Chord responses must include \"progression\" and/or \"chords\" "
                           "(explicit harmony metadata), not only note events.";
            return result;
        }
    }

    // Repair notes that overrun the loop instead of rejecting the whole
    // pattern (mirrors the engine validator's clamp behaviour): notes that
    // start inside the loop get their tail trimmed; notes that start at or
    // after the loop end are dropped.
    {
        double beatsPerBar = 4.0;
        const int tsNumerator = p.timeSignature
                                    .upToFirstOccurrenceOf ("/", false, false)
                                    .trim().getIntValue();
        if (tsNumerator >= 1 && tsNumerator <= 32)
            beatsPerBar = (double) tsNumerator;

        const double loopBeats = (double) p.bars * beatsPerBar;
        auto clampLane = [loopBeats] (std::vector<PatternNote>& notes)
        {
            notes.erase (std::remove_if (notes.begin(), notes.end(),
                             [loopBeats] (const PatternNote& n)
                             { return n.startBeat >= loopBeats; }),
                         notes.end());
            for (auto& n : notes)
                if (n.startBeat + n.durationBeats > loopBeats)
                    n.durationBeats = loopBeats - n.startBeat;
        };

        clampLane (p.notes);
        for (auto& lane : p.parts)
            clampLane (lane.notes);
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

    // Serialize EVERY lane (multi-part loops included), one MIDI channel
    // per lane — single-part patterns still come through lanes() as one.
    for (const auto& lane : pattern.lanes())
    {
        const int ch = midiChannelFor (instrumentTypeFromName (lane.instrument));

        for (const auto& n : lane.notes)
        {
            const double onTick  = n.startBeat * (double) ppq;
            const double offTick = (n.startBeat + n.durationBeats) * (double) ppq;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, n.velocity);
            seq.addEvent (juce::MidiMessage::noteOn  (ch, n.pitchMidi, vel), onTick);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitchMidi),      offTick);
        }
    }

    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

GeneratedPart patternToGeneratedPart (const MidiPattern& pattern)
{
    MidiPatternPart lane;
    lane.instrument = pattern.instrument;
    lane.notes = pattern.notes;
    return patternPartToGeneratedPart (lane, pattern.bpm, pattern.bars);
}

GeneratedPart patternPartToGeneratedPart (const MidiPatternPart& part, int /*bpm*/, int /*bars*/)
{
    GeneratedPart out;
    out.type = instrumentTypeFromName (part.instrument);
    out.notes.reserve (part.notes.size());

    for (const auto& n : part.notes)
    {
        NoteEvent e;
        e.startBeats  = n.startBeat;
        e.lengthBeats = n.durationBeats;
        e.pitch       = n.pitchMidi;
        e.velocity    = (float) n.velocity / 127.0f;
        out.notes.push_back (e);
    }
    return out;
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
