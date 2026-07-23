#pragma once

#include "MusicInstructions.h"
#include "MusicTheory.h"
#include "StylePresets.h"
#include <vector>

namespace aimidi
{
/** The shared song skeleton.

    Every lane (chords, bass, pad, arp, melody, critic) derives its harmonic
    decisions from ONE deterministic plan built from the same MusicParams, so
    parts can never disagree about the progression. Chords are voice-led bar
    to bar (close-position voicing around the previous chord's centre).

    Deterministic on purpose: parts are generated in separate calls, and two
    calls with the same params MUST agree on the harmony. Only rhythm/feel
    uses the RNG. JUCE-free, unit-testable. */
struct SongPlan
{
    std::vector<int> degrees;              // scale degree per bar
    std::vector<std::vector<int>> chords;  // voice-led MIDI pitches per bar
};

/** Cadence degree for the final bar of an 8-bar phrase: prefer the
    dominant-function 5th degree (index 4); if the progression already ends
    there, fall back to the subdominant (index 3). Deterministic — the whole
    plan must be reproducible from MusicParams alone. */
inline int cadenceDegree (const std::array<int, 4>& progression)
{
    return progression[3] == 4 ? 3 : 4;
}

inline SongPlan buildSongPlan (const MusicParams& p, int tones)
{
    const auto& st   = findStyle (p.genre);
    const int rootPc = theory::rootPitchClass (p.root);
    const auto ivals = theory::scaleIntervals (p.scale);
    const int base   = 12 * p.octave + rootPc;

    SongPlan plan;
    std::vector<int> prev;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        // A/B phrase structure: the 4-bar progression repeats, but the final
        // bar of every 8-bar phrase substitutes a cadential turnaround chord
        // so longer loops breathe (tension into the next phrase) instead of
        // looping identically.
        int deg = st.progression[(size_t) (bar % 4)];
        if (p.bars >= 8 && (bar % 8) == 7)
            deg = cadenceDegree (st.progression);

        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, tones);
        if (! prev.empty())
            chord = theory::voiceLead (prev, chord);

        plan.degrees.push_back (deg);
        plan.chords.push_back (chord);
        prev = chord;
    }
    return plan;
}

/** The chord governing a given position (in beats) — clamped, so positions
    past the end reuse the final bar. */
inline const std::vector<int>& chordAtBeat (const SongPlan& plan, double beats)
{
    const int bar = (int) (beats / 4.0);
    const int idx = bar < 0 ? 0
                  : bar >= (int) plan.chords.size() ? (int) plan.chords.size() - 1
                  : bar;
    return plan.chords[(size_t) idx];
}

} // namespace aimidi
