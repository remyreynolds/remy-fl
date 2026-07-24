#include "../src/ai/BrainCorpus.h"
#include "../src/ai/BrainValidator.h"
#include "../src/ai/MidiPattern.h"
#include <iostream>
#include <cstdlib>

using namespace aimidi;

static int fails = 0;

static void expect (bool cond, const char* msg)
{
    if (! cond)
    {
        std::cerr << "FAIL: " << msg << "\n";
        ++fails;
    }
    else
    {
        std::cout << "ok  — " << msg << "\n";
    }
}

int main()
{
    BrainCorpus corpus;
    juce::File corpusFile;
    juce::Array<juce::File> candidates;
    candidates.add (juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("brain/1-knowledge/house_brain_corpus.json"));
    candidates.add (juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("../brain/1-knowledge/house_brain_corpus.json"));
    auto walk = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        candidates.add (walk.getChildFile ("brain/1-knowledge/house_brain_corpus.json"));
        walk = walk.getParentDirectory();
    }
    for (auto& c : candidates)
        if (c.existsAsFile()) { corpusFile = c; break; }

    expect (corpusFile.existsAsFile(), "corpus JSON exists in repo");
    expect (corpus.loadFromFile (corpusFile), "corpus loads");
    expect (corpus.size() > 50, "corpus has many heading chunks");
    expect (corpus.hardRules().promptOverride.containsIgnoreCase ("parameter cards"),
            "prompt override present");
    expect (corpus.hardRules().bassMonophonyHard, "bass monophony hard rule");
    expect (corpus.hardRules().optionalDrumHitsPerBar == 7, "optional drum budget = 7");

    expect (corpus.selectArchetype ("minimal groove rolling bass") == "minimal_deep_tech_pocket",
            "Appendix C: minimal groove → Stussy");
    expect (corpus.selectArchetype ("big festival melody") == "melody_first_progressive_house",
            "Appendix C: big festival → Avicii");
    expect (corpus.selectArchetype ("make a kettama warehouse loop") == "high_impact_loop_pressure",
            "Appendix C: kettama → KETTAMA");

    auto route = corpus.retrieveForGeneration ("minimal groove bassline in A minor", 12000);
    expect (route.primaryArchetype == "minimal_deep_tech_pocket", "route primary archetype");
    expect (route.chunksUsed > 0, "retrieved chunks");
    expect (route.context.containsIgnoreCase ("overrides"), "context has override text");
    expect (route.context.containsIgnoreCase ("HOUSE BRAIN")
                || route.context.containsIgnoreCase ("Primary archetype"),
            "context has primary archetype header");

    // Must not dump all five Part II profiles for a single-archetype request
    int otherArchHits = 0;
    for (auto& title : route.matchedTitles)
    {
        auto t = title.toLowerCase();
        if (t.contains ("avicii") || t.contains ("guetta") || t.contains ("prospa")
            || t.contains ("kettama"))
            ++otherArchHits;
    }
    expect (otherArchHits == 0, "does not pull other archetype profile titles");

    auto hybrid = corpus.retrieveForGeneration (
        "avicii progressive hybrid with stussy rolling bass", 8000);
    expect (hybrid.primaryArchetype == "melody_first_progressive_house"
                || hybrid.primaryArchetype == "minimal_deep_tech_pocket",
            "hybrid picks a primary");
    expect ((int) hybrid.borrows.size() <= 2, "hybrid max two borrows");

    // Validator: bass polyphony reject
    MidiPattern bassPoly;
    bassPoly.instrument = "bass";
    bassPoly.bars = 4;
    bassPoly.notes.push_back ({ "A1", 33, 0.0, 1.0, 100 });
    bassPoly.notes.push_back ({ "E2", 40, 0.0, 1.0, 100 }); // overlapping
    BrainValidator v (corpus.hardRules());
    auto bad = v.validatePattern (bassPoly, "bass");
    expect (! bad.ok && bad.failures.contains ("bass_polyphony"), "rejects bass polyphony");

    MidiPattern bassMono;
    bassMono.instrument = "bass";
    bassMono.bars = 4;
    bassMono.notes.push_back ({ "A1", 33, 0.0, 0.5, 100 });
    bassMono.notes.push_back ({ "E2", 40, 1.0, 0.5, 100 });
    auto good = v.validatePattern (bassMono, "bass");
    expect (good.ok, "accepts monophonic bass");

    if (fails > 0)
    {
        std::cerr << fails << " failure(s)\n";
        return 1;
    }
    std::cout << "all BrainCorpusTests passed\n";
    return 0;
}
