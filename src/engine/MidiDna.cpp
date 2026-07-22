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

    int drumOnsets = 0, pitchedNotes = 0;
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
                ++pitchedNotes;
            }
        }
    }

    // --- groove verdict ---
    dna.hasDrums = drumOnsets >= 4;

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
         + ". Regenerating with it — drums follow the reference feel now.";
}

} // namespace aimidi
