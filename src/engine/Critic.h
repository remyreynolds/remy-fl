#pragma once

#include "MusicInstructions.h"
#include "MusicTheory.h"
#include "SongPlan.h"
#include "StylePresets.h"
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace aimidi
{
/** Cross-part critic.

    validate() (in MidiGenerator) polices each part in isolation — scale
    snapping, overlaps, ranges. The critic looks at the ARRANGEMENT: do the
    lanes agree with the song plan, does the bass fight the kick, are strong
    beats consonant? It repairs what it safely can and reports what it did,
    so no generation path (manual, style switch, or AI chat) dumps unchecked
    MIDI into the roll.

    JUCE-free and deterministic given the same inputs + parts. */
struct CriticReport
{
    int fixes = 0;
    std::vector<std::string> notes;

    std::string summary() const
    {
        if (fixes == 0 && notes.empty())
            return "Critic: clean pass, no repairs needed.";
        std::string s = "Critic: " + std::to_string (fixes) + " repair"
                      + (fixes == 1 ? "" : "s");
        for (const auto& n : notes) { s += " · "; s += n; }
        return s;
    }
};

namespace critic
{
    /** Melody-family lanes: strong beats (1 and 3) must land on chord tones
        of the bar's chord. Passing tones on weak beats are welcome. */
    inline void enforceChordTonesOnStrongBeats (GeneratedPart& part,
                                                const SongPlan& plan,
                                                CriticReport& rep)
    {
        if (part.locked) return; // locked lanes are the user's word
        for (auto& n : part.notes)
        {
            const double beatInBar = std::fmod (n.startBeats, 4.0);
            const bool strong = std::abs (beatInBar - 0.0) < 0.05
                             || std::abs (beatInBar - 2.0) < 0.05;
            if (! strong) continue;

            const auto& chord = chordAtBeat (plan, n.startBeats);
            if (! theory::isChordTone (n.pitch, chord))
            {
                n.pitch = theory::snapToChord (n.pitch, chord);
                ++rep.fixes;
            }
        }
    }

    /** Keep the bass in bass register (roughly E1..G3). */
    inline void clampBassRegister (GeneratedPart& bass, CriticReport& rep)
    {
        if (bass.locked) return;
        for (auto& n : bass.notes)
        {
            const int before = n.pitch;
            while (n.pitch > 55) n.pitch -= 12;
            while (n.pitch < 28) n.pitch += 12;
            if (n.pitch != before) ++rep.fixes;
        }
    }

    /** For busy bass styles, notes that land exactly on a kick hit get
        nudged to the next 1/16 so the low end doesn't smear. Sustained-sub
        and garage styles keep their downbeats (that clash is the genre). */
    inline void resolveKickBassClashes (GeneratedPart& bass,
                                        const GeneratedPart& kick,
                                        const MusicParams& p,
                                        CriticReport& rep)
    {
        if (bass.locked) return;
        const auto& st = findStyle (p.genre);
        if (st.bassStyle == BassStyle::SubSustained
         || st.bassStyle == BassStyle::GarageSub)
            return;

        for (auto& b : bass.notes)
            for (const auto& k : kick.notes)
                if (std::abs (b.startBeats - k.startBeats) < 0.06)
                {
                    b.startBeats = k.startBeats + 0.25;
                    ++rep.fixes;
                    break;
                }
    }

    /** Flag (don't silently mangle) arrangement-level oddities. */
    inline void reportDensity (const std::array<GeneratedPart,
                                   (size_t) InstrumentType::NumTypes>& parts,
                               CriticReport& rep)
    {
        int sustained = 0;
        for (const auto& pt : parts)
            if ((pt.type == InstrumentType::Chords || pt.type == InstrumentType::Pad)
                && ! pt.muted)
                for (const auto& n : pt.notes)
                    if (n.startBeats < 0.05) ++sustained;

        if (sustained > 12)
            rep.notes.push_back ("very dense chord stack (" + std::to_string (sustained)
                                 + " simultaneous notes) — consider muting Pad or Chords");
    }
} // namespace critic

/** Review + repair the whole arrangement in place. */
inline CriticReport reviewArrangement (
    std::array<GeneratedPart, (size_t) InstrumentType::NumTypes>& parts,
    std::array<GeneratedPart, (size_t) DrumPiece::NumPieces>& drumKit,
    const MusicParams& p)
{
    CriticReport rep;
    const auto& st = findStyle (p.genre);
    const auto plan = buildSongPlan (p, st.chordTones);

    critic::enforceChordTonesOnStrongBeats (parts[(size_t) InstrumentType::Melody],        plan, rep);
    critic::enforceChordTonesOnStrongBeats (parts[(size_t) InstrumentType::CounterMelody], plan, rep);

    auto& bass = parts[(size_t) InstrumentType::Bass];
    critic::clampBassRegister (bass, rep);
    critic::resolveKickBassClashes (bass, drumKit[(size_t) DrumPiece::Kick], p, rep);

    critic::reportDensity (parts, rep);
    return rep;
}

} // namespace aimidi
