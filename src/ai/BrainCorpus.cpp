#include "BrainCorpus.h"

#include <algorithm>

namespace aimidi
{
namespace
{
juce::String strField (const juce::var& o, const char* key)
{
    if (auto* obj = o.getDynamicObject())
        return obj->getProperty (key).toString();
    return {};
}

int intField (const juce::var& o, const char* key, int fallback = 0)
{
    if (auto* obj = o.getDynamicObject())
    {
        auto v = obj->getProperty (key);
        if (v.isInt() || v.isInt64() || v.isDouble())
            return (int) v;
    }
    return fallback;
}

bool boolField (const juce::var& o, const char* key)
{
    if (auto* obj = o.getDynamicObject())
        return (bool) obj->getProperty (key);
    return false;
}

double doubleField (const juce::var& o, const char* key, double fallback)
{
    if (auto* obj = o.getDynamicObject())
    {
        auto v = obj->getProperty (key);
        if (v.isDouble() || v.isInt() || v.isInt64())
            return (double) v;
    }
    return fallback;
}

double doubleProp (const juce::DynamicObject& obj, const char* key, double fallback)
{
    auto v = obj.getProperty (key);
    if (v.isDouble() || v.isInt() || v.isInt64())
        return (double) v;
    return fallback;
}

juce::String layerFromInstrument (const juce::String& instrument)
{
    auto i = instrument.toLowerCase();
    if (i.contains ("drum") || i.contains ("kick") || i.contains ("hat") || i.contains ("perc"))
        return "drums";
    if (i.contains ("bass"))
        return "bass";
    if (i.contains ("melody") || i.contains ("lead") || i.contains ("top"))
        return "melody";
    if (i.contains ("chord") || i.contains ("pad") || i.contains ("harmony"))
        return "harmony";
    return "arrangement";
}

bool isSharedLayer (const juce::String& chunkLayer)
{
    return chunkLayer.isEmpty()
        || chunkLayer == "shared"
        || chunkLayer == "pipeline"
        || chunkLayer == "scoring"
        || chunkLayer == "validation"
        || chunkLayer == "rejection"
        || chunkLayer == "hybrid"
        || chunkLayer == "density"
        || chunkLayer == "identity"
        || chunkLayer == "archetype"
        || chunkLayer == "analysis"
        || chunkLayer == "form"
        || chunkLayer == "humanization"
        || chunkLayer == "export";
}

bool layerMatches (const juce::String& chunkLayer, const juce::StringArray& wanted)
{
    if (wanted.isEmpty() || isSharedLayer (chunkLayer))
        return true;

    for (auto& w : wanted)
        if (chunkLayer.containsIgnoreCase (w) || w.containsIgnoreCase (chunkLayer))
            return true;
    return false;
}

/** Appendix D — scaffold the user request for the model. */
juce::String appendixDScaffold (const juce::String& userPrompt,
                                const juce::String& archetype,
                                const juce::String& label,
                                const juce::StringArray& layers)
{
    juce::String s;
    s << "=== APPENDIX D REQUEST SCAFFOLD ===\n";
    s << "Primary archetype: " << label << " (" << archetype << ")\n";
    s << "Target layers: " << layers.joinIntoString (", ") << "\n";
    s << "User brief: " << userPrompt.trim() << "\n";
    s << "Compose MIDI that follows the parameter cards and recipes for this archetype.\n";
    s << "Respect generation order: harmony/chords before bass before drums before melody "
         "before arrangement mutations.\n";
    return s;
}
} // namespace

BrainCorpus::BrainCorpus() = default;

bool BrainCorpus::loadFromDefaultLocations()
{
    juce::Array<juce::File> candidates;

    auto app = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AIMidiGen")
                   .getChildFile ("knowledge")
                   .getChildFile ("house_brain_corpus.json");
    candidates.add (app);

    candidates.add (juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("brain/1-knowledge/house_brain_corpus.json"));
    candidates.add (juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("../brain/1-knowledge/house_brain_corpus.json"));

#if JUCE_MAC
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    candidates.add (exe.getParentDirectory().getChildFile ("house_brain_corpus.json"));
    candidates.add (exe.getParentDirectory()
                        .getSiblingFile ("Resources")
                        .getChildFile ("house_brain_corpus.json"));
    // Walk up from .app bundle toward repo root during development
    auto walk = exe.getParentDirectory();
    for (int i = 0; i < 8; ++i)
    {
        candidates.add (walk.getChildFile ("brain/1-knowledge/house_brain_corpus.json"));
        walk = walk.getParentDirectory();
    }
#endif

    for (auto& f : candidates)
        if (f.existsAsFile() && loadFromFile (f))
            return true;
    return false;
}

bool BrainCorpus::loadFromFile (const juce::File& jsonFile)
{
    if (! jsonFile.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse (jsonFile);
    if (parsed.isVoid())
        return false;

    chunks.clear();
    presetRules.clear();
    archetypeLabels.clear();

    if (auto* root = parsed.getDynamicObject())
    {
        if (auto* arch = root->getProperty ("archetypes").getDynamicObject())
        {
            for (auto& name : arch->getProperties())
                archetypeLabels.set (name.value.toString(), name.name.toString());
        }

        if (auto* arr = root->getProperty ("chunks").getArray())
        {
            for (auto& item : *arr)
            {
                Chunk c;
                c.id = strField (item, "id");
                c.title = strField (item, "title");
                c.text = strField (item, "text");
                c.part = strField (item, "part");
                c.archetype = strField (item, "archetype");
                c.layer = strField (item, "layer");
                c.musicalTopic = strField (item, "musical_topic");
                c.evidenceLabel = strField (item, "evidence_label");
                c.pageStart = intField (item, "page_start");
                c.pageEnd = intField (item, "page_end");
                c.hasTable = boolField (item, "has_table");
                if (c.text.isNotEmpty())
                    chunks.push_back (std::move (c));
            }
        }
    }

    parseHardRules (parsed);
    parsePresetRules (parsed);
    return ! chunks.empty();
}

void BrainCorpus::parseHardRules (const juce::var& root)
{
    rules = HardRules{};
    if (auto* obj = root.getDynamicObject())
    {
        // Root-level prompt_override (corpus schema)
        rules.promptOverride = obj->getProperty ("prompt_override").toString();

        auto hr = obj->getProperty ("hard_rules");
        if (auto* h = hr.getDynamicObject())
        {
            if (rules.promptOverride.isEmpty())
                rules.promptOverride = h->getProperty ("prompt_override").toString();

            if (auto* order = h->getProperty ("pipeline_order").getArray())
                for (auto& s : *order)
                    rules.pipelineOrder.add (s.toString());

            if (auto* rej = h->getProperty ("rejection").getDynamicObject())
            {
                rules.melodyIntervalSimMax =
                    doubleProp (*rej, "melody_interval_similarity_max",
                                doubleProp (*rej, "melody_interval_sim_max", 0.86));
                rules.melodyRhythmSimMax =
                    doubleProp (*rej, "melody_rhythm_similarity_max",
                                doubleProp (*rej, "melody_rhythm_sim_max", 0.82));
                rules.drumMaskSimMax =
                    doubleProp (*rej, "drum_mask_similarity_max",
                                doubleProp (*rej, "drum_mask_sim_max", 0.90));
                rules.bassRhythmSimMax =
                    doubleProp (*rej, "bass_rhythm_similarity_max",
                                doubleProp (*rej, "bass_rhythm_sim_max", 0.84));
            }

            rules.optionalDrumHitsPerBar =
                (int) doubleProp (*h, "default_optional_drum_hits_per_bar", 7.0);
            if (auto* dens = h->getProperty ("density").getDynamicObject())
                rules.optionalDrumHitsPerBar =
                    (int) doubleProp (*dens, "optional_drum_hits_per_bar",
                                      (double) rules.optionalDrumHitsPerBar);

            if (h->hasProperty ("bass_monophony_hard"))
                rules.bassMonophonyHard = (bool) h->getProperty ("bass_monophony_hard");
            else if (auto* val = h->getProperty ("validation").getDynamicObject())
                rules.bassMonophonyHard = (bool) val->getProperty ("bass_monophony_hard");
        }
    }

    if (rules.promptOverride.isEmpty())
    {
        rules.promptOverride =
            "Generate using the parameter cards, progression libraries (Appendix A), "
            "drum pattern libraries (Appendix B), velocity bands, swing ranges, and "
            "generation recipes from the provided document. Where the document specifies "
            "a value or range, it overrides your general knowledge.";
    }
    if (rules.pipelineOrder.isEmpty())
        rules.pipelineOrder.addArray ({ "harmony", "bass", "drums", "melody", "arrangement" });
}

void BrainCorpus::parsePresetRules (const juce::var& root)
{
    if (auto* obj = root.getDynamicObject())
    {
        // Corpus uses fast_preset_selection; accept fast_preset_rules as alias
        juce::var arrVar = obj->getProperty ("fast_preset_selection");
        if (arrVar.isVoid())
            arrVar = obj->getProperty ("fast_preset_rules");

        if (auto* arr = arrVar.getArray())
        {
            for (auto& item : *arr)
            {
                PresetRule r;
                r.archetype = strField (item, "archetype");
                r.label = strField (item, "label");
                if (auto* m = item.getDynamicObject())
                    if (auto* matches = m->getProperty ("match").getArray())
                        for (auto& s : *matches)
                            r.match.add (s.toString().toLowerCase());
                if (r.archetype.isNotEmpty())
                    presetRules.push_back (std::move (r));
            }
        }
    }

    if (presetRules.empty())
    {
        presetRules.push_back ({ { "minimal groove", "deep tech", "rolling bass", "restrained", "minimal" },
                                 "minimal_deep_tech_pocket", "Chris Stussy" });
        presetRules.push_back ({ { "big festival", "festival melody", "uplifting", "avicii" },
                                 "melody_first_progressive_house", "Avicii" });
        presetRules.push_back ({ { "warehouse", "loop pressure", "kettama", "hypnotic loop" },
                                 "high_impact_loop_pressure", "KETTAMA" });
        presetRules.push_back ({ { "rave", "uk house", "prospa", "filter house" },
                                 "rave_house_lift", "Prospa" });
        presetRules.push_back ({ { "pop edm", "radio", "guetta", "festival drop" },
                                 "pop_edm_clarity", "David Guetta" });
    }
}

bool BrainCorpus::textMentionsAllArchetypes (const juce::String& prompt)
{
    auto p = prompt.toLowerCase();
    return (p.contains ("all archetypes") || p.contains ("all five")
            || p.contains ("compare archetypes") || p.contains ("every archetype"));
}

juce::String BrainCorpus::selectArchetype (const juce::String& userPrompt) const
{
    auto p = userPrompt.toLowerCase();

    if (p.contains ("stussy") || p.contains ("chris stussy"))
        return "minimal_deep_tech_pocket";
    if (p.contains ("avicii"))
        return "melody_first_progressive_house";
    if (p.contains ("kettama") || p.contains ("ketama"))
        return "high_impact_loop_pressure";
    if (p.contains ("prospa"))
        return "rave_house_lift";
    if (p.contains ("guetta") || p.contains ("david guetta"))
        return "pop_edm_clarity";

    // Longer phrase matches first (sort by match length desc)
    struct Hit { int len; juce::String arch; };
    Hit best { -1, {} };
    for (auto& rule : presetRules)
        for (auto& m : rule.match)
            if (m.isNotEmpty() && p.contains (m) && m.length() > best.len)
                best = { m.length(), rule.archetype };
    if (best.len >= 0)
        return best.arch;

    if (p.contains ("tech house") || p.contains ("minimal"))
        return "minimal_deep_tech_pocket";
    if (p.contains ("progressive") || p.contains ("festival") || p.contains ("big room"))
        return "melody_first_progressive_house";
    if (p.contains ("afro") || p.contains ("tribal"))
        return "high_impact_loop_pressure";
    if (p.contains ("french") || p.contains ("filter") || p.contains ("chicago"))
        return "rave_house_lift";
    if (p.contains ("pop") || p.contains ("edm") || p.contains ("radio"))
        return "pop_edm_clarity";

    return "minimal_deep_tech_pocket";
}

juce::StringArray BrainCorpus::layersForRequest (const juce::String& userPrompt)
{
    juce::StringArray layers;
    auto p = userPrompt.toLowerCase();

    auto add = [&] (const juce::String& L) {
        if (! layers.contains (L))
            layers.add (L);
    };

    if (p.contains ("drum") || p.contains ("kick") || p.contains ("hat") || p.contains ("perc")
        || p.contains ("groove"))
        add ("drums");
    if (p.contains ("bass") || p.contains ("sub"))
        add ("bass");
    if (p.contains ("melody") || p.contains ("lead") || p.contains ("topline") || p.contains ("hook"))
        add ("melody");
    if (p.contains ("chord") || p.contains ("pad") || p.contains ("harmony") || p.contains ("progression"))
        add ("harmony");
    if (p.contains ("arrangement") || p.contains ("drop") || p.contains ("build") || p.contains ("breakdown"))
        add ("arrangement");

    if (p.contains ("instrument:"))
    {
        auto idx = p.indexOf ("instrument:");
        auto rest = p.substring (idx + 11).upToFirstOccurrenceOf ("\n", false, false).trim();
        add (layerFromInstrument (rest));
    }

    if (layers.isEmpty())
        layers.addArray ({ "harmony", "bass", "drums", "melody", "arrangement" });
    return layers;
}

BrainCorpus::RouteResult BrainCorpus::retrieveForGeneration (const juce::String& userPrompt,
                                                             int maxChars) const
{
    RouteResult out;
    if (chunks.empty())
        return out;

    out.primaryArchetype = selectArchetype (userPrompt);
    out.layersRequested = layersForRequest (userPrompt);

    for (int i = 0; i < archetypeLabels.size(); ++i)
        if (archetypeLabels.getAllValues()[i] == out.primaryArchetype)
        {
            out.primaryLabel = archetypeLabels.getAllKeys()[i];
            break;
        }
    if (out.primaryLabel.isEmpty())
    {
        for (auto& rule : presetRules)
            if (rule.archetype == out.primaryArchetype)
            {
                out.primaryLabel = rule.label;
                break;
            }
    }
    if (out.primaryLabel.isEmpty())
        out.primaryLabel = out.primaryArchetype;

    auto p = userPrompt.toLowerCase();
    if (p.contains ("hybrid") || p.contains (" with ") || p.contains ("borrow"))
    {
        auto maybeAddBorrow = [&] (const juce::String& arch, const juce::String& dim) {
            if (arch == out.primaryArchetype)
                return;
            if ((int) out.borrows.size() >= 2)
                return;
            for (auto& b : out.borrows)
                if (b.fromArchetype == arch && b.dimension == dim)
                    return;
            out.borrows.push_back ({ arch, dim, 0.55f });
        };

        if (p.contains ("stussy") && out.primaryArchetype != "minimal_deep_tech_pocket")
            maybeAddBorrow ("minimal_deep_tech_pocket",
                            p.contains ("bass") ? "bass" : "drums");
        if (p.contains ("avicii") && out.primaryArchetype != "melody_first_progressive_house")
            maybeAddBorrow ("melody_first_progressive_house", "melody");
        if ((p.contains ("kettama") || p.contains ("ketama"))
            && out.primaryArchetype != "high_impact_loop_pressure")
            maybeAddBorrow ("high_impact_loop_pressure", "drums");
        if (p.contains ("prospa") && out.primaryArchetype != "rave_house_lift")
            maybeAddBorrow ("rave_house_lift", "harmony");
        if (p.contains ("guetta") && out.primaryArchetype != "pop_edm_clarity")
            maybeAddBorrow ("pop_edm_clarity", "arrangement");
    }

    const bool wantAll = textMentionsAllArchetypes (userPrompt);

    auto scoreChunk = [&] (const Chunk& c) -> int {
        int score = 0;
        if (c.part == "III")
            score += 40;
        if (c.part == "I" && layerMatches (c.layer, out.layersRequested))
            score += 30;
        if (c.part == "II")
        {
            if (c.archetype == out.primaryArchetype)
                score += 50;
            else if (wantAll)
                score += 20;
            else
            {
                bool borrowed = false;
                for (auto& b : out.borrows)
                    if (b.fromArchetype == c.archetype
                        && (b.dimension.isEmpty()
                            || layerMatches (c.layer, { b.dimension })))
                    {
                        borrowed = true;
                        break;
                    }
                if (borrowed)
                    score += 25;
                else
                    return -1;
            }
        }
        if (c.part == "IV"
            && (c.title.containsIgnoreCase ("appendix a")
                || c.title.containsIgnoreCase ("appendix b")
                || c.title.containsIgnoreCase ("appendix d")
                || c.musicalTopic.containsIgnoreCase ("progression")
                || c.musicalTopic.containsIgnoreCase ("drum")))
            score += 15;

        if (layerMatches (c.layer, out.layersRequested))
            score += 10;
        if (c.hasTable)
            score += 5;

        auto words = juce::StringArray::fromTokens (p, " \n\t,.;:", "");
        for (auto& w : words)
        {
            if (w.length() < 4)
                continue;
            if (c.title.containsIgnoreCase (w) || c.text.containsIgnoreCase (w)
                || c.musicalTopic.containsIgnoreCase (w))
                score += 2;
        }
        return score;
    };

    struct Scored
    {
        int score = 0;
        const Chunk* chunk = nullptr;
    };
    std::vector<Scored> ranked;
    ranked.reserve (chunks.size());
    for (auto& c : chunks)
    {
        int s = scoreChunk (c);
        if (s > 0)
            ranked.push_back ({ s, &c });
    }
    std::sort (ranked.begin(), ranked.end(),
               [] (const Scored& a, const Scored& b) { return a.score > b.score; });

    juce::String block;
    block << "=== HOUSE BRAIN PDF (authoritative — overrides general music knowledge) ===\n";
    block << rules.promptOverride << "\n\n";
    block << "Primary archetype: " << out.primaryLabel << " (" << out.primaryArchetype << ")\n";
    block << "Layers in scope: " << out.layersRequested.joinIntoString (", ") << "\n";
    if (! out.borrows.empty())
    {
        block << "Hybrid borrows (max 2; never average identities):\n";
        for (auto& b : out.borrows)
            block << "  - from " << b.fromArchetype << " dim=" << b.dimension
                  << " amount=" << juce::String (b.amount, 2) << "\n";
    }
    block << "Generation order (hard): "
          << rules.pipelineOrder.joinIntoString (" → ") << "\n\n";

    int used = 0;
    for (auto& r : ranked)
    {
        if (r.chunk == nullptr)
            continue;
        auto& c = *r.chunk;
        juce::String header;
        header << "--- [" << c.evidenceLabel << "] p." << c.pageStart;
        if (c.pageEnd != c.pageStart)
            header << "-" << c.pageEnd;
        header << " | " << c.title << " | layer=" << c.layer
               << " | topic=" << c.musicalTopic << " ---\n";

        auto piece = header + c.text + "\n\n";
        if (used + piece.length() > maxChars && used > 2000)
            break;
        block << piece;
        used += piece.length();
        out.matchedTitles.add (c.title);
        ++out.chunksUsed;
        if (out.chunksUsed >= 28)
            break;
    }

    block << appendixDScaffold (userPrompt, out.primaryArchetype, out.primaryLabel,
                               out.layersRequested);
    block << "=== MACHINE OUTPUT CONTRACT ===\n";
    block << "Return separate MIDI tracks (chords/bass/drums/melody as applicable), "
             "JSON metadata (archetype, key, bpm, bars, swing, velocity bands, recipe id), "
             "and similarity scores vs reference fingerprints when available.\n";

    out.context = block;
    return out;
}

} // namespace aimidi
