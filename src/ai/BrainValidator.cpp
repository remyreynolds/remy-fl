#include "BrainValidator.h"

#include <cmath>
#include <map>

namespace aimidi
{
namespace
{
juce::String laneFingerprint (const MidiPatternPart& lane)
{
    juce::StringArray toks;
    for (auto& n : lane.notes)
    {
        const int step = (int) std::lround (n.startBeat * 4.0);
        toks.add (juce::String (step) + ":" + juce::String (n.pitchMidi % 12)
                  + ":" + juce::String ((int) std::lround (n.durationBeats * 4.0)));
    }
    return toks.joinIntoString ("|");
}

const MidiPatternPart* findLane (const std::vector<MidiPatternPart>& lanes,
                                 const juce::String& needle)
{
    for (auto& lane : lanes)
        if (lane.instrument.containsIgnoreCase (needle))
            return &lane;
    return nullptr;
}
} // namespace

int BrainValidator::maxPolyphony (const MidiPattern& pattern)
{
    struct Ev
    {
        double t;
        int delta;
    };
    std::vector<Ev> evs;
    for (auto& lane : pattern.lanes())
        for (auto& n : lane.notes)
        {
            evs.push_back ({ n.startBeat, +1 });
            evs.push_back ({ n.startBeat + juce::jmax (0.01, n.durationBeats), -1 });
        }
    std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) {
        if (a.t != b.t)
            return a.t < b.t;
        return a.delta < b.delta;
    });
    int cur = 0, mx = 0;
    for (auto& e : evs)
    {
        cur += e.delta;
        mx = juce::jmax (mx, cur);
    }
    return mx;
}

int BrainValidator::maxPolyphonyInLane (const MidiPatternPart& lane)
{
    struct Ev
    {
        double t;
        int delta;
    };
    std::vector<Ev> evs;
    for (auto& n : lane.notes)
    {
        evs.push_back ({ n.startBeat, +1 });
        evs.push_back ({ n.startBeat + juce::jmax (0.01, n.durationBeats), -1 });
    }
    std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) {
        if (a.t != b.t)
            return a.t < b.t;
        return a.delta < b.delta;
    });
    int cur = 0, mx = 0;
    for (auto& e : evs)
    {
        cur += e.delta;
        mx = juce::jmax (mx, cur);
    }
    return mx;
}

double BrainValidator::hitsPerBar (const MidiPattern& pattern)
{
    const int notes = pattern.totalNotes();
    if (notes <= 0 || pattern.bars <= 0)
        return 0.0;
    return (double) notes / (double) pattern.bars;
}

BrainValidationResult BrainValidator::validatePattern (const MidiPattern& pattern,
                                                       const juce::String& layerHint) const
{
    BrainValidationResult r;
    auto layer = layerHint.toLowerCase();
    const auto lanes = pattern.lanes();

    if (pattern.totalNotes() <= 0)
    {
        r.ok = false;
        r.failures.add ("empty_pattern");
        return r;
    }

    for (auto& lane : lanes)
        for (auto& n : lane.notes)
        {
            if (n.pitchMidi < 0 || n.pitchMidi > 127)
            {
                r.ok = false;
                r.failures.add ("pitch_out_of_range");
            }
            if (n.durationBeats <= 0.0)
            {
                r.ok = false;
                r.failures.add ("non_positive_length");
            }
            if (n.velocity < 1 || n.velocity > 127)
            {
                r.ok = false;
                r.failures.add ("velocity_out_of_range");
            }
        }

    // Ch.11 — bass monophony hard
    if (rules.bassMonophonyHard)
    {
        for (auto& lane : lanes)
        {
            if (! lane.instrument.containsIgnoreCase ("bass")
                && ! (layer.contains ("bass") && lanes.size() == 1))
                continue;
            if (maxPolyphonyInLane (lane) > 1)
            {
                r.ok = false;
                r.failures.add ("bass_polyphony");
            }
        }
    }

    // Density budget for drums
    if (auto* drums = findLane (lanes, "drum"))
    {
        if (pattern.bars > 0)
        {
            const double hpb = (double) drums->notes.size() / (double) pattern.bars;
            if (hpb > (double) rules.optionalDrumHitsPerBar * 4.0)
            {
                r.ok = false;
                r.failures.add ("drum_density_budget");
            }
            else if (hpb > (double) rules.optionalDrumHitsPerBar * 2.5)
                r.warnings.add ("drum_density_high");
        }
    }

    // Ch.6 dependency graph (full-loop): melody/bass imply harmony present when multi-part
    if (lanes.size() >= 2)
    {
        const bool hasHarmony = findLane (lanes, "chord") != nullptr
                             || findLane (lanes, "pad") != nullptr;
        const bool hasMelody = findLane (lanes, "melody") != nullptr
                            || findLane (lanes, "lead") != nullptr;
        const bool hasBass = findLane (lanes, "bass") != nullptr;
        if ((hasMelody || hasBass) && ! hasHarmony)
            r.warnings.add ("pipeline_missing_harmony_before_melody_or_bass");
    }

    const double maxBeat = (double) juce::jmax (1, pattern.bars) * 4.0 + 0.01;
    for (auto& lane : lanes)
        for (auto& n : lane.notes)
            if (n.startBeat < -0.01 || n.startBeat >= maxBeat)
            {
                r.warnings.add ("note_outside_bars");
                goto done_timing;
            }
done_timing:
    return r;
}

BrainValidationResult BrainValidator::validateOriginality (
    const MidiPattern& pattern,
    const juce::StringArray& recentFingerprints,
    const juce::String& layerHint) const
{
    BrainValidationResult r;
    if (recentFingerprints.isEmpty())
        return r;

    auto layer = layerHint.toLowerCase();
    double limit = 0.90;
    if (layer.contains ("melody"))
        limit = rules.melodyIntervalSimMax;
    else if (layer.contains ("bass"))
        limit = rules.bassRhythmSimMax;
    else if (layer.contains ("drum"))
        limit = rules.drumMaskSimMax;
    else
        limit = rules.melodyIntervalSimMax;

    juce::StringArray fps;
    auto chordFp = chordProgressionFingerprint (pattern);
    if (chordFp.isNotEmpty())
        fps.add (chordFp);
    for (auto& lane : pattern.lanes())
        fps.add (lane.instrument + "=" + laneFingerprint (lane));

    for (auto& fp : fps)
    {
        if (fp.isEmpty())
            continue;
        for (auto& other : recentFingerprints)
        {
            if (other.isEmpty() || other == fp)
                continue;
            auto a = juce::StringArray::fromTokens (fp, "|:;", "");
            auto b = juce::StringArray::fromTokens (other, "|:;", "");
            a.removeEmptyStrings();
            b.removeEmptyStrings();
            if (a.isEmpty() || b.isEmpty())
                continue;
            int inter = 0;
            for (auto& t : a)
                if (b.contains (t))
                    ++inter;
            const int uni = a.size() + b.size() - inter;
            const double sim = uni > 0 ? (double) inter / (double) uni : 0.0;
            if (sim >= limit)
            {
                r.ok = false;
                r.failures.add ("originality_reject_sim_" + juce::String (sim, 3));
                return r;
            }
        }
    }
    return r;
}

double BrainValidator::scoreCandidate (const MidiPattern& pattern,
                                       const juce::String& /*primaryArchetype*/) const
{
    // Ch.7 weighted proxy using document weight names as guidance
    double score = 0.0;
    if (pattern.totalNotes() <= 0)
        return 0.0;

    const double hpb = hitsPerBar (pattern);
    score += 1.4 * (10.0 - std::abs (hpb - 8.0)); // groove_fit-ish

    double velSum = 0.0;
    int noteCount = 0;
    int onGrid = 0;
    int lo = 127, hi = 0;
    for (auto& lane : pattern.lanes())
        for (auto& n : lane.notes)
        {
            velSum += (double) n.velocity;
            ++noteCount;
            const double step = std::round (n.startBeat * 4.0) / 4.0;
            if (std::abs (n.startBeat - step) < 0.02)
                ++onGrid;
            lo = juce::jmin (lo, n.pitchMidi);
            hi = juce::jmax (hi, n.pitchMidi);
        }

    if (noteCount > 0)
    {
        const double velAvg = velSum / (double) noteCount;
        if (velAvg >= 70.0 && velAvg <= 110.0)
            score += 0.9 * 8.0; // phrase_shape proxy
        score += 1.2 * 15.0 * ((double) onGrid / (double) noteCount); // motif_coherence grid
    }

    const int span = hi - lo;
    if (span >= 5 && span <= 24)
        score += 0.6 * 10.0; // playable_range

    if (pattern.progression.isNotEmpty() || ! pattern.chordSymbols.isEmpty())
        score += 1.6 * 5.0; // harmony_fit metadata present

    // Polyphony collisions on bass
    for (auto& lane : pattern.lanes())
        if (lane.instrument.containsIgnoreCase ("bass") && maxPolyphonyInLane (lane) > 1)
            score += -1.8 * 10.0; // collision_penalty

    return score;
}

BrainValidationResult BrainValidator::validateFullSuite (const MidiPattern& pattern,
                                                         const juce::StringArray& recentFingerprints,
                                                         const juce::String& layerHint) const
{
    auto v = validatePattern (pattern, layerHint);
    if (! v.ok)
        return v;
    auto o = validateOriginality (pattern, recentFingerprints, layerHint);
    if (! o.ok)
    {
        v.ok = false;
        v.failures.addArray (o.failures);
    }
    v.warnings.addArray (o.warnings);
    v.score = scoreCandidate (pattern, {});
    return v;
}

} // namespace aimidi
