#pragma once

#include <array>
#include <string>
#include <vector>

namespace aimidi
{
/** Scale/key theory helpers. Header-only, no JUCE dependency so the engine
    stays unit-testable in isolation. */
namespace theory
{
    // Semitone offsets from the root for common scales (one octave).
    inline std::vector<int> scaleIntervals (const std::string& scale)
    {
        if (scale == "minor" || scale == "aeolian")      return { 0, 2, 3, 5, 7, 8, 10 };
        if (scale == "major" || scale == "ionian")       return { 0, 2, 4, 5, 7, 9, 11 };
        if (scale == "dorian")                            return { 0, 2, 3, 5, 7, 9, 10 };
        if (scale == "phrygian")                          return { 0, 1, 3, 5, 7, 8, 10 };
        if (scale == "lydian")                            return { 0, 2, 4, 6, 7, 9, 11 };
        if (scale == "mixolydian")                        return { 0, 2, 4, 5, 7, 9, 10 };
        if (scale == "locrian")                           return { 0, 1, 3, 5, 6, 8, 10 };
        if (scale == "harmonicMinor")                     return { 0, 2, 3, 5, 7, 8, 11 };
        if (scale == "minorPentatonic")                   return { 0, 3, 5, 7, 10 };
        if (scale == "majorPentatonic")                   return { 0, 2, 4, 7, 9 };
        return { 0, 2, 3, 5, 7, 8, 10 }; // default: natural minor
    }

    // Convert a root note name ("C", "F#", "Bb") to a pitch class 0-11.
    inline int rootPitchClass (const std::string& name)
    {
        static const std::array<std::string, 12> sharps =
            { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        static const std::array<std::string, 12> flats =
            { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };
        for (int i = 0; i < 12; ++i)
            if (name == sharps[(size_t) i] || name == flats[(size_t) i])
                return i;
        return 0; // default C
    }

    /** Snap an arbitrary MIDI note to the nearest note in the given scale.
        Guarantees output is always in-key — the theory validator's backbone. */
    inline int snapToScale (int midiNote, int rootPc, const std::vector<int>& intervals)
    {
        const int octave = midiNote / 12;
        const int pc     = ((midiNote % 12) - rootPc + 12) % 12;

        int best = intervals.front();
        int bestDist = 128;
        for (int iv : intervals)
        {
            int d = std::abs (iv - pc);
            if (d < bestDist) { bestDist = d; best = iv; }
        }
        return octave * 12 + ((rootPc + best) % 12);
    }

    // Build a stacked-thirds chord rooted on a scale degree, in-key.
    // tones: 3 = triad, 4 = 7th, 5 = 9th, 6 = 11th.
    inline std::vector<int> diatonicChord (int rootMidi, int rootPc,
                                           const std::vector<int>& intervals,
                                           int degree, int tones = 3)
    {
        std::vector<int> notes;
        const int steps = tones < 3 ? 3 : (tones > 6 ? 6 : tones);
        const int baseOctave = (rootMidi / 12) * 12; // strip pitch class, keep octave
        for (int i = 0; i < steps; ++i)
        {
            const int scaleIdx = degree + i * 2;
            const int oct      = scaleIdx / (int) intervals.size();
            const int iv       = intervals[(size_t) (scaleIdx % (int) intervals.size())];
            notes.push_back (baseOctave + oct * 12 + ((rootPc + iv) % 12));
        }
        return notes;
    }
} // namespace theory
} // namespace aimidi
