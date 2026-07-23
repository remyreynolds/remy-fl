#include "../src/ai/MidiPattern.h"
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

    auto badBpm = parseClaudeMidiJson (R"({"bpm":400,"key":"C minor","instrument":"bass","bars":4,"timeSignature":"4/4","notes":[{"pitch":"C3","startBeat":0,"durationBeats":1,"velocity":100}]})");
    expect (! badBpm.ok, "bpm out of range rejected");

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

    if (fails == 0)
    {
        std::cout << "\nAll MidiPattern tests passed.\n";
        return 0;
    }

    std::cerr << "\n" << fails << " test(s) failed.\n";
    return 1;
}
