#include "../src/ai/MidiPattern.h"
#include "../src/ai/AIClient.h" // header-only use: sanitizeApiKey
#include <iostream>
#include <cstdlib>

using namespace aimidi;

static int fails = 0;

static void expect (bool cond, const char* msg)
{
    if (! cond)
    {
        std::cerr << "FAIL: " << msg << "\n";
        ++fails;
    }
    else
    {
        std::cout << "ok  — " << msg << "\n";
    }
}

int main()
{
    // --- note name → MIDI ---
    expect (noteNameToMidi ("A1") == 33, "A1 → 33");
    expect (noteNameToMidi ("C4") == 60, "C4 → 60");
    expect (noteNameToMidi ("C#2") == 37, "C#2 → 37");
    expect (noteNameToMidi ("Bb3") == 58, "Bb3 → 58");
    expect (noteNameToMidi ("not-a-note") < 0, "invalid pitch rejected");

    // --- valid bassline JSON ---
    const juce::String validJson = R"({
      "bpm": 126,
      "key": "A minor",
      "instrument": "bass",
      "bars": 4,
      "timeSignature": "4/4",
      "notes": [
        { "pitch": "A1", "startBeat": 0, "durationBeats": 0.5, "velocity": 110 },
        { "pitch": "A1", "startBeat": 1, "durationBeats": 0.5, "velocity": 100 },
        { "pitch": "E2", "startBeat": 2, "durationBeats": 1.0, "velocity": 105 }
      ]
    })";

    auto ok = parseClaudeMidiJson (validJson);
    expect (ok.ok, "valid bassline JSON parses");
    expect (ok.pattern.bpm == 126, "bpm = 126");
    expect (ok.pattern.notes.size() == 3, "3 notes");
    expect (ok.pattern.notes[0].pitchMidi == 33, "first note A1");
    expect (ok.pattern.lanes().size() == 1, "single-part has one lane");
    expect (chordProgressionFingerprint (ok.pattern).isEmpty(),
            "bass-only pattern has no chord fingerprint");

    auto seq = patternToSequence (ok.pattern, 960);
    int noteOns = 0, noteOffs = 0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto& m = seq.getEventPointer (i)->message;
        if (m.isNoteOn()) ++noteOns;
        if (m.isNoteOff()) ++noteOffs;
    }
    expect (noteOns == 3 && noteOffs == 3, "sequence has matching noteOn/noteOff");
    expect (seq.getEventTime (0) == 0.0, "first event at beat 0");

    // --- full-loop parts[] JSON ---
    const juce::String loopJson = R"({
      "bpm": 124,
      "key": "F minor",
      "bars": 8,
      "timeSignature": "4/4",
      "progression": "i–i–i",
      "chords": ["Fm", "Fm", "Fm", "Fm", "Fm", "Fm", "Fm", "Fm"],
      "parts": [
        {
          "instrument": "chords",
          "notes": [
            { "pitch": "F3", "startBeat": 0, "durationBeats": 2, "velocity": 90 },
            { "pitch": "Ab3", "startBeat": 0, "durationBeats": 2, "velocity": 88 },
            { "pitch": "C4", "startBeat": 0, "durationBeats": 2, "velocity": 86 }
          ]
        },
        {
          "instrument": "bass",
          "notes": [
            { "pitch": "F1", "startBeat": 0.5, "durationBeats": 0.4, "velocity": 110 }
          ]
        },
        {
          "instrument": "melody",
          "notes": [
            { "pitch": "C5", "startBeat": 0, "durationBeats": 0.5, "velocity": 100 }
          ]
        }
      ]
    })";
    auto loop = parseClaudeMidiJson (loopJson);
    expect (loop.ok, "full-loop JSON parses");
    expect (loop.pattern.progression.isNotEmpty(), "progression metadata present");
    expect (loop.pattern.chordSymbols.size() == 8, "chords metadata present");
    expect (loop.pattern.parts.size() == 3, "3 parts");
    expect (loop.pattern.lanes().size() == 3, "3 lanes");
    expect (loop.pattern.totalNotes() == 5, "5 total notes");
    expect (loop.pattern.instrumentSummary().contains ("chords"), "summary includes chords");
    expect (loop.pattern.instrumentSummary().contains ("bass"), "summary includes bass");
    const auto harmony = chordProgressionFingerprint (loop.pattern);
    expect (harmony.isNotEmpty(), "chord lane gets a progression fingerprint");

    auto octaveShifted = loop.pattern;
    for (auto& note : octaveShifted.parts.front().notes)
        note.pitchMidi += 12;
    expect (chordProgressionFingerprint (octaveShifted) == harmony,
            "fingerprint ignores octave-only voicing changes");

    // --- invalid JSON ---
    auto bad = parseClaudeMidiJson ("{ not json");
    expect (! bad.ok, "invalid JSON fails cleanly");
    expect (bad.error.isNotEmpty(), "invalid JSON has error message");

    auto emptyNotes = parseClaudeMidiJson (R"({"bpm":120,"key":"C minor","instrument":"bass","bars":4,"timeSignature":"4/4","notes":[]})");
    expect (! emptyNotes.ok, "empty notes rejected");

    // --- bpm range: clamp instead of reject (60–200) ---
    auto highBpm = parseClaudeMidiJson (R"({"bpm":400,"key":"C minor","instrument":"bass","bars":4,"timeSignature":"4/4","notes":[{"pitch":"C3","startBeat":0,"durationBeats":1,"velocity":100}]})");
    expect (highBpm.ok, "bpm above range still parses");
    expect (highBpm.pattern.bpm == 200, "bpm 400 clamped to 200");

    auto lowBpm = parseClaudeMidiJson (R"({"bpm":30,"key":"C minor","instrument":"bass","bars":4,"timeSignature":"4/4","notes":[{"pitch":"C3","startBeat":0,"durationBeats":1,"velocity":100}]})");
    expect (lowBpm.ok, "bpm below range still parses");
    expect (lowBpm.pattern.bpm == 60, "bpm 30 clamped to 60");

    auto bpm190 = parseClaudeMidiJson (R"({"bpm":190,"key":"C minor","instrument":"bass","bars":4,"timeSignature":"4/4","notes":[{"pitch":"C3","startBeat":0,"durationBeats":1,"velocity":100}]})");
    expect (bpm190.ok && bpm190.pattern.bpm == 190, "bpm 190 accepted unchanged");

    auto chordsNoMeta = parseClaudeMidiJson (R"({
      "bpm": 120, "key": "F minor", "instrument": "chords", "bars": 4, "timeSignature": "4/4",
      "notes": [
        { "pitch": "F3", "startBeat": 0, "durationBeats": 1, "velocity": 90 },
        { "pitch": "Ab3", "startBeat": 0, "durationBeats": 1, "velocity": 88 }
      ]
    })");
    expect (! chordsNoMeta.ok, "chord JSON without progression/chords metadata rejected");

    // MIDI export / sequence path still works for FL drag readiness
    {
        auto okChord = parseClaudeMidiJson (R"({
          "bpm": 120, "key": "F minor", "instrument": "chords", "bars": 4,
          "progression": "i–VI–III–VII",
          "chords": ["Fm","Db","Ab","Eb"],
          "notes": [
            { "pitch": "F3", "startBeat": 0, "durationBeats": 1, "velocity": 90 },
            { "pitch": "Ab3", "startBeat": 0, "durationBeats": 1, "velocity": 88 },
            { "pitch": "C4", "startBeat": 0, "durationBeats": 1, "velocity": 86 }
          ]
        })");
        expect (okChord.ok, "chord JSON with explicit harmony metadata parses");
        auto seq = patternToSequence (okChord.pattern);
        expect (seq.getNumEvents() >= 2, "MIDI sequence export still works");
    }

    // --- overrunning notes are clamped/dropped, not rejected ---
    {
        // 2 bars of 4/4 = 8 beats. Note 2 overhangs the loop end; note 3
        // starts past the loop end entirely.
        auto overrun = parseClaudeMidiJson (R"({
          "bpm": 124, "key": "A minor", "instrument": "bass", "bars": 2, "timeSignature": "4/4",
          "notes": [
            { "pitch": "A1", "startBeat": 0,  "durationBeats": 1,  "velocity": 100 },
            { "pitch": "A1", "startBeat": 6,  "durationBeats": 8,  "velocity": 100 },
            { "pitch": "E2", "startBeat": 12, "durationBeats": 1,  "velocity": 100 }
          ]
        })");
        expect (overrun.ok, "pattern with overrunning notes still parses");
        expect (overrun.pattern.notes.size() == 2, "note starting past loop end is dropped");
        expect (overrun.pattern.notes[1].durationBeats == 2.0,
                "overhanging note clamped to the loop end (6 + 8 → dur 2)");
        expect (overrun.pattern.notes[0].durationBeats == 1.0, "in-loop note untouched");
    }

    // --- overrun clamp also applies inside parts[] lanes ---
    {
        auto overrunLoop = parseClaudeMidiJson (R"({
          "bpm": 124, "key": "F minor", "bars": 1, "timeSignature": "4/4",
          "progression": "i", "chords": ["Fm"],
          "parts": [
            { "instrument": "chords", "notes": [
              { "pitch": "F3",  "startBeat": 0, "durationBeats": 9, "velocity": 90 },
              { "pitch": "Ab3", "startBeat": 0, "durationBeats": 9, "velocity": 88 } ] },
            { "instrument": "bass", "notes": [
              { "pitch": "F1", "startBeat": 3.5, "durationBeats": 2, "velocity": 110 },
              { "pitch": "F1", "startBeat": 5,   "durationBeats": 1, "velocity": 110 } ] }
          ]
        })");
        expect (overrunLoop.ok, "multi-part pattern with overruns still parses");
        expect (overrunLoop.pattern.parts[0].notes[0].durationBeats == 4.0,
                "chord lane note clamped to bars*beatsPerBar");
        expect (overrunLoop.pattern.parts[1].notes.size() == 1,
                "bass note starting past loop end dropped from its lane");
        expect (overrunLoop.pattern.parts[1].notes[0].durationBeats == 0.5,
                "bass overhang clamped (3.5 + 2 → dur 0.5)");
    }

    // --- patternToSequence serializes ALL lanes, one channel per lane ---
    {
        auto multiSeq = patternToSequence (loop.pattern, 960);
        int noteOns = 0;
        bool sawChordsCh = false, sawBassCh = false, sawMelodyCh = false;
        const int chordsCh = midiChannelFor (InstrumentType::Chords);
        const int bassCh   = midiChannelFor (InstrumentType::Bass);
        const int melodyCh = midiChannelFor (InstrumentType::Melody);
        for (int i = 0; i < multiSeq.getNumEvents(); ++i)
        {
            auto& m = multiSeq.getEventPointer (i)->message;
            if (! m.isNoteOn())
                continue;
            ++noteOns;
            if (m.getChannel() == chordsCh) sawChordsCh = true;
            if (m.getChannel() == bassCh)   sawBassCh = true;
            if (m.getChannel() == melodyCh) sawMelodyCh = true;
        }
        expect (noteOns == 5, "sequence contains notes from every lane (5 noteOns)");
        expect (sawChordsCh, "chords lane on its own MIDI channel");
        expect (sawBassCh, "bass lane on its own MIDI channel");
        expect (sawMelodyCh, "melody lane on its own MIDI channel");
    }

    // --- API key sanitization (header-injection guard) ---
    expect (sanitizeApiKey ("  sk-ant-abc123\r\n") == "sk-ant-abc123",
            "API key trimmed of whitespace and CR/LF");
    expect (sanitizeApiKey ("sk-ant\r\nX-Evil: 1") == "sk-antX-Evil: 1",
            "embedded CR/LF stripped so no extra header line survives");
    expect (sanitizeApiKey ("\tsk-plain ") == "sk-plain",
            "tabs/spaces trimmed around the key");

    if (fails == 0)
    {
        std::cout << "\nAll MidiPattern tests passed.\n";
        return 0;
    }

    std::cerr << "\n" << fails << " test(s) failed.\n";
    return 1;
}
