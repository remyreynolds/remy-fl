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
        const int deg = st.progression[(size_t) (bar % 4)];
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
