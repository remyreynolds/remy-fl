#pragma once

#include "MusicInstructions.h"
#include "MusicTheory.h"
#include "StylePresets.h"
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace aimidi
{
/** The shared song skeleton.

    Every lane (chords, bass, pad, arp, melody, critic) derives its harmonic
    decisions from ONE deterministic plan built from the same MusicParams, so
    parts can never disagree about the progression. Chords are voice-led bar
    to bar (close-position voicing around the previous chord's centre).

    Deterministic on purpose: parts are generated in separate calls, and two
    calls with the same params MUST agree on the harmony. The seed selects
    which genre-appropriate progression template (and light harmonic colour)
    is used — rhythm/feel still uses the RNG separately. */
struct SongPlan
{
    std::vector<int> degrees;              // scale degree per bar
    std::vector<std::vector<int>> chords;  // voice-led MIDI pitches per bar
    int templateIndex = 0;                 // which progression bank entry was chosen
    int inversionBias = 0;                 // 0..2 — voicing colour from seed
    bool usedSubstitution = false;
    bool usedTurnaround = false;
};

/** Compact harmonic fingerprint for local memory / New Idea avoidance.
    Same key with different degrees → different fingerprint. */
inline std::string songPlanFingerprint (const SongPlan& plan)
{
    std::ostringstream oss;
    for (size_t i = 0; i < plan.degrees.size(); ++i)
    {
        if (i) oss << '-';
        oss << plan.degrees[i];
        if (i < plan.chords.size() && ! plan.chords[i].empty())
        {
            // Include bass PC so inversion colour is part of identity.
            const int bassPc = ((plan.chords[i].front() % 12) + 12) % 12;
            oss << ':' << bassPc;
        }
    }
    return oss.str();
}

/** Cadence degree for the final bar of an 8-bar phrase: prefer the
    dominant-function 5th degree (index 4); if the progression already ends
    there, fall back to the subdominant (index 3). Deterministic — the whole
    plan must be reproducible from MusicParams alone. */
inline int cadenceDegree (const std::array<int, 4>& progression)
{
    return progression[3] == 4 ? 3 : 4;
}

inline int cadenceDegreeFromLast (int lastDegree)
{
    return lastDegree == 4 ? 3 : 4;
}

//==============================================================================
/** Genre-appropriate progression templates (scale degrees).
    Lengths of 1, 2, 4, or 8 — expanded to fill the loop length. */
struct ProgressionTemplate
{
    std::vector<int> degrees;
    const char* label = "";
};

inline const std::vector<ProgressionTemplate>& progressionBankForStyle (const StylePreset& st)
{
    // Style default (index 0) always mirrors StylePreset::progression so older
    // callers / docs that quote the preset still match seed==0 behaviour.
    static std::vector<ProgressionTemplate> tech = {
        { { 0, 0, 5, 3 }, "tech-default" },
        { { 0, 5, 3, 4 }, "tech-i-vi-iv-v" },
        { { 0, 3, 4, 0 }, "tech-i-iv-v-i" },
        { { 0, 5 }, "tech-two-chord" },
        { { 0 }, "tech-static" },
        { { 0, 0, 3, 5, 0, 5, 3, 4 }, "tech-eight" },
        { { 0, 6, 5, 3 }, "tech-i-bvii-vi-iv" },
        { { 0, 4, 5, 3 }, "tech-i-v-vi-iv" },
    };
    static std::vector<ProgressionTemplate> bass = {
        { { 0, 0, 0, 5 }, "bass-default" },
        { { 0, 5, 0, 3 }, "bass-pump" },
        { { 0, 0 }, "bass-two" },
        { { 0 }, "bass-drone" },
        { { 0, 5, 3, 0 }, "bass-loop" },
        { { 0, 0, 5, 5, 0, 3, 5, 0 }, "bass-eight" },
    };
    static std::vector<ProgressionTemplate> afro = {
        { { 0, 3, 5, 4 }, "afro-default" },
        { { 0, 5, 3, 4 }, "afro-warm" },
        { { 0, 2, 3, 5 }, "afro-step" },
        { { 0, 3 }, "afro-two" },
        { { 0, 3, 5, 4, 0, 5, 3, 4 }, "afro-eight" },
        { { 0, 5, 4, 3 }, "afro-descend" },
    };
    static std::vector<ProgressionTemplate> melodic = {
        { { 0, 5, 3, 6 }, "melodic-default" },
        { { 0, 3, 5, 4 }, "melodic-lift" },
        { { 0, 5, 0, 4 }, "melodic-pedal" },
        { { 0, 6 }, "melodic-two" },
        { { 0, 5, 3, 6, 0, 3, 5, 4 }, "melodic-eight" },
        { { 0, 2, 5, 3 }, "melodic-ii" },
    };
    static std::vector<ProgressionTemplate> deep = {
        { { 0, 5, 3, 4 }, "deep-default" },
        { { 0, 3, 5, 4 }, "deep-soul" },
        { { 0, 5 }, "deep-two" },
        { { 0, 5, 3, 4, 0, 5, 4, 0 }, "deep-eight" },
        { { 0, 4, 5, 3 }, "deep-classic" },
        { { 0 }, "deep-drone" },
    };
    static std::vector<ProgressionTemplate> organic = {
        { { 0, 2, 5, 3 }, "organic-default" },
        { { 0, 5, 3, 4 }, "organic-warm" },
        { { 0, 2 }, "organic-two" },
        { { 0, 2, 5, 3, 0, 5, 2, 4 }, "organic-eight" },
        { { 0, 3, 2, 5 }, "organic-wander" },
    };
    static std::vector<ProgressionTemplate> garage = {
        { { 0, 5, 3, 6 }, "garage-default" },
        { { 0, 3, 5, 0 }, "garage-bounce" },
        { { 0, 5 }, "garage-two" },
        { { 0, 5, 3, 6, 0, 3, 5, 4 }, "garage-eight" },
        { { 0, 4, 3, 5 }, "garage-twist" },
    };
    static std::vector<ProgressionTemplate> classic = {
        { { 0, 4, 5, 3 }, "classic-default" },
        { { 0, 5, 3, 4 }, "classic-pop" },
        { { 0, 3, 4, 0 }, "classic-cadence" },
        { { 0, 4 }, "classic-two" },
        { { 0, 4, 5, 3, 0, 5, 4, 0 }, "classic-eight" },
        { { 0, 2, 5, 3 }, "classic-ii-V" },
    };
    static std::vector<ProgressionTemplate> fallback = {
        { { 0, 5, 3, 4 }, "house-default" },
        { { 0, 3, 4, 0 }, "house-turn" },
        { { 0, 5 }, "house-two" },
        { { 0 }, "house-one" },
        { { 0, 5, 3, 4, 0, 3, 5, 4 }, "house-eight" },
    };

    const std::string name = st.name != nullptr ? st.name : "";
    if (name == "Tech House")   return tech;
    if (name == "Bass House")   return bass;
    if (name == "Afro House")   return afro;
    if (name == "Melodic House") return melodic;
    if (name == "Deep House")   return deep;
    if (name == "Organic House") return organic;
    if (name == "UK Garage")    return garage;
    if (name == "Classic House") return classic;
    return fallback;
}

/** Ensure bank entry 0 matches the StylePreset default progression. */
inline ProgressionTemplate styleDefaultTemplate (const StylePreset& st)
{
    ProgressionTemplate t;
    t.degrees.assign (st.progression.begin(), st.progression.end());
    t.label = "style-preset";
    return t;
}

inline std::vector<int> expandProgressionDegrees (const std::vector<int>& motif, int bars)
{
    std::vector<int> out;
    out.reserve ((size_t) bars);
    if (motif.empty())
    {
        for (int i = 0; i < bars; ++i) out.push_back (0);
        return out;
    }
    for (int bar = 0; bar < bars; ++bar)
        out.push_back (motif[(size_t) (bar % (int) motif.size())]);
    return out;
}

/** Apply seed-driven colour: occasional diatonic substitution + inversion bias.
    Stays inside the selected key (degree space only — no chromatic borrow). */
inline void colourProgression (std::vector<int>& degrees,
                               uint32_t seed,
                               bool& usedSubstitution,
                               int& inversionBias)
{
    usedSubstitution = false;
    inversionBias = (int) ((seed >> 8) % 3u); // 0 root, 1 first-ish, 2 open

    if (degrees.empty()) return;

    // Substitution: swap one mid-phrase chord to a diatonic neighbour (seed-gated).
    if (((seed >> 3) & 0x7u) >= 5u && degrees.size() >= 3)
    {
        const size_t idx = 1 + (size_t) ((seed >> 11) % (degrees.size() - 2));
        const int options[] = { 0, 2, 3, 4, 5 };
        const int pick = options[(seed >> 17) % 5u];
        if (pick != degrees[idx])
        {
            degrees[idx] = pick;
            usedSubstitution = true;
        }
    }
}

inline std::vector<int> applyInversionColour (std::vector<int> chord, int inversionBias)
{
    if (chord.size() < 2 || inversionBias <= 0)
        return chord;

    // Rotate lowest notes up an octave (1st / 2nd inversion flavour).
    const int rotates = std::min (inversionBias, (int) chord.size() - 1);
    for (int r = 0; r < rotates; ++r)
    {
        const int n = chord.front();
        chord.erase (chord.begin());
        chord.push_back (n + 12);
    }
    std::sort (chord.begin(), chord.end());
    return chord;
}

/** Optional 7th/9th extension colour from seed — still diatonic. */
inline int tonesForSeed (int baseTones, uint32_t seed)
{
    int tones = baseTones;
    const auto nibble = (seed >> 5) & 0x3u;
    if (nibble == 3u) ++tones;
    if (nibble == 0u) --tones;
    return std::clamp (tones, 3, 6);
}

inline SongPlan buildSongPlan (const MusicParams& p, int tones)
{
    const auto& st   = findStyle (p.genre);
    const int rootPc = theory::rootPitchClass (p.root);
    const auto ivals = theory::scaleIntervals (p.scale);
    const int base   = 12 * p.octave + rootPc;
    // seed 0 selects the style's published default template (index 0).
    const uint32_t seed = p.seed;

    auto bank = progressionBankForStyle (st);
    // Force index 0 to equal the style's published progression.
    if (! bank.empty())
        bank[0] = styleDefaultTemplate (st);
    else
        bank.push_back (styleDefaultTemplate (st));

    const int templateIndex = (int) (seed % (uint32_t) bank.size());
    const auto& tmpl = bank[(size_t) templateIndex];

    auto degrees = expandProgressionDegrees (tmpl.degrees, p.bars);

    bool usedSubstitution = false;
    int inversionBias = 0;
    colourProgression (degrees, seed, usedSubstitution, inversionBias);

    // 8-bar A/B: final bar of each 8-bar phrase gets a cadential turnaround
    // when the seed asks for it (always for seed bit, matching classic feel).
    bool usedTurnaround = false;
    const bool wantTurnaround = p.bars >= 8 && (((seed >> 1) & 1u) != 0u
                                                || templateIndex == 0);
    if (wantTurnaround)
    {
        for (int bar = 0; bar < p.bars; ++bar)
        {
            if ((bar % 8) == 7)
            {
                // Prefer cadence relative to the 4th bar of the phrase motif.
                const int phraseBase = (bar / 8) * 8;
                const int refIdx = std::min (phraseBase + 3, p.bars - 1);
                degrees[(size_t) bar] = cadenceDegreeFromLast (degrees[(size_t) refIdx]);
                usedTurnaround = true;
            }
        }
    }

    const int chordTones = tonesForSeed (tones, seed);

    SongPlan plan;
    plan.templateIndex = templateIndex;
    plan.inversionBias = inversionBias;
    plan.usedSubstitution = usedSubstitution;
    plan.usedTurnaround = usedTurnaround;

    std::vector<int> prev;
    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg = degrees[(size_t) bar];
        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, chordTones);
        chord = applyInversionColour (std::move (chord), inversionBias);
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
