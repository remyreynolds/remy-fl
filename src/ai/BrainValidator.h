#pragma once

#include "BrainCorpus.h"
#include "MidiPattern.h"

#include <optional>

namespace aimidi
{
/** Post-generation enforcement of PDF hard rules (chs. 6–7, 9, 11).
    Prompt alone is insufficient — reject and regenerate on failure. */
struct BrainValidationResult
{
    bool ok = true;
    double score = 0.0;
    juce::StringArray failures;
    juce::StringArray warnings;
};

class BrainValidator
{
public:
    explicit BrainValidator (const BrainCorpus::HardRules& rulesIn) : rules (rulesIn) {}

    /** Chapter 11-style suite + density + monophony + ch.6 dependency warnings. */
    BrainValidationResult validatePattern (const MidiPattern& pattern,
                                           const juce::String& layerHint = {}) const;

    /** Chapter 9 originality vs recent fingerprints (0..1 similarity). */
    BrainValidationResult validateOriginality (const MidiPattern& pattern,
                                               const juce::StringArray& recentFingerprints,
                                               const juce::String& layerHint = {}) const;

    /** Chapter 7 weighted candidate score (higher is better). */
    double scoreCandidate (const MidiPattern& pattern,
                           const juce::String& primaryArchetype) const;

    /** Run validate + originality + score in one pass. */
    BrainValidationResult validateFullSuite (const MidiPattern& pattern,
                                             const juce::StringArray& recentFingerprints,
                                             const juce::String& layerHint = {}) const;

    static int maxPolyphony (const MidiPattern& pattern);
    static int maxPolyphonyInLane (const MidiPatternPart& lane);
    static double hitsPerBar (const MidiPattern& pattern);

private:
    BrainCorpus::HardRules rules;
};

} // namespace aimidi
