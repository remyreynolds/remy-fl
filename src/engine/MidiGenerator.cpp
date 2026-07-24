#include "MidiGenerator.h"
#include "MidiDna.h"
#include "MusicTheory.h"
#include "SongPlan.h"
#include <algorithm>
#include <cmath>

namespace aimidi
{
using theory::scaleIntervals;
using theory::rootPitchClass;
using theory::snapToScale;

//==============================================================================
GeneratedPart MidiGenerator::generate (InstrumentType type, const MusicParams& params)
{
    rng.seed (params.seed != 0 ? params.seed : std::random_device{}());

    GeneratedPart part;
    switch (type)
    {
        case InstrumentType::Chords:        part = generateChords (params); break;
        case InstrumentType::Bass:          part = generateBass   (params); break;
        case InstrumentType::Melody:        part = generateMelody (params); break;
        case InstrumentType::CounterMelody: part = generateCounterMelody (params); break;
        case InstrumentType::Drums:         part = generateDrums  (params); break;
        case InstrumentType::Arp:           part = generateArp    (params); break;
        case InstrumentType::Pad:           part = generatePad    (params); break;
        default:                            part = generateMelody (params); break;
    }
    part.type = type;
    // Drums are validated per-piece inside generateDrumKit() already —
    // re-validating the flattened blob here would double-apply swing/humanize.
    if (type != InstrumentType::Drums)
        validate (part, params);
    return part;
}

//==============================================================================
GeneratedPart MidiGenerator::generateChords (const MusicParams& p)
{
    const auto& st = findStyle (p.genre);

    // chordComplexity dial nudges the preset's voicing richer/simpler.
    int tones = st.chordTones;
    if (p.chordComplexity > 0.75f) ++tones;
    if (p.chordComplexity < 0.25f) --tones;
    tones = std::clamp (tones, 3, 6);

    return generateChordsWithMode (p, st.chordMode, tones);
}

GeneratedPart MidiGenerator::generateChordsWithMode (const MusicParams& p,
                                                     ChordMode mode, int tones)
{
    GeneratedPart part;

    // All harmony comes from the shared song plan — the same deterministic
    // skeleton bass/arp/melody/critic use, with bar-to-bar voice-leading.
    const auto plan = buildSongPlan (p, tones);

    // Pick a groove contour for this take (seeded via rng). House chords should
    // change placement + length across bars, not stamp the same grid forever.
    const int groovePick = (int) (rand01() * 4.0f) % 4;
    const float dens = std::clamp (p.noteDensity, 0.0f, 1.0f);
    const float energy = std::clamp (p.energy, 0.0f, 1.0f);
    const float rhythm = std::clamp (p.rhythmComplexity, 0.0f, 1.0f);

    auto pushVoicing = [&] (double start, double len, const std::vector<int>& chord,
                            float velBase, bool thin = false)
    {
        if (chord.empty() || len <= 0.05) return;
        const double startClamped = std::max (0.0, start);
        for (size_t i = 0; i < chord.size(); ++i)
        {
            // Occasionally omit an upper extension so voicings aren't identical slabs.
            if (thin && i + 1 == chord.size() && chord.size() >= 4 && rand01() < 0.45f)
                continue;
            if (thin && i == 1 && chord.size() >= 3 && rand01() < 0.2f)
                continue;
            const float vel = std::clamp (velBase + (rand01() - 0.5f) * 0.12f, 0.35f, 0.95f);
            const double noteLen = std::max (0.08, len * (0.88 + rand01() * 0.2));
            part.notes.push_back ({ startClamped, noteLen, chord[i], vel });
        }
    };

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const auto& chord = plan.chords[(size_t) bar];
        const double b0 = bar * 4.0;
        // Per-bar micro-variation so consecutive bars don't clone each other.
        const double barNudge = (rand01() - 0.5) * 0.03 * (0.35 + p.humanize);

        switch (mode)
        {
            case ChordMode::Sustained:
            {
                // Hold + optional mid-bar reattack / breath — lengths vary.
                const int shape = (groovePick + bar) % 3;
                if (shape == 0)
                {
                    // Long pad with a slight early cut so the next bar can breathe.
                    const double len = 3.2 + rand01() * 0.7; // 3.2–3.9
                    pushVoicing (b0 + barNudge, len, chord, 0.58f + energy * 0.1f);
                }
                else if (shape == 1)
                {
                    // Two holds: downbeat + pickup into next phrase.
                    const double a = 1.6 + rand01() * 0.7;
                    pushVoicing (b0 + barNudge, a, chord, 0.62f);
                    if (rand01() < 0.55f + dens * 0.3f)
                        pushVoicing (b0 + a + 0.15 + rand01() * 0.25, 1.4 + rand01() * 0.8,
                                     chord, 0.52f, true);
                }
                else
                {
                    // Anticipation into the bar + sustained body.
                    if (bar > 0 && rand01() < 0.4f + rhythm * 0.3f)
                        pushVoicing (b0 - (0.2 + rand01() * 0.15), 0.25 + rand01() * 0.2,
                                     chord, 0.48f, true);
                    pushVoicing (b0 + 0.05 + barNudge, 3.0 + rand01() * 0.8, chord, 0.6f);
                }
                break;
            }

            case ChordMode::Stabs:
            {
                // Choose hit slots inside the bar (beats), not a fixed 1 + 2.5 forever.
                struct Hit { double at; double len; float vel; };
                std::vector<Hit> hits;
                const int pattern = (groovePick + bar * 3) % 5;
                switch (pattern)
                {
                    case 0: // classic sparse
                        hits = { { 0.0, 0.35 + rand01() * 0.25, 0.82f },
                                 { 2.0 + rand01() * 0.5, 0.28 + rand01() * 0.22, 0.7f } };
                        break;
                    case 1: // syncopated &-heavy
                        hits = { { 0.5 + rand01() * 0.1, 0.3 + rand01() * 0.2, 0.78f },
                                 { 1.5 + rand01() * 0.15, 0.25 + rand01() * 0.2, 0.66f },
                                 { 3.0 + rand01() * 0.2, 0.3 + rand01() * 0.2, 0.72f } };
                        break;
                    case 2: // four-on-the-and with gaps
                        for (double t : { 0.5, 1.5, 2.5, 3.5 })
                            if (rand01() < 0.55f + dens * 0.35f)
                                hits.push_back ({ t + (rand01() - 0.5) * 0.06,
                                                  0.22 + rand01() * 0.28,
                                                  0.62f + rand01() * 0.2f });
                        break;
                    case 3: // long then short
                        hits = { { 0.0, 0.9 + rand01() * 0.7, 0.8f },
                                 { 2.75 + rand01() * 0.2, 0.2 + rand01() * 0.25, 0.68f } };
                        break;
                    default: // clustered turnaround feel
                        hits = { { 0.0, 0.4 + rand01() * 0.3, 0.8f },
                                 { 1.0 + rand01() * 0.25, 0.25 + rand01() * 0.2, 0.65f },
                                 { 2.25 + rand01() * 0.2, 0.35 + rand01() * 0.3, 0.74f } };
                        if (energy > 0.55f && rand01() < 0.6f)
                            hits.push_back ({ 3.5 + rand01() * 0.1, 0.18 + rand01() * 0.15, 0.6f });
                        break;
                }

                if (hits.empty())
                    hits.push_back ({ 0.0, 0.4, 0.75f });

                for (const auto& h : hits)
                    pushVoicing (b0 + h.at + barNudge, h.len, chord, h.vel,
                                 rhythm > 0.45f && rand01() < 0.35f);
                break;
            }

            case ChordMode::OffbeatStabs:
            {
                // Offbeat house, but skip some "&"s and vary gate length.
                for (int beat = 0; beat < 4; ++beat)
                {
                    const float keepProb = 0.55f + dens * 0.35f
                                         - (beat == 0 ? 0.0f : 0.05f);
                    if (rand01() > keepProb) continue;
                    const double at = b0 + beat + 0.5
                                    + (rand01() - 0.5) * 0.05 * (0.4 + p.humanize);
                    const double len = (rand01() < 0.3f + rhythm * 0.3f)
                                         ? (0.55 + rand01() * 0.55)   // longer stab
                                         : (0.22 + rand01() * 0.28);  // short stab
                    const float vel = 0.62f + (beat % 2 == 0 ? 0.1f : 0.0f)
                                    + rand01() * 0.12f;
                    pushVoicing (at, len, chord, vel, rand01() < 0.25f);
                }
                // Occasional downbeat punch for variety.
                if (rand01() < 0.25f + energy * 0.25f)
                    pushVoicing (b0 + barNudge, 0.2 + rand01() * 0.25, chord, 0.78f, true);
                break;
            }

            case ChordMode::Plucked:
            {
                // Mix of 1/8 and 1/16 cells, irregular density, variable gates.
                const int steps = (rhythm > 0.55f || rand01() < 0.4f) ? 16 : 8;
                const double step = 4.0 / (double) steps;
                for (int s = 0; s < steps; ++s)
                {
                    float keep = 0.25f + dens * 0.45f;
                    if (s % 4 == 0) keep += 0.12f;          // favor downbeat cells
                    if (s % 2 == 1) keep += rhythm * 0.1f;  // syncopation when complex
                    if (rand01() > keep) continue;
                    const double at = b0 + s * step
                                    + (rand01() - 0.5) * 0.03 * (0.3 + p.humanize);
                    const double len = (rand01() < 0.2f)
                                         ? (0.45 + rand01() * 0.4)
                                         : (0.12 + rand01() * 0.22);
                    pushVoicing (at, len, chord, 0.48f + rand01() * 0.28f,
                                 rand01() < 0.4f);
                }
                break;
            }
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generateBass (const MusicParams& p)
{
    GeneratedPart part;
    const auto& st   = findStyle (p.genre);
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int base   = 12 * (p.octave - 2) + rootPc; // low register

    // Roots come from the shared song plan (NOT the raw preset progression)
    // so the bass also follows the cadential turnaround bar in 8-bar phrases.
    const auto plan = buildSongPlan (p, st.chordTones);

    auto degreeNote = [&] (int bar) -> int
    {
        const int deg = plan.degrees[(size_t) std::min (bar,
                            (int) plan.degrees.size() - 1)];
        return base + ivals[(size_t) (deg % (int) ivals.size())];
    };

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int note  = degreeNote (bar);

        switch (st.bassStyle)
        {
            case BassStyle::OffbeatEighths:
                // Classic house: play the "and" of each beat, occasional gap.
                for (int eighth = 1; eighth < 8; eighth += 2)
                {
                    if (rand01() > 0.85f) continue;
                    part.notes.push_back ({ b0 + eighth * 0.5, 0.45, note, 0.85f });
                }
                break;

            case BassStyle::RollingSixteenths:
                // Tech house: relentless 16ths (downbeats left to the kick),
                // with octave pops for movement.
                for (int s = 0; s < 16; ++s)
                {
                    if (s % 4 == 0) continue;               // leave kick space
                    if (rand01() > 0.92f) continue;          // rare gap
                    const bool octavePop = (s % 8 == 6) && rand01() < 0.6f;
                    part.notes.push_back ({ b0 + s * 0.25, 0.22,
                                            note + (octavePop ? 12 : 0),
                                            s % 2 == 0 ? 0.85f : 0.7f });
                }
                break;

            case BassStyle::SubSustained:
                // Deep/organic: long low subs, occasional pickup note.
                part.notes.push_back ({ b0, 2.4, note, 0.8f });
                if (rand01() < 0.6f)
                    part.notes.push_back ({ b0 + 2.5, 1.3, note, 0.7f });
                break;

            case BassStyle::StabSyncopated:
                // Afro house: syncopated stabs between the kicks.
                for (int s : { 3, 6, 11, 14 })
                {
                    if (rand01() > 0.85f) continue;
                    part.notes.push_back ({ b0 + s * 0.25, 0.4, note, 0.85f });
                }
                break;

            case BassStyle::GarageSub:
            {
                // UKG: 2-step sub with pitch movement (root -> 5th -> b7).
                const int offsets[] = { 0, 0, 7, 0, 10 };
                const int steps[]   = { 0, 5, 8, 11, 14 };
                for (int i = 0; i < 5; ++i)
                {
                    if (rand01() > 0.9f) continue;
                    part.notes.push_back ({ b0 + steps[i] * 0.25, 0.4,
                                            note + offsets[i], 0.85f });
                }
                break;
            }
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generateMelody (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int nIvals = (int) ivals.size();
    const int base   = 12 * (p.octave + 1) + rootPc;

    // Harmony anchor: the shared song plan. Strong beats sit on chord tones,
    // weak beats move stepwise (passing tones), and the last note of each
    // bar APPROACHES the next bar's chord tone by step — so the line pulls
    // into every chord change instead of wandering (tension -> resolution).
    const auto& st  = findStyle (p.genre);
    const auto plan = buildSongPlan (p, st.chordTones);

    const double stepBeats = (p.rhythmComplexity > 0.6f || rand01() < 0.35f) ? 0.25 : 0.5;
    const int stepsPerBar  = (int) std::round (4.0 / stepBeats);
    const int stepsPerBeat = (int) std::round (1.0 / stepBeats);

    // Absolute scale grid: index i -> a real MIDI note, monotonically rising.
    auto noteAt = [&] (int idx) -> int
    {
        idx = std::clamp (idx, 0, nIvals * 10 - 1);
        return rootPc + (idx / nIvals) * 12 + ivals[(size_t) (idx % nIvals)];
    };
    auto nearestIdx = [&] (int note) -> int
    {
        int best = 0, bestD = 999;
        for (int i = 0; i < nIvals * 10; ++i)
        {
            const int d = std::abs (noteAt (i) - note);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    };

    // Build a one-bar rhythmic motif, then repeat it with variation —
    // this is what makes it sound like a hook instead of dice rolls.
    std::vector<bool> motif ((size_t) stepsPerBar, false);
    const float density = 0.25f + p.noteDensity * 0.5f;
    for (int s = 0; s < stepsPerBar; ++s)
        motif[(size_t) s] = rand01() < density;
    motif[0] = true; // anchor the downbeat — the hook needs a home

    const int loIdx = nearestIdx (base - 3);
    const int hiIdx = nearestIdx (base + 19);
    int curIdx = nearestIdx (theory::snapToChord (base, plan.chords[0]));

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const auto& chord     = plan.chords[(size_t) std::min (bar, (int) plan.chords.size() - 1)];
        const bool  lastBar   = bar == p.bars - 1;
        const auto* nextChord = lastBar ? &plan.chords[0]
                                        : &plan.chords[(size_t) std::min (bar + 1,
                                              (int) plan.chords.size() - 1)];

        // Which steps sound this bar (motif +/- variation)?
        std::vector<int> active;
        for (int s = 0; s < stepsPerBar; ++s)
        {
            bool play = motif[(size_t) s];
            if (s != 0 && rand01() < 0.15f) play = ! play; // per-bar variation
            if (play) active.push_back (s);
        }

        for (size_t a = 0; a < active.size(); ++a)
        {
            const int s = active[a];
            const bool strong    = (s % stepsPerBeat) == 0;   // on a beat
            const bool lastOfBar = a == active.size() - 1;
            int note;

            if (lastBar && lastOfBar)
            {
                // Phrase resolution: land the final note on a tone of the
                // loop's opening chord and let it ring — the loop breathes
                // out, then pulls back around.
                note = theory::snapToChord (noteAt (curIdx), *nextChord);
                curIdx = nearestIdx (note);
                // Ring, but never past the loop boundary — a tail that spills
                // over the end overlaps the loop's own downbeat on repeat.
                const double start  = bar * 4.0 + s * stepBeats;
                const double maxLen = p.bars * 4.0 - start;
                part.notes.push_back ({ start,
                                        std::min (stepBeats * 3.0, maxLen), note, 0.75f });
                continue;
            }

            if (lastOfBar && ! chord.empty())
            {
                // Approach note: one scale step above/below the tone this
                // line resolves to at the next bar's downbeat.
                const int target = theory::snapToChord (noteAt (curIdx), *nextChord);
                const int tIdx   = nearestIdx (target);
                curIdx = tIdx + (rand01() < 0.5f ? 1 : -1);
                note   = noteAt (curIdx);
            }
            else if (strong)
            {
                // Chord tone on the beat (small chord-tone leaps allowed).
                const int move = randInt (-2, 2);
                note   = theory::snapToChord (noteAt (curIdx + move), chord);
                curIdx = nearestIdx (note);
            }
            else
            {
                // Weak beat: stepwise passing motion through the scale.
                const int move = rand01() < 0.5f ? 1 : -1;
                const int wide = rand01() < p.complexity * 0.4f ? 2 : 1;
                curIdx = std::clamp (curIdx + move * wide, loIdx, hiIdx);
                note   = noteAt (curIdx);
            }

            curIdx = std::clamp (curIdx, loIdx, hiIdx);

            // Avoid-note handling: a passing tone one semitone above a chord
            // tone is fine as colour but shouldn't sustain — shorten it.
            const bool rub = ! strong && ! chord.empty()
                          && theory::isChordTone (note - 1, chord)
                          && ! theory::isChordTone (note, chord);
            const double gate = rub ? 0.45 : 0.6 + rand01() * 0.35;

            part.notes.push_back ({ bar * 4.0 + s * stepBeats, stepBeats * gate,
                                    note, 0.6f + rand01() * 0.3f });
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generateCounterMelody (const MusicParams& p)
{
    // Call-and-response: rebuild the melody with the SAME dice (identical
    // seed path), find its gaps, and answer INSIDE them — the two lines
    // converse instead of talking over each other.
    auto melody = generateMelody (p);

    // The answers get their own dice so they aren't a copy of the call.
    rng.seed ((p.seed != 0 ? p.seed : std::random_device{}()) ^ 0x9e3779b9u);

    const auto& st  = findStyle (p.genre);
    const auto plan = buildSongPlan (p, st.chordTones);

    std::sort (melody.notes.begin(), melody.notes.end(),
               [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeats < b.startBeats; });

    GeneratedPart part;
    const double loopEnd = p.bars * 4.0;

    auto answer = [&] (double from, double to)
    {
        if (to - from < 1.0) return;               // only real gaps
        const auto& chord = chordAtBeat (plan, from);
        if (chord.empty()) return;

        // 1-3 short chord-tone notes walking up or down through the gap.
        const int count = std::min (3, 1 + (int) ((to - from) / 1.0));
        const int dir   = rand01() < 0.5f ? 1 : -1;
        int idx = randInt (0, (int) chord.size() - 1);
        for (int i = 0; i < count; ++i)
        {
            if (rand01() < 0.25f) continue;        // answers stay sparse
            const double t = from + 0.25 + i * 0.5;
            if (t + 0.4 > to) break;
            idx = (idx + dir + (int) chord.size()) % (int) chord.size();
            part.notes.push_back ({ t, 0.4, chord[(size_t) idx] + 12,
                                    0.45f + rand01() * 0.15f });
        }
    };

    double cursor = 0.0;
    for (const auto& n : melody.notes)
    {
        answer (cursor, n.startBeats);
        cursor = std::max (cursor, n.startBeats + n.lengthBeats);
    }
    answer (cursor, loopEnd);
    return part;
}

GeneratedPart MidiGenerator::generateArp (const MusicParams& p)
{
    GeneratedPart part;

    // Arp climbs the voice-led chord of the current bar (from the shared
    // song plan) with octave alternation — always consonant with the other
    // lanes by construction. Sits one octave above the chord register.
    const auto& st = findStyle (p.genre);
    const auto plan = buildSongPlan (p, st.chordTones);
    const double step = 0.25; // 1/16 arp
    const int totalSteps = (int) std::round ((p.bars * 4.0) / step);
    for (int s = 0; s < totalSteps; ++s)
    {
        const int bar     = (int) (s * step / 4.0);
        const auto& chord = plan.chords[(size_t) std::min (bar, (int) plan.chords.size() - 1)];
        if (chord.empty()) continue;
        const int idx = s % (int) chord.size();
        const int oct = 12 + ((s / (int) chord.size()) % 2) * 12;
        // Accent the downbeat 1/16s so the arp pulses with the groove
        // instead of sounding like a flat sequencer run.
        const float vel = (s % 4 == 0) ? 0.8f : 0.6f;
        part.notes.push_back ({ s * step, step * 0.9, chord[(size_t) idx] + oct, vel });
    }
    return part;
}

GeneratedPart MidiGenerator::generatePad (const MusicParams& p)
{
    // Pads are always the sustained voicing of the current progression,
    // regardless of the style's chord rhythm.
    const auto& st = findStyle (p.genre);
    auto part = generateChordsWithMode (p, ChordMode::Sustained,
                                        std::clamp (st.chordTones, 3, 5));
    // 3.95 (not a full 4.0) so back-to-back bars retrigger cleanly instead
    // of note-off/note-on landing on the exact same tick in the DAW.
    for (auto& n : part.notes) { n.lengthBeats = 3.95; n.velocity = 0.55f; }
    return part;
}

GeneratedPart MidiGenerator::generateDrums (const MusicParams& p)
{
    // Combined "whole loop" view — used when the AI/UI regenerates the kit
    // as a unit and for the single-file "drag full loop" convenience export.
    // The real per-piece patterns live in generateDrumKit(); this just
    // flattens the (already-validated) pieces onto one timeline.
    GeneratedPart part;
    part.type = InstrumentType::Drums;

    auto kit = generateDrumKit (p);
    for (auto& piece : kit)
        for (auto& n : piece.notes)
            part.notes.push_back (n);

    std::sort (part.notes.begin(), part.notes.end(),
               [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeats < b.startBeats; });
    return part;
}

std::array<GeneratedPart, (size_t) DrumPiece::NumPieces>
    MidiGenerator::generateDrumKit (const MusicParams& p)
{
    // Seed here too: this is a public entry point (UI piece-reroll, critic),
    // not only reached via generate() — without this the kit was
    // non-deterministic and generateDrumPiece couldn't reproduce the kit.
    rng.seed ((p.seed != 0 ? p.seed : std::random_device{}()) ^ 0x517cc1b7u);

    std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> kit;
    for (auto& g : kit) g.type = InstrumentType::Drums;

    // Style-preset groove templates: 16 velocity steps per piece per bar,
    // probability-gated so every regenerate breathes a little differently.
    const auto& st = findStyle (p.genre);

    for (int pieceIdx = 0; pieceIdx < (int) DrumPiece::NumPieces; ++pieceIdx)
    {
        const auto piece   = (DrumPiece) pieceIdx;
        const int midiNote = drumPieceMidiNote (piece);

        // MIDI-pack DNA: when the user loaded a reference MIDI file, its
        // extracted groove replaces the preset pattern for any piece the
        // reference actually plays; pieces it doesn't cover keep the style.
        const DrumPattern* patPtr = &st.drums[(size_t) pieceIdx];
        DrumPattern dnaPat;
        if (dna != nullptr && dna->hasDrums)
        {
            bool any = false;
            for (float v : dna->drumSteps[(size_t) pieceIdx])
                if (v > 0.0f) { any = true; break; }
            if (any)
            {
                dnaPat.steps = dna->drumSteps[(size_t) pieceIdx];
                dnaPat.probability = 0.95f;
                patPtr = &dnaPat;
            }
        }
        const auto& pat = *patPtr;

        // Hats and shakers get denser with the energy dial; core pieces
        // (kick/snare/clap) stay rock solid.
        const bool energySensitive = piece == DrumPiece::ClosedHat
                                  || piece == DrumPiece::Shaker
                                  || piece == DrumPiece::Ride;
        const float probScale = energySensitive ? (0.55f + p.energy * 0.5f) : 1.0f;

        for (int bar = 0; bar < p.bars; ++bar)
        {
            const double b0 = bar * 4.0;
            for (int s = 0; s < 16; ++s)
            {
                const float v = pat.steps[(size_t) s];
                if (v <= 0.0f) continue;
                if (rand01() > pat.probability * probScale) continue;

                const float vel = std::clamp (v * (0.85f + p.energy * 0.25f),
                                              0.05f, 1.0f);
                kit[(size_t) pieceIdx].notes.push_back (
                    { b0 + s * 0.25, 0.15, midiNote, vel });
            }
        }
    }

    // Turnaround fill: last beat of every 4th bar gets a velocity-ramped
    // 16th run on the snare (or clap, for styles with no snare pattern) —
    // real sub-genre identity needs phrase punctuation, not endless loops.
    {
        bool styleHasSnare = false;
        for (float v : st.drums[(size_t) DrumPiece::Snare].steps)
            if (v > 0.0f) { styleHasSnare = true; break; }

        const auto fillPiece = styleHasSnare ? DrumPiece::Snare : DrumPiece::Clap;
        const int fillNote   = drumPieceMidiNote (fillPiece);

        for (int bar = 3; bar < p.bars; bar += 4)
        {
            const double b0 = bar * 4.0;
            for (int s = 12; s < 16; ++s)
            {
                if (rand01() > 0.5f + p.energy * 0.4f) continue;
                const float vel = 0.35f + 0.13f * (float) (s - 12); // ramp up
                kit[(size_t) fillPiece].notes.push_back (
                    { b0 + s * 0.25, 0.12, fillNote, vel });
            }
        }

        // Ghost notes: barely-audible snare/rim ticks on the "a" before the
        // backbeats — the human wrist a drum machine doesn't have. Scaled by
        // the humanize dial so a fully quantized feel is still available.
        const auto ghostPiece = styleHasSnare ? DrumPiece::Snare : DrumPiece::Rim;
        const int ghostNote   = drumPieceMidiNote (ghostPiece);
        const float ghostProb = 0.35f * (0.4f + p.humanize);
        for (int bar = 0; bar < p.bars; ++bar)
        {
            const double b0 = bar * 4.0;
            for (int s : { 3, 7, 11 })
            {
                if (rand01() > ghostProb) continue;
                kit[(size_t) ghostPiece].notes.push_back (
                    { b0 + s * 0.25, 0.08, ghostNote,
                      0.20f + rand01() * 0.15f });
            }
        }
    }

    for (auto& piece : kit)
        validate (piece, p);

    return kit;
}

GeneratedPart MidiGenerator::generateDrumPiece (DrumPiece piece, const MusicParams& p)
{
    // Regenerate the whole kit's worth of pattern logic (cheap — a handful
    // of notes) but only hand back the one piece the caller asked for, so
    // the other pieces are left completely untouched by this call.
    auto kit = generateDrumKit (p);
    return kit[(size_t) piece];
}

//==============================================================================
int MidiGenerator::validate (GeneratedPart& part, const MusicParams& p)
{
    int repaired = 0;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const bool pitched = part.type != InstrumentType::Drums;

    // Apply swing to offbeat 1/8s and humanization to timing/velocity.
    for (auto& n : part.notes)
    {
        if (pitched)
        {
            const int snapped = snapToScale (n.pitch, rootPc, ivals);
            if (snapped != n.pitch) { n.pitch = snapped; ++repaired; }
            n.pitch = std::clamp (n.pitch, 0, 127);
        }

        // Swing: push the "and" of the beat later (1/8 swing), and give the
        // 2nd/4th 1/16 of each beat a lighter push (shuffle feel — essential
        // for UK garage and swung deep/afro grooves).
        const double frac = std::fmod (n.startBeats, 1.0);
        const bool offbeat8  = std::abs (frac - 0.5) < 1e-3;
        const bool sixteenth = std::abs (frac - 0.25) < 1e-3
                            || std::abs (frac - 0.75) < 1e-3;
        if (offbeat8)
            n.startBeats += p.swing * 0.15;
        else if (sixteenth)
            n.startBeats += p.swing * 0.08;

        // Accent wave: downbeats strongest, offbeat 8ths medium, 16th
        // "e"/"a" lightest — a musical velocity contour instead of only
        // random jitter. Ghost notes (already very quiet) are left alone.
        if (n.velocity > 0.25f)
        {
            const float accent = offbeat8 ? 0.93f : sixteenth ? 0.85f : 1.0f;
            const float amount = 0.35f + 0.65f * p.humanize;
            n.velocity *= 1.0f + (accent - 1.0f) * amount;
        }

        // Humanize timing + velocity slightly.
        n.startBeats += (rand01() - 0.5f) * 0.02 * p.humanize;
        n.velocity    = std::clamp (n.velocity + (rand01() - 0.5f) * 0.2f * p.humanize,
                                    0.05f, 1.0f);
        if (n.startBeats < 0.0) n.startBeats = 0.0;
    }

    // Sort and de-overlap same-pitch notes.
    std::sort (part.notes.begin(), part.notes.end(),
               [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeats < b.startBeats; });

    for (size_t i = 0; i < part.notes.size(); ++i)
        for (size_t j = i + 1; j < part.notes.size(); ++j)
        {
            auto& a = part.notes[i];
            auto& b = part.notes[j];
            if (a.pitch == b.pitch && b.startBeats < a.startBeats + a.lengthBeats)
            {
                a.lengthBeats = std::max (0.05, b.startBeats - a.startBeats - 0.01);
                ++repaired;
            }
        }
    return repaired;
}

//==============================================================================
juce::MidiMessageSequence MidiGenerator::toSequence (const GeneratedPart& part, double /*bpm*/)
{
    juce::MidiMessageSequence seq;
    const int ch = midiChannelFor (part.type);

    for (const auto& n : part.notes)
    {
        const double onTick  = n.startBeats * ticksPerQuarter;
        const double offTick = (n.startBeats + n.lengthBeats) * ticksPerQuarter;
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::round (n.velocity * 127.0f));

        seq.addEvent (juce::MidiMessage::noteOn  (ch, n.pitch, vel), onTick);
        seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch),      offTick);
    }
    seq.updateMatchedPairs();
    return seq;
}

juce::File MidiGenerator::writeTempMidiFile (const GeneratedPart& part,
                                             const MusicParams& params,
                                             const juce::String& baseName)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ticksPerQuarter);

    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (
        (int) std::round (60'000'000.0 / params.bpm)), 0.0);
    mf.addTrack (tempo);

    mf.addTrack (toSequence (part, params.bpm));

    auto dir  = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("AIMidiGen");
    dir.createDirectory();
    auto file = dir.getChildFile (baseName + "_" +
                    juce::String (juce::Time::currentTimeMillis()) + ".mid");

    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        out->setPosition (0);
        out->truncate();
        mf.writeTo (*out);
        out->flush();
    }
    return file;
}

juce::File MidiGenerator::writeTempMultiTrackMidiFile (
    const std::vector<const GeneratedPart*>& parts,
    const MusicParams& params,
    const juce::String& baseName)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ticksPerQuarter);

    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (
        (int) std::round (60'000'000.0 / params.bpm)), 0.0);
    tempo.addEvent (juce::MidiMessage::textMetaEvent (3, "AI MIDI Gen"), 0.0);
    mf.addTrack (tempo);

    for (auto* part : parts)
    {
        if (part == nullptr || part->notes.empty())
            continue;

        auto seq = toSequence (*part, params.bpm);
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, toString (part->type)), 0.0);
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("AIMidiGen");
    dir.createDirectory();
    auto file = dir.getChildFile (baseName + "_" +
                                  juce::String (juce::Time::currentTimeMillis()) + ".mid");

    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        out->setPosition (0);
        out->truncate();
        mf.writeTo (*out);
        out->flush();
    }
    return file;
}

} // namespace aimidi
