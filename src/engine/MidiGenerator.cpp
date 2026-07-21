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
    GeneratedPart part;
    const int rootPc  = rootPitchClass (p.root);
    const auto ivals  = scaleIntervals (p.scale);
    const int base    = 12 * (p.octave) + rootPc; // e.g. octave 4 -> ~C4 region

    // A classic minor house progression by scale degree: i - VI - III - VII
    const int degrees[] = { 0, 5, 2, 6 };
    const bool seventh = p.chordComplexity > 0.5f;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg = degrees[bar % 4];
        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, seventh);
        const double start = bar * 4.0; // one chord per bar (4 beats)

        // Stab vs sustain based on energy: high energy = shorter punchy stabs.
        const double len = p.energy > 0.6f ? 0.5 : 3.5;
        for (int n : chord)
            part.notes.push_back ({ start, len, n, 0.75f });

        // Optional off-beat stab for energetic house
        if (p.energy > 0.6f)
            for (int n : chord)
                part.notes.push_back ({ start + 2.0, 0.5, n, 0.6f });
    }
    return part;
}

GeneratedPart MidiGenerator::generateBass (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int base   = 12 * (p.octave - 2) + rootPc; // low register

    const int degrees[] = { 0, 5, 2, 6 };
    // 1/8-note offbeat house bass with gaps (avoid clashing with the kick).
    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg  = degrees[bar % 4];
        const int note = base + ivals[(size_t) (deg % (int) ivals.size())];
        for (int eighth = 0; eighth < 8; ++eighth)
        {
            // Skip downbeats (where kick lands): play the "and" of each beat.
            if (eighth % 2 == 0) continue;
            if (rand01() > 0.85f) continue; // occasional gap for groove
            const double start = bar * 4.0 + eighth * 0.5;
            part.notes.push_back ({ start, 0.45, note, 0.85f });
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

    const int degrees[] = { 0, 2, 4, 6 }; // arpeggiated triad+7 tones
    const double step = 0.25; // 1/16 arp
    const int totalSteps = (int) std::round ((p.bars * 4.0) / step);
    for (int s = 0; s < totalSteps; ++s)
    {
        const int deg  = degrees[s % 4];
        const int oct  = (s / 4) % 2;
        const int note = base + oct * 12 + ivals[(size_t) (deg % (int) ivals.size())];
        part.notes.push_back ({ s * step, step * 0.9, note, 0.7f });
    }
    return part;
}

GeneratedPart MidiGenerator::generatePad (const MusicParams& p)
{
    // Sustained version of the chord part.
    auto part = generateChords (p);
    for (auto& n : part.notes) { n.lengthBeats = 4.0; n.velocity = 0.55f; }
    // collapse offbeat stabs -> keep only bar-aligned notes
    part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(),
        [] (const NoteEvent& n) { return std::fmod (n.startBeats, 4.0) > 0.01; }),
        part.notes.end());
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

    auto& kickPart  = kit[(size_t) DrumPiece::Kick];
    auto& snarePart = kit[(size_t) DrumPiece::Snare];
    auto& clapPart  = kit[(size_t) DrumPiece::Clap];
    auto& chatPart  = kit[(size_t) DrumPiece::ClosedHat];
    auto& ohatPart  = kit[(size_t) DrumPiece::OpenHat];

    const int kick      = drumPieceMidiNote (DrumPiece::Kick);
    const int snare     = drumPieceMidiNote (DrumPiece::Snare);
    const int clap      = drumPieceMidiNote (DrumPiece::Clap);
    const int closedHat = drumPieceMidiNote (DrumPiece::ClosedHat);
    const int openHat   = drumPieceMidiNote (DrumPiece::OpenHat);

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const double b0 = bar * 4.0;

        // Four-on-the-floor kick
        for (int beat = 0; beat < 4; ++beat)
            kickPart.notes.push_back ({ b0 + beat, 0.25, kick, 0.95f });

        // Clap/snare on 2 and 4
        clapPart.notes.push_back  ({ b0 + 1.0, 0.25, clap,  0.85f });
        snarePart.notes.push_back ({ b0 + 3.0, 0.25, snare, 0.8f });

        // Offbeat open hat (the house "tss")
        for (int beat = 0; beat < 4; ++beat)
            ohatPart.notes.push_back ({ b0 + beat + 0.5, 0.2, openHat, 0.6f });

        // 1/16 closed hats, energy-scaled
        for (int s = 0; s < 16; ++s)
        {
            if (rand01() > 0.4f + p.energy * 0.5f) continue;
            chatPart.notes.push_back ({ b0 + s * 0.25, 0.1, closedHat,
                                        0.4f + rand01() * 0.3f });
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

        // Swing: push the "and" of the beat later.
        const double frac = std::fmod (n.startBeats, 1.0);
        if (std::abs (frac - 0.5) < 1e-3)
            n.startBeats += p.swing * 0.15;

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
