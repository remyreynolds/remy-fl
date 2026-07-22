#include "MidiGenerator.h"
#include "MusicTheory.h"
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
        case InstrumentType::CounterMelody: part = generateMelody (params); break;
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
    const auto& st    = findStyle (p.genre);
    const int rootPc  = rootPitchClass (p.root);
    const auto ivals  = scaleIntervals (p.scale);
    const int base    = 12 * (p.octave) + rootPc; // e.g. octave 4 -> ~C4 region

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg = st.progression[(size_t) (bar % 4)];
        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, tones);
        const double b0 = bar * 4.0;

        switch (mode)
        {
            case ChordMode::Sustained:
                for (int n : chord)
                    part.notes.push_back ({ b0, 3.9, n, 0.65f });
                break;

            case ChordMode::Stabs:
            {
                // Sparse, punchy hits — extra syncopated hit at high energy.
                for (int n : chord)
                {
                    part.notes.push_back ({ b0,        0.4, n, 0.8f });
                    part.notes.push_back ({ b0 + 2.5,  0.35, n, 0.7f });
                    if (p.energy > 0.65f)
                        part.notes.push_back ({ b0 + 1.75, 0.3, n, 0.6f });
                }
                break;
            }

            case ChordMode::OffbeatStabs:
                // Classic house piano/organ: stab on every "and".
                for (int beat = 0; beat < 4; ++beat)
                    for (int n : chord)
                        part.notes.push_back ({ b0 + beat + 0.5, 0.45, n,
                                                0.68f + rand01() * 0.12f });
                break;

            case ChordMode::Plucked:
                // Rhythmic short plucks on 1/8s, density-gated.
                for (int eighth = 0; eighth < 8; ++eighth)
                {
                    if (rand01() > 0.35f + p.noteDensity * 0.35f) continue;
                    for (int n : chord)
                        part.notes.push_back ({ b0 + eighth * 0.5, 0.3, n,
                                                0.5f + rand01() * 0.2f });
                }
                break;
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

    auto degreeNote = [&] (int bar) -> int
    {
        const int deg = st.progression[(size_t) (bar % 4)];
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
    const int base   = 12 * (p.octave + 1) + rootPc;

    const double stepBeats = p.rhythmComplexity > 0.6f ? 0.25 : 0.5; // 1/16 vs 1/8
    const int totalSteps   = (int) std::round ((p.bars * 4.0) / stepBeats);
    int lastDeg = 0;

    for (int s = 0; s < totalSteps; ++s)
    {
        const float density = 0.3f + p.noteDensity * 0.6f;
        if (rand01() > density) continue; // rest

        // Random walk over scale degrees for singable contour.
        const int move = (int) std::round ((rand01() - 0.5f) * 4.0f * (0.5f + p.complexity));
        lastDeg = std::clamp (lastDeg + move, 0, (int) ivals.size() * 2);
        const int oct  = lastDeg / (int) ivals.size();
        const int note = base + oct * 12 + ivals[(size_t) (lastDeg % (int) ivals.size())];

        part.notes.push_back ({ s * stepBeats, stepBeats * 0.9, note,
                                0.6f + rand01() * 0.3f });
    }
    return part;
}

GeneratedPart MidiGenerator::generateArp (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int base   = 12 * (p.octave + 1) + rootPc;

    // Arp follows the style's chord progression: each bar arpeggiates the
    // chord of that bar (root/3rd/5th/7th climbing, octave alternation).
    const auto& st = findStyle (p.genre);
    const int offsets[] = { 0, 2, 4, 6 };
    const double step = 0.25; // 1/16 arp
    const int totalSteps = (int) std::round ((p.bars * 4.0) / step);
    for (int s = 0; s < totalSteps; ++s)
    {
        const int bar  = (int) (s * step / 4.0);
        const int deg  = st.progression[(size_t) (bar % 4)] + offsets[s % 4];
        const int oct  = (s / 4) % 2 + deg / (int) ivals.size();
        const int note = base + oct * 12 + ivals[(size_t) (deg % (int) ivals.size())];
        part.notes.push_back ({ s * step, step * 0.9, note, 0.7f });
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
    for (auto& n : part.notes) { n.lengthBeats = 4.0; n.velocity = 0.55f; }
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
    std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> kit;
    for (auto& g : kit) g.type = InstrumentType::Drums;

    // Style-preset groove templates: 16 velocity steps per piece per bar,
    // probability-gated so every regenerate breathes a little differently.
    const auto& st = findStyle (p.genre);

    for (int pieceIdx = 0; pieceIdx < (int) DrumPiece::NumPieces; ++pieceIdx)
    {
        const auto piece   = (DrumPiece) pieceIdx;
        const auto& pat    = st.drums[(size_t) pieceIdx];
        const int midiNote = drumPieceMidiNote (piece);

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
        if (std::abs (frac - 0.5) < 1e-3)
            n.startBeats += p.swing * 0.15;
        else if (std::abs (frac - 0.25) < 1e-3 || std::abs (frac - 0.75) < 1e-3)
            n.startBeats += p.swing * 0.08;

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
    const int ch = part.type == InstrumentType::Drums ? 10 : 1;

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

} // namespace aimidi
