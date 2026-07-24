#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace aimidi
{
/** Local structured store for house_music_midi_generator_brain.pdf
    (heading-chunked JSON per Appendix E). No remote backend. */
class BrainCorpus
{
public:
    struct Chunk
    {
        juce::String id;
        juce::String title;
        juce::String text;
        juce::String part;          // I / II / III / IV
        juce::String archetype;     // empty = shared
        juce::String layer;         // harmony/melody/bass/drums/arrangement/...
        juce::String musicalTopic;
        juce::String evidenceLabel;
        int pageStart = 0;
        int pageEnd = 0;
        bool hasTable = false;
    };

    struct HardRules
    {
        double melodyIntervalSimMax = 0.86;
        double melodyRhythmSimMax = 0.82;
        double drumMaskSimMax = 0.90;
        double bassRhythmSimMax = 0.84;
        int optionalDrumHitsPerBar = 7;
        bool bassMonophonyHard = true;
        juce::StringArray pipelineOrder;
        juce::String promptOverride;
    };

    struct HybridBorrow
    {
        juce::String fromArchetype;
        juce::String dimension;
        float amount = 0.0f;
    };

    struct RouteResult
    {
        juce::String primaryArchetype;
        juce::String primaryLabel;
        std::vector<HybridBorrow> borrows; // max 2
        juce::StringArray layersRequested;
        juce::String context;              // prompt injection block
        juce::StringArray matchedTitles;
        int chunksUsed = 0;
    };

    BrainCorpus();

    bool loadFromDefaultLocations();
    bool loadFromFile (const juce::File& jsonFile);
    bool isLoaded() const { return ! chunks.empty(); }
    int size() const { return (int) chunks.size(); }
    const HardRules& hardRules() const { return rules; }

    /** Appendix C fast preset selection + optional hybrid parse. */
    juce::String selectArchetype (const juce::String& userPrompt) const;

    /** Map requested instrument / chat text → layers to retrieve. */
    static juce::StringArray layersForRequest (const juce::String& userPrompt);

    /** PDF-first retrieval: Part I shared theory for layers + one primary
        archetype from Part II + Part III pipeline/constraint rules.
        Never pulls all five archetypes unless hybrid/all explicitly requested. */
    RouteResult retrieveForGeneration (const juce::String& userPrompt,
                                       int maxChars = 16000) const;

private:
    std::vector<Chunk> chunks;
    HardRules rules;
    struct PresetRule { juce::StringArray match; juce::String archetype; juce::String label; };
    std::vector<PresetRule> presetRules;
    juce::StringPairArray archetypeLabels; // slug -> display

    void parseHardRules (const juce::var& root);
    void parsePresetRules (const juce::var& root);
    static bool textMentionsAllArchetypes (const juce::String& prompt);
};

} // namespace aimidi
