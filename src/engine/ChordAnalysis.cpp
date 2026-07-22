#include "ChordAnalysis.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace aimidi
{

juce::String pitchClassName (int pc, const std::string& preferRoot)
{
    pc = ((pc % 12) + 12) % 12;
    const bool preferFlats = preferRoot.find ('b') != std::string::npos
                          || preferRoot == "F" || preferRoot == "Bb"
                          || preferRoot == "Eb" || preferRoot == "Ab"
                          || preferRoot == "Db" || preferRoot == "Gb";
    static const char* sharps[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    static const char* flats[]  = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };
    return preferFlats ? flats[pc] : sharps[pc];
}

namespace
{
bool hasInterval (const std::set<int>& pcs, int from, int semitones)
{
    return pcs.count ((from + semitones) % 12) > 0;
}

juce::String qualityFromIntervals (bool minor3, bool major3, bool perfect5,
                                   bool dim5, bool aug5, bool min7, bool maj7, bool dom7)
{
    if (minor3 && dim5 && min7) return "m7b5";
    if (minor3 && dim5)         return "dim";
    if (major3 && aug5)         return "aug";
    if (minor3 && perfect5 && min7) return "m7";
    if (major3 && perfect5 && maj7) return "maj7";
    if (major3 && perfect5 && dom7) return "7";
    if (minor3 && perfect5)     return "m";
    if (major3 && perfect5)     return "";
    if (minor3)                 return "m";
    if (major3)                 return "";
    return "?";
}
} // namespace

juce::String nameChordFromPitches (const std::vector<int>& midiPitches,
                                   const MusicParams& params)
{
    if (midiPitches.empty())
        return "—";

    std::set<int> pcs;
    for (int p : midiPitches)
        pcs.insert (((p % 12) + 12) % 12);

    if (pcs.size() == 1)
        return pitchClassName (*pcs.begin(), params.root);

    if (pcs.size() == 2)
    {
        auto it = pcs.begin();
        const int a = *it++;
        const int b = *it;
        return pitchClassName (a, params.root) + "/" + pitchClassName (b, params.root);
    }

    juce::String best;
    int bestScore = -1;

    for (int root : pcs)
    {
        const bool minor3   = hasInterval (pcs, root, 3);
        const bool major3   = hasInterval (pcs, root, 4);
        const bool perfect5 = hasInterval (pcs, root, 7);
        const bool dim5     = hasInterval (pcs, root, 6);
        const bool aug5     = hasInterval (pcs, root, 8);
        const bool min7     = hasInterval (pcs, root, 10);
        const bool maj7     = hasInterval (pcs, root, 11);
        const bool dom7     = min7 && major3 && perfect5;

        const auto q = qualityFromIntervals (minor3, major3, perfect5, dim5, aug5,
                                             min7, maj7, dom7);
        if (q == "?") continue;

        int score = 0;
        if (minor3 || major3) score += 3;
        if (perfect5 || dim5 || aug5) score += 2;
        if (min7 || maj7) score += 1;

        // Prefer roots that are the project key root when tied
        if (root == theory::rootPitchClass (params.root))
            score += 2;

        if (score > bestScore)
        {
            bestScore = score;
            best = pitchClassName (root, params.root) + q;
        }
    }

    if (best.isNotEmpty())
        return best;

    // Fallback: list pitch classes
    juce::String list;
    for (int pc : pcs)
    {
        if (list.isNotEmpty()) list << " ";
        list << pitchClassName (pc, params.root);
    }
    return list;
}

std::vector<ChordHit> analysePartChords (const GeneratedPart& part,
                                         const MusicParams& params)
{
    std::vector<ChordHit> hits;
    if (part.notes.empty() || part.type == InstrumentType::Drums)
        return hits;

    // Group by quantized start (nearest 1/4 beat) so stab/sustain clusters merge.
    std::vector<double> starts;
    for (auto& n : part.notes)
        starts.push_back (std::round (n.startBeats * 4.0) / 4.0);

    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end(),
                               [] (double a, double b) { return std::abs (a - b) < 1e-6; }),
                  starts.end());

    juce::String lastName;
    for (double t : starts)
    {
        std::vector<int> sounding;
        for (auto& n : part.notes)
        {
            const double end = n.startBeats + n.lengthBeats;
            if (n.startBeats <= t + 0.05 && t < end - 1e-6)
                sounding.push_back (n.pitch);
        }

        // Prefer notes that start at this hit (voicing change)
        std::vector<int> started;
        for (auto& n : part.notes)
            if (std::abs (n.startBeats - t) < 0.12)
                started.push_back (n.pitch);
        if (started.size() >= 2)
            sounding = started;

        auto name = nameChordFromPitches (sounding, params);
        if (name == lastName)
            continue;
        lastName = name;

        ChordHit hit;
        hit.startBeat = t;
        hit.name = name;
        hits.push_back (hit);
    }

    return hits;
}

juce::String chordAtBeat (const GeneratedPart& part,
                          const MusicParams& params,
                          double beat)
{
    if (part.notes.empty() || part.type == InstrumentType::Drums)
        return "—";

    std::vector<int> sounding;
    for (auto& n : part.notes)
    {
        const double end = n.startBeats + n.lengthBeats;
        if (n.startBeats <= beat + 1e-6 && beat < end - 1e-6)
            sounding.push_back (n.pitch);
    }
    return nameChordFromPitches (sounding, params);
}

} // namespace aimidi
