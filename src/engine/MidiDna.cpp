#include "MidiDna.h"
#include "MusicTheory.h"
#include <cmath>

namespace aimidi
{
namespace
{
    /** GM percussion note -> kit piece. Returns -1 for unmapped notes. */
    int gmNoteToPiece (int note)
    {
        switch (note)
        {
            case 35: case 36:           return (int) DrumPiece::Kick;
            case 38: case 40:           return (int) DrumPiece::Snare;
            case 39:                    return (int) DrumPiece::Clap;
            case 42: case 44:           return (int) DrumPiece::ClosedHat;
            case 46:                    return (int) DrumPiece::OpenHat;
            case 51: case 53: case 59:  return (int) DrumPiece::Ride;
            case 69: case 70: case 82:  return (int) DrumPiece::Shaker;
            case 37:                    return (int) DrumPiece::Rim;
            case 62: case 63:           return (int) DrumPiece::CongaHi;
            case 64: case 65: case 66:  return (int) DrumPiece::CongaLo;
            default:                    return -1;
        }
    }

    /** Scale templates the harmony detector votes across. */
    const std::array<const char*, 3> candidateScales { "major", "minor", "dorian" };
}

MidiDna MidiDna::analyzeFile (const juce::File& file)
{
    MidiDna dna;
    dna.sourceName = file.getFileName().toStdString();

    juce::FileInputStream in (file);
    if (! in.openedOk()) return dna;

    juce::MidiFile mf;
    if (! mf.readFrom (in)) return dna;

    const short fmt = mf.getTimeFormat();
    if (fmt <= 0) return dna; // SMPTE time not supported — loops are PPQ
    const double ticksPerBeat = (double) fmt;

    int drumOnsets = 0, pitchedNotes = 0, syncopatedOnsets = 0;
    double lastBeat = 0.0, velocityTotal = 0.0;
    std::array<double, 12> pcWeight {};

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack (t);
        for (int e = 0; e < track->getNumEvents(); ++e)
        {
            const auto& msg = track->getEventPointer (e)->message;
            if (! msg.isNoteOn()) continue;

            const double beat = track->getEventTime (e) / ticksPerBeat;
            const int note    = msg.getNoteNumber();
            const float vel   = msg.getFloatVelocity();
            lastBeat = std::max (lastBeat, beat);

            const int piece = msg.getChannel() == 10 ? gmNoteToPiece (note)
                            : (note < 82 ? gmNoteToPiece (note) : -1);

            // Heuristic: channel 10 is definitely drums; otherwise only
            // treat it as a drum hit if the note maps AND the file has no
            // channel-10 material competing (decided by the vote below).
            if (msg.getChannel() == 10 && piece >= 0)
            {
                const int step = ((int) std::llround (beat * 4.0)) % 16;
                auto& cell = dna.drumSteps[(size_t) piece][(size_t) (step < 0 ? step + 16 : step)];
                cell = std::max (cell, vel);
                ++drumOnsets;
            }
            else if (msg.getChannel() != 10)
            {
                pcWeight[(size_t) (note % 12)] += (double) vel;
                velocityTotal += vel * 127.0;
                const double quarterDistance = std::abs (beat - std::round (beat));
                if (quarterDistance > 0.08)
                    ++syncopatedOnsets;
                ++pitchedNotes;
            }
        }
    }

    // --- groove verdict ---
    dna.hasDrums = drumOnsets >= 4;
    dna.pitchedNoteCount = pitchedNotes;
    dna.pitchedDensityPerBeat = lastBeat > 0.0
        ? (float) ((double) pitchedNotes / (lastBeat + 1.0)) : 0.0f;
    dna.averageVelocity = pitchedNotes > 0
        ? (float) (velocityTotal / (double) pitchedNotes) : 0.0f;
    dna.syncopation = pitchedNotes > 0
        ? (float) syncopatedOnsets / (float) pitchedNotes : 0.0f;

    // JUCE exposes tempo events in seconds-per-quarter-note.
    for (int t = 0; t < mf.getNumTracks() && dna.bpm <= 0.0; ++t)
    {
        const auto* track = mf.getTrack (t);
        for (int e = 0; e < track->getNumEvents(); ++e)
        {
            const auto& msg = track->getEventPointer (e)->message;
            if (msg.isTempoMetaEvent())
            {
                const auto seconds = msg.getTempoSecondsPerQuarterNote();
                if (seconds > 0.0)
                    dna.bpm = 60.0 / seconds;
                break;
            }
        }
    }

    // --- harmony verdict: vote root+scale by pitch-class weight coverage ---
    if (pitchedNotes >= 8)
    {
        double bestScore = -1.0;
        for (int root = 0; root < 12; ++root)
        {
            for (const char* sc : candidateScales)
            {
                const auto ivals = theory::scaleIntervals (sc);
                double score = pcWeight[(size_t) root] * 0.5; // root emphasis
                for (int iv : ivals)
                    score += pcWeight[(size_t) ((root + iv) % 12)];
                if (score > bestScore)
                {
                    bestScore = score;
                    dna.rootPc = root;
                    dna.scale  = sc;
                }
            }
        }
        dna.hasHarmony = true;
    }

    dna.valid = dna.hasDrums || dna.hasHarmony;
    return dna;
}

juce::String MidiDna::describe() const
{
    if (! valid)
        return "Couldn't learn anything usable from that MIDI file.";

    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    juce::String s ("Learned DNA from \"" + juce::String (sourceName) + "\": ");
    juce::StringArray bits;

    if (hasDrums)
    {
        int covered = 0;
        for (const auto& piece : drumSteps)
            for (float v : piece)
                if (v > 0.0f) { ++covered; break; }
        bits.add ("drum groove (" + juce::String (covered) + " kit pieces)");
    }
    if (hasHarmony)
        bits.add ("harmony -> " + juce::String (names[rootPc]) + " " + juce::String (scale));

    return s + bits.joinIntoString (" + ")
         + ". Saved as a reusable style reference; source notes are never copied.";
}

juce::String MidiDna::styleProfile() const
{
    if (! valid) return {};
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    juce::String s;
    s << "# Reference MIDI style profile\n\n"
      << "Source label: " << juce::String (sourceName) << "\n"
      << "Learning rule: borrow abstract feel only. Never reproduce source pitches, riffs, or note sequence.\n";
    if (bpm > 0.0) s << "Tempo: " << juce::String (bpm, 1) << " BPM\n";
    if (hasHarmony) s << "Tonal center: " << names[rootPc] << " " << juce::String (scale) << "\n";
    s << "Pitched-note density: " << juce::String (pitchedDensityPerBeat, 2) << " notes/beat\n"
      << "Average velocity: " << juce::String (averageVelocity, 1) << "/127\n"
      << "Syncopation: " << juce::String (syncopation * 100.0f, 0) << "% of pitched onsets off quarter notes\n";
    if (hasDrums)
    {
        int covered = 0;
        for (const auto& piece : drumSteps)
            for (float v : piece)
                if (v > 0.0f) { ++covered; break; }
        s << "Drum roles detected: " << covered << "\n";
    }
    s << "Use for: density, energy, syncopation, tonal color, and groove weighting.\n";
    return s;
}

} // namespace aimidi
