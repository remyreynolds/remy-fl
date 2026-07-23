// Deep-audit harness: exercises the full deterministic music engine across
// every style preset, several seeds, keys and scales; checks musical
// invariants that would be audible bugs if violated.
#include "engine/MidiGenerator.h"
#include "engine/MusicTheory.h"
#include "engine/StylePresets.h"
#include "engine/SongPlan.h"
#include "engine/Critic.h"
#include "engine/ChatDirector.h"
#include <iostream>
#include <set>
#include <map>
#include <cmath>

using namespace aimidi;

static int failures = 0;
static std::map<std::string, int> failCounts; // dedupe spam
#define CHECK(cond, msg) do { if(!(cond)) { ++failures; \
  if (failCounts[msg]++ < 3) std::cout << "FAIL: " << (msg) << "\n"; } } while(0)

static bool inScale (int pitch, const MusicParams& mp)
{
    const auto ivs = theory::scaleIntervals (mp.scale);
    const int pc = ((pitch % 12) - theory::rootPitchClass (mp.root) + 12) % 12;
    for (int iv : ivs) if (iv == pc) return true;
    return false;
}

static void auditPart (const GeneratedPart& p, const MusicParams& mp,
                       const std::string& tag, bool pitched)
{
    const double loopEnd = mp.bars * 4.0;
    for (const auto& n : p.notes)
    {
        CHECK(n.startBeats >= -1e-6, tag + ": note starts before beat 0");
        CHECK(n.startBeats < loopEnd + 1e-6, tag + ": note starts past loop end");
        CHECK(n.lengthBeats > 1e-4, tag + ": zero/negative note length");
        CHECK(n.pitch >= 0 && n.pitch <= 127, tag + ": pitch out of MIDI range");
        CHECK(n.velocity > 0.0f && n.velocity <= 1.0f + 1e-6f, tag + ": velocity out of (0,1]");
        if (pitched)
            CHECK(inScale (n.pitch, mp), tag + ": out-of-key pitch (key " + mp.root + " " + mp.scale + ")");
    }
    // Overlap check per pitch (same pitch overlapping = stuck-note risk)
    std::map<int, std::vector<std::pair<double,double>>> byPitch;
    for (const auto& n : p.notes)
        byPitch[n.pitch].push_back ({ n.startBeats, n.startBeats + n.lengthBeats });
    for (auto& [pitch, spans] : byPitch)
    {
        std::sort (spans.begin(), spans.end());
        for (size_t i = 1; i < spans.size(); ++i)
            CHECK(spans[i].first >= spans[i-1].second - 1e-3,
                  tag + ": overlapping same-pitch notes (stuck-note risk)");
    }
}

int main()
{
    MidiGenerator gen;
    int stylesTested = 0, partsGenerated = 0;

    static const char* roots[]  = { "C", "F#", "Bb" };
    static const char* scales[] = { "minor", "major", "dorian" };

    for (const auto& style : allStyles())
    {
        ++stylesTested;
        for (unsigned seed : { 1u, 42u, 999u })
        {
            MusicParams mp;
            mp.genre = style.name;
            mp.bpm   = style.bpm;
            mp.swing = style.swing;
            mp.root  = roots[seed % 3];
            mp.scale = scales[(seed / 3) % 3];
            mp.bars  = (seed == 999u) ? 8 : 4;
            mp.seed  = seed;

            const std::string base = std::string (style.name) + "/seed" + std::to_string (seed);

            // Pitched lanes
            for (auto t : { InstrumentType::Melody, InstrumentType::Chords,
                            InstrumentType::Bass, InstrumentType::CounterMelody,
                            InstrumentType::Arp, InstrumentType::Pad })
            {
                auto part = gen.generate (t, mp);
                ++partsGenerated;
                auditPart (part, mp, base + "/" + toString (t), true);
            }

            // Essential lanes must not be empty
            {
                auto bass = gen.generate (InstrumentType::Bass, mp);
                auto chords = gen.generate (InstrumentType::Chords, mp);
                auto melody = gen.generate (InstrumentType::Melody, mp);
                CHECK(! bass.notes.empty(),   base + ": bass lane came out EMPTY");
                CHECK(! chords.notes.empty(), base + ": chords lane came out EMPTY");
                CHECK(! melody.notes.empty(), base + ": melody lane came out EMPTY");

                // Register sanity: bass must actually be low, melody above bass
                double bassAvg = 0, melAvg = 0;
                for (auto& n : bass.notes)   bassAvg += n.pitch;
                for (auto& n : melody.notes) melAvg  += n.pitch;
                bassAvg /= (double) bass.notes.size();
                melAvg  /= (double) melody.notes.size();
                CHECK(bassAvg < 55.0, base + ": bass register too high (avg pitch >= 55)");
                CHECK(melAvg > bassAvg, base + ": melody sits below the bass");

                // Chords: polyphony >= 3 at some point (a real chord)
                std::map<double, int> simult;
                for (auto& n : chords.notes) simult[std::round (n.startBeats * 100) / 100]++;
                int maxPoly = 0;
                for (auto& [t2, c] : simult) maxPoly = std::max (maxPoly, c);
                CHECK(maxPoly >= 3, base + ": chords never stack 3+ notes (no real chord)");
            }

            // Drum kit
            auto kit = gen.generateDrumKit (mp);
            for (size_t i = 0; i < kit.size(); ++i)
                auditPart (kit[i], mp, base + "/drum:" + toString ((DrumPiece) i), false);
            CHECK(! kit[(size_t) DrumPiece::Kick].notes.empty(), base + ": kick EMPTY");
            CHECK(! kit[(size_t) DrumPiece::ClosedHat].notes.empty(), base + ": closed hat EMPTY");

            // Determinism: same seed -> identical output
            MusicParams mp2 = mp;
            auto a = gen.generate (InstrumentType::Bass, mp);
            auto b = gen.generate (InstrumentType::Bass, mp2);
            bool same = a.notes.size() == b.notes.size();
            if (same)
                for (size_t i = 0; i < a.notes.size(); ++i)
                    same = same && a.notes[i].pitch == b.notes[i].pitch
                                && std::abs (a.notes[i].startBeats - b.notes[i].startBeats) < 1e-9;
            CHECK(same, base + ": same seed produced different bass (non-deterministic)");

            // Validator: feed garbage, must come back clean
            GeneratedPart junk;
            junk.type = InstrumentType::Melody;
            junk.notes = { { -2.0, 0.0, 300, 4.0f }, { 3.0, 1.0, 61, -1.0f },
                           { (double) mp.bars * 4.0 + 10.0, 1.0, 60, 0.5f } };
            gen.validate (junk, mp);
            auditPart (junk, mp, base + "/validator-repair", true);
        }
    }

    // ChatDirector regression vocabulary
    CHECK(parseChatIntent("make a bassline").action == ChatAction::Generate, "chat: 'make a bassline'");
    CHECK(parseChatIntent("undo").action == ChatAction::Undo, "chat: 'undo'");
    CHECK(parseChatIntent("128 bpm").action == ChatAction::AdjustOnly, "chat: '128 bpm'");
    CHECK(parseChatIntent("what is a chord?").action == ChatAction::Conversation, "chat: question routes to AI");
    CHECK(parseChatIntent("new idea").action == ChatAction::NewIdea, "chat: 'new idea'");
    CHECK(parseChatIntent("busier hats").action == ChatAction::Generate, "chat: 'busier hats'");
    CHECK(parseChatIntent("switch to deep house").action == ChatAction::AdjustOnly
          || parseChatIntent("switch to deep house").action == ChatAction::GenerateAll,
          "chat: genre switch recognised");

    std::cout << "\nStyles: " << stylesTested << "  parts generated: " << partsGenerated
              << "  distinct failures: " << failCounts.size()
              << "  total failures: " << failures << "\n";
    return failures == 0 ? 0 : 1;
}
