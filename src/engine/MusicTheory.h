#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
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
        // Circular pitch-class distance: a note 1 semitone below the root must
        // snap UP to the root, not down to the 7th; and the result must stay in
        // the note's own register (no %12 octave jumps).
        const int pc = ((midiNote % 12) - rootPc + 12) % 12;

        int bestDelta = 0, bestDist = 128;
        for (int iv : intervals)
        {
            int d = iv - pc;
            if (d > 6)  d -= 12;   // wrap to [-6, +6]
            if (d < -6) d += 12;
            if (std::abs (d) < bestDist) { bestDist = std::abs (d); bestDelta = d; }
        }
        return midiNote + bestDelta;
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
            // No %12 here: intervals crossing the octave must keep ascending
            // (stacked thirds), not fold back into a cluster.
            notes.push_back (baseOctave + oct * 12 + rootPc + iv);
        }
        return notes;
    }
    /** Re-voice `next` so it sits as close as possible to the previous
        chord's register (close-position voicing around the previous centre).
        Classic cheap voice-leading: minimizes hand movement between chords. */
    inline std::vector<int> voiceLead (const std::vector<int>& prev, std::vector<int> next)
    {
        if (prev.empty() || next.empty()) return next;

        double target = 0.0;
        for (int n : prev) target += n;
        target /= (double) prev.size();

        for (auto& n : next)
        {
            while (n - target > 6.0)  n -= 12;
            while (target - n > 6.0)  n += 12;
        }

        std::sort (next.begin(), next.end());
        // Don't DELETE tones that collapsed onto the same pitch (that shrank
        // 9th/11th chords to triads) — re-spread them up an octave instead.
        for (size_t i = 1; i < next.size(); ++i)
            while (next[i] <= next[i - 1])
                next[i] += 12;
        return next;
    }

    /** True if the note's pitch class is one of the chord's pitch classes. */
    inline bool isChordTone (int midiNote, const std::vector<int>& chord)
    {
        for (int c : chord)
            if ((c % 12) == (midiNote % 12))
                return true;
        return false;
    }

    /** Snap a note to the nearest chord tone (keeps register). */
    inline int snapToChord (int midiNote, const std::vector<int>& chord)
    {
        if (chord.empty()) return midiNote;
        int best = midiNote, bestDist = 128;
        for (int c : chord)
        {
            const int pc = c % 12;
            for (int oct = (midiNote / 12) - 1; oct <= (midiNote / 12) + 1; ++oct)
            {
                const int cand = oct * 12 + pc;
                const int d = std::abs (cand - midiNote);
                if (d < bestDist) { bestDist = d; best = cand; }
            }
        }
        return best;
    }
} // namespace theory
} // namespace aimidi
