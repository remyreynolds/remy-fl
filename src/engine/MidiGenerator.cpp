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
std::vector<int> MidiGenerator::pickProgression()
{
    // Curated pool of house progressions by scale degree (0 = tonic).
    // All idiomatic in minor/dorian house; validator snaps to scale anyway.
    static const std::vector<std::vector<int>> pool = {
        { 0, 5, 2, 6 },   // i - VI - III - VII  (classic deep house)
        { 0, 3, 4, 3 },   // i - iv - v - iv     (hypnotic)
        { 0, 6, 5, 3 },   // i - VII - VI - iv
        { 0, 0, 5, 6 },   // static tonic then lift
        { 5, 3, 0, 6 },   // VI - iv - i - VII   (starts away from home)
        { 0, 2, 5, 6 },   // i - III - VI - VII
        { 0, 4, 5, 3 },   // i - v - VI - iv
        { 3, 0, 6, 5 },   // iv - i - VII - VI
        { 0, 5, 3, 4 },   // i - VI - iv - v
        { 0, 2, 3, 6 },   // i - III - iv - VII
    };
    auto prog = pool[(size_t) randInt (0, (int) pool.size() - 1)];

    // Occasionally swap two bars for extra variety without losing coherence.
    if (rand01() < 0.3f)
        std::swap (prog[1], prog[3]);
    return prog;
}

GeneratedPart MidiGenerator::generateChords (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc  = rootPitchClass (p.root);
    const auto ivals  = scaleIntervals (p.scale);
    const int base    = 12 * (p.octave) + rootPc; // e.g. octave 4 -> ~C4 region

    const auto degrees = pickProgression();
    const bool seventh = p.chordComplexity > 0.5f || rand01() < 0.4f;
    const bool addNine = p.chordComplexity > 0.3f && rand01() < 0.35f;
    const int  inversion = randInt (0, 2);        // random voicing per generation

    // Rhythm styles drawn per generation (all house staples):
    //   0 sustained pad-chords, 1 offbeat stabs ("and" of every beat),
    //   2 classic 2-and-4-and push, 3 syncopated 16th stab pattern,
    //   4 half-bar movement (2 hits per bar).
    const int style = randInt (0, 4);

    // Per-style hit grid: (startBeat, length) within a bar.
    std::vector<std::pair<double,double>> hits;
    switch (style)
    {
        case 0: hits = { { 0.0, 3.9 } };                                      break;
        case 1: hits = { { 0.5, 0.4 }, { 1.5, 0.4 }, { 2.5, 0.4 }, { 3.5, 0.4 } }; break;
        case 2: hits = { { 1.5, 0.45 }, { 3.5, 0.45 } };                      break;
        case 3: hits = { { 0.0, 0.3 }, { 0.75, 0.3 }, { 1.5, 0.3 }, { 2.5, 0.3 }, { 3.25, 0.3 } }; break;
        default: hits = { { 0.0, 1.9 }, { 2.0, 1.9 } };                       break;
    }

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg = degrees[(size_t) (bar % (int) degrees.size())];
        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, seventh);

        // Apply inversion: move lowest notes up an octave.
        for (int i = 0; i < inversion && i < (int) chord.size(); ++i)
            chord[(size_t) i] += 12;
        if (addNine)
            chord.push_back (base + ivals[(size_t) ((deg + 1) % (int) ivals.size())] + 12);

        for (auto& [offset, len] : hits)
        {
            if (len < 1.0 && rand01() < 0.12f) continue; // drop a stab now and then
            const double start = bar * 4.0 + offset;
            const float  vel   = 0.65f + rand01() * 0.2f;
            for (int n : chord)
                part.notes.push_back ({ start, len, n, vel });
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generateBass (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int base   = 12 * (p.octave - 2) + rootPc; // low register

    const auto degrees = pickProgression();

    // Bass styles: 0 classic offbeat 8ths, 1 rolling octave 16ths,
    // 2 syncopated one-note groove, 3 root-fifth pump, 4 acid-ish walk.
    const int style = randInt (0, 4);

    // A random 16-step gate mask for style 2 (regenerated per generation).
    std::array<bool, 16> mask {};
    for (int s = 0; s < 16; ++s)
        mask[(size_t) s] = (s % 4 != 0) ? (rand01() < 0.45f + p.noteDensity * 0.3f)
                                        : (rand01() < 0.2f); // rarely fight the kick

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int deg  = degrees[(size_t) (bar % (int) degrees.size())];
        const int root = base + ivals[(size_t) (deg % (int) ivals.size())];
        const double b0 = bar * 4.0;

        switch (style)
        {
            case 0: // classic offbeat 8ths with gaps + occasional octave pop
                for (int e = 1; e < 8; e += 2)
                {
                    if (rand01() > 0.88f) continue;
                    const bool up = rand01() < 0.15f;
                    part.notes.push_back ({ b0 + e * 0.5, 0.4 + rand01() * 0.1,
                                            root + (up ? 12 : 0),
                                            0.8f + rand01() * 0.15f });
                }
                break;

            case 1: // rolling 16ths alternating root/octave
                for (int s = 0; s < 16; ++s)
                {
                    if (s % 4 == 0 && rand01() < 0.5f) continue; // duck the kick
                    const int oct = (s % 2 == 1) ? 12 : 0;
                    part.notes.push_back ({ b0 + s * 0.25, 0.2, root + oct,
                                            0.7f + rand01() * 0.2f });
                }
                break;

            case 2: // syncopated groove from the random mask
                for (int s = 0; s < 16; ++s)
                    if (mask[(size_t) s])
                        part.notes.push_back ({ b0 + s * 0.25, 0.22, root,
                                                0.75f + rand01() * 0.2f });
                break;

            case 3: // root-fifth pump on offbeats
                for (int e = 1; e < 8; e += 2)
                {
                    const bool fifth = (e == 3 || e == 7) && rand01() < 0.6f;
                    part.notes.push_back ({ b0 + e * 0.5, 0.42,
                                            root + (fifth ? 7 : 0),
                                            0.82f + rand01() * 0.12f });
                }
                break;

            default: // acid-ish walk around the root (stays in scale via validator)
                for (int s = 0; s < 16; ++s)
                {
                    if (rand01() > 0.35f + p.noteDensity * 0.4f) continue;
                    const int offsets[] = { 0, 0, 0, 12, 7, -5, 3 };
                    const int off = offsets[(size_t) randInt (0, 6)];
                    part.notes.push_back ({ b0 + s * 0.25, 0.2, root + off,
                                            0.65f + rand01() * 0.3f });
                }
                break;
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

    const auto degrees = pickProgression();
    const double stepBeats = (p.rhythmComplexity > 0.6f || rand01() < 0.35f) ? 0.25 : 0.5;
    const int stepsPerBar  = (int) std::round (4.0 / stepBeats);

    // Build a one-bar rhythmic motif, then repeat it with variation —
    // this is what makes it sound like a hook instead of dice rolls.
    std::vector<bool> motif ((size_t) stepsPerBar, false);
    const float density = 0.25f + p.noteDensity * 0.5f;
    for (int s = 0; s < stepsPerBar; ++s)
        motif[(size_t) s] = rand01() < density;
    motif[0] = motif[0] || rand01() < 0.6f; // usually anchor the downbeat

    int lastDeg = randInt (0, (int) ivals.size() - 1);

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int chordDeg = degrees[(size_t) (bar % (int) degrees.size())];
        for (int s = 0; s < stepsPerBar; ++s)
        {
            bool play = motif[(size_t) s];
            if (rand01() < 0.15f) play = ! play; // per-bar variation
            if (! play) continue;

            if (s == 0 && rand01() < 0.7f)
                lastDeg = chordDeg; // resolve to the chord tone at bar starts
            else
            {
                const int move = (int) std::round ((rand01() - 0.5f) * 4.0f * (0.5f + p.complexity));
                lastDeg = std::clamp (lastDeg + move, 0, (int) ivals.size() * 2);
            }

            const int oct  = lastDeg / (int) ivals.size();
            const int note = base + oct * 12 + ivals[(size_t) (lastDeg % (int) ivals.size())];
            const double gate = 0.6 + rand01() * 0.35;
            part.notes.push_back ({ bar * 4.0 + s * stepBeats, stepBeats * gate,
                                    note, 0.6f + rand01() * 0.3f });
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generateArp (const MusicParams& p)
{
    GeneratedPart part;
    const int rootPc = rootPitchClass (p.root);
    const auto ivals = scaleIntervals (p.scale);
    const int base   = 12 * (p.octave + 1) + rootPc;

    const auto degrees = pickProgression();

    // Note pool per chord: triad, triad+7, or add9 — chosen per generation.
    const int poolChoice = randInt (0, 2);
    // Direction: 0 up, 1 down, 2 up-down, 3 random order.
    const int direction  = randInt (0, 3);
    const double step    = (p.rhythmComplexity < 0.3f && rand01() < 0.4f) ? 0.5 : 0.25;
    const int octSpan    = randInt (1, 2);
    const double gate    = 0.5 + rand01() * 0.45;

    const int stepsPerBar = (int) std::round (4.0 / step);

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const int chordDeg = degrees[(size_t) (bar % (int) degrees.size())];
        std::vector<int> tones = { 0, 2, 4 };
        if (poolChoice >= 1) tones.push_back (6);
        if (poolChoice == 2) tones.push_back (8); // 9th (validator keeps it in scale)

        // Expand across octaves.
        std::vector<int> seq;
        for (int o = 0; o < octSpan; ++o)
            for (int t : tones)
            {
                const int degIdx = chordDeg + t;
                seq.push_back (base + o * 12 + (degIdx / (int) ivals.size()) * 12
                               + ivals[(size_t) (degIdx % (int) ivals.size())]);
            }

        for (int s = 0; s < stepsPerBar; ++s)
        {
            int idx;
            const int n = (int) seq.size();
            switch (direction)
            {
                case 0:  idx = s % n;                          break;
                case 1:  idx = (n - 1) - (s % n);              break;
                case 2:  { const int cyc = 2 * n - 2;
                           const int m = s % cyc;
                           idx = m < n ? m : cyc - m; }        break;
                default: idx = randInt (0, n - 1);             break;
            }
            if (rand01() < 0.06f) continue; // tiny gaps breathe
            part.notes.push_back ({ bar * 4.0 + s * step, step * gate,
                                    seq[(size_t) idx],
                                    0.6f + rand01() * 0.25f });
        }
    }
    return part;
}

GeneratedPart MidiGenerator::generatePad (const MusicParams& p)
{
    // Sustained chords following a (freshly drawn) progression, with
    // random open/close voicing — independent of the stab-chord part.
    GeneratedPart part;
    const int rootPc  = rootPitchClass (p.root);
    const auto ivals  = scaleIntervals (p.scale);
    const int base    = 12 * (p.octave) + rootPc;

    const auto degrees = pickProgression();
    const bool seventh = rand01() < 0.6f;
    const bool spread  = rand01() < 0.5f; // open voicing: drop middle note an octave
    const int  holdBars = rand01() < 0.3f ? 2 : 1; // sometimes 2-bar washes

    for (int bar = 0; bar < p.bars; bar += holdBars)
    {
        const int deg = degrees[(size_t) (bar % (int) degrees.size())];
        auto chord = theory::diatonicChord (base, rootPc, ivals, deg, seventh);
        if (spread && chord.size() > 2)
            chord[1] -= 12;

        for (int n : chord)
            part.notes.push_back ({ bar * 4.0, 4.0 * holdBars - 0.1, n,
                                    0.5f + rand01() * 0.1f });
    }
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

    // Per-generation stylistic choices (constant across bars = groove identity).
    const bool clapOn2and4   = rand01() < 0.7f;  // else clap on 2, snare on 4
    const bool extraKick     = rand01() < 0.3f;  // ghost kick on the "and of 4"
    const int  ohatStyle     = randInt (0, 2);   // 0 every offbeat, 1 alternate, 2 sparse
    const int  chatStyle     = randInt (0, 2);   // 0 straight 16ths, 1 offbeat 8ths, 2 broken
    const bool doubleClap    = rand01() < 0.25f; // flammed second clap
    const bool fillLastBar   = p.bars >= 4 && rand01() < 0.5f;

    // A fixed random accent mask for broken hats — same every bar, so it grooves.
    std::array<bool, 16> chatMask {};
    for (int s = 0; s < 16; ++s)
        chatMask[(size_t) s] = rand01() < 0.35f + p.energy * 0.35f;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const double b0 = bar * 4.0;
        const bool isFillBar = fillLastBar && (bar % 4 == 3);

        // Four-on-the-floor kick — the non-negotiable house heartbeat.
        for (int beat = 0; beat < 4; ++beat)
            kickPart.notes.push_back ({ b0 + beat, 0.25, kick,
                                        0.92f + rand01() * 0.06f });
        if (extraKick && rand01() < 0.6f)
            kickPart.notes.push_back ({ b0 + 3.5, 0.2, kick, 0.6f });

        // Backbeat: clap/snare arrangement per style.
        if (clapOn2and4)
        {
            clapPart.notes.push_back ({ b0 + 1.0, 0.25, clap, 0.85f });
            clapPart.notes.push_back ({ b0 + 3.0, 0.25, clap, 0.85f });
        }
        else
        {
            clapPart.notes.push_back  ({ b0 + 1.0, 0.25, clap,  0.85f });
            snarePart.notes.push_back ({ b0 + 3.0, 0.25, snare, 0.8f });
        }
        if (doubleClap)
            clapPart.notes.push_back ({ b0 + 1.0 + 0.08, 0.15, clap, 0.5f });

        // Open hat — the offbeat "tss", with pattern personality.
        for (int beat = 0; beat < 4; ++beat)
        {
            const bool play = ohatStyle == 0 ? true
                            : ohatStyle == 1 ? (beat % 2 == 0)
                                             : (rand01() < 0.5f);
            if (play)
                ohatPart.notes.push_back ({ b0 + beat + 0.5, 0.2, openHat,
                                            0.55f + rand01() * 0.15f });
        }

        // Closed hats per style.
        for (int s = 0; s < 16; ++s)
        {
            bool play = false;
            float vel = 0.4f + rand01() * 0.25f;
            switch (chatStyle)
            {
                case 0: play = rand01() < 0.55f + p.energy * 0.4f;
                        if (s % 4 == 2) vel += 0.15f;            break; // accent "e"
                case 1: play = (s % 2 == 1);                     break; // offbeat 8ths
                default: play = chatMask[(size_t) s];            break; // broken groove
            }
            if (play)
                chatPart.notes.push_back ({ b0 + s * 0.25, 0.1, closedHat, vel });
        }

        // Simple fill: 1/16 snare roll into the next 4-bar phrase.
        if (isFillBar)
            for (int s = 12; s < 16; ++s)
                snarePart.notes.push_back ({ b0 + s * 0.25, 0.12, snare,
                                             0.4f + (s - 12) * 0.15f });
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
