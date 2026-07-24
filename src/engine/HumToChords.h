#pragma once

#include "MusicInstructions.h"
#include "MusicTheory.h"
#include <algorithm>
#include <vector>

namespace aimidi
{
/** One captured hummed note: a stable pitch held for some duration, already
    converted to beats by the caller (which knows the live tempo/transport). */
struct HumNote
{
    double startBeats  = 0.0;
    double lengthBeats = 1.0;
    int    midiNote    = 60;
};

/** Turn a short hummed melody into an in-key chord progression for the
    Chords lane. Pure function, JUCE-free, unit-testable: each hummed note
    is snapped to the project's scale, treated as implying a chord rooted on
    that scale degree, voiced with stacked thirds and voice-led from the
    previous chord so the progression doesn't jump around register. */
inline GeneratedPart humToChords (const MusicParams& params, const std::vector<HumNote>& hums)
{
    GeneratedPart out;
    out.type = InstrumentType::Chords;
    if (hums.empty())
        return out;

    const int rootPc = theory::rootPitchClass (params.root);
    const auto intervals = theory::scaleIntervals (params.scale);
    const int tones = params.chordComplexity > 0.66f ? 5
                     : (params.chordComplexity > 0.33f ? 4 : 3);

    std::vector<int> prevChord;
    for (const auto& h : hums)
    {
        const int snapped = theory::snapToScale (h.midiNote, rootPc, intervals);
        const int pc = ((snapped % 12) - rootPc + 12) % 12;

        // Find which scale degree this pitch class corresponds to.
        int degree = 0;
        for (size_t i = 0; i < intervals.size(); ++i)
            if (intervals[i] == pc) { degree = (int) i; break; }

        auto chord = theory::diatonicChord (snapped, rootPc, intervals, degree, tones);
        chord = theory::voiceLead (prevChord, chord);
        prevChord = chord;

        for (int note : chord)
        {
            NoteEvent ev;
            ev.startBeats  = h.startBeats;
            ev.lengthBeats = std::max (0.25, h.lengthBeats);
            ev.pitch       = std::min (127, std::max (0, note));
            ev.velocity    = 0.72f;
            out.notes.push_back (ev);
        }
    }
    return out;
}

} // namespace aimidi
