#include "MusicInstructions.h"
#include "MusicTheory.h"
#include "StylePresets.h"
#include "SongPlan.h"
#include "GenerationReport.h"
#include "Critic.h"
#include "ChatDirector.h"
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

using namespace aimidi;

int main()
{
    const auto& styles = allStyles();
    assert (styles.size() == 8);

    // Every style: sane bpm/swing, valid progression degrees, kick present
    for (const auto& st : styles)
    {
        assert (st.bpm >= 110 && st.bpm <= 140);
        assert (st.swing >= 0.0f && st.swing <= 0.5f);
        assert (st.chordTones >= 3 && st.chordTones <= 6);
        for (int d : st.progression) assert (d >= 0 && d <= 6);

        int kickHits = 0;
        for (float v : st.drums[(size_t) DrumPiece::Kick].steps)
            if (v > 0) ++kickHits;
        assert (kickHits >= 2); // every house style needs a kick pattern

        // theory sanity: chord builds in-key for each degree used
        auto ivals = theory::scaleIntervals (st.scale);
        for (int d : st.progression)
        {
            auto c = theory::diatonicChord (60, 0, ivals, d, st.chordTones);
            assert ((int) c.size() == st.chordTones);
        }
        std::printf ("OK  %-14s %3d bpm  swing %.2f  kick hits/bar %d\n",
                     st.name, (int) st.bpm, st.swing, kickHits);
    }

    // Lookup behaviour
    assert (&findStyle ("Tech House")   == &styles[0]);
    assert (&findStyle ("afro vibes like keinemusik") == &findStyle ("Afro House"));
    assert (&findStyle ("john summit")  == &styles[0]);
    assert (&findStyle ("2-step shuffle") == &findStyle ("UK Garage"));
    assert (&findStyle ("anyma afterlife") == &findStyle ("Melodic House"));
    assert (&findStyle ("piano 90s rave")  == &findStyle ("Classic House"));
    assert (&findStyle ("lane 8 sunset")   == &findStyle ("Organic House"));
    assert (findStyleOrNull ("completely unrelated words") == nullptr);
    assert (&findStyle ("garbage-no-match") != nullptr); // falls back safely... 
    
    // 10-piece kit + GM notes valid
    assert ((int) DrumPiece::NumPieces == 10);
    for (int i = 0; i < (int) DrumPiece::NumPieces; ++i)
    {
        int n = drumPieceMidiNote ((DrumPiece) i);
        assert (n >= 35 && n <= 81);
    }

    // ---- SongPlan: deterministic, voice-led ----
    {
        MusicParams p; // Tech House defaults
        const auto& st = findStyle (p.genre);
        auto a = buildSongPlan (p, st.chordTones);
        auto b = buildSongPlan (p, st.chordTones);
        assert (a.degrees == b.degrees);           // same params -> same plan
        assert (a.chords.size() == a.degrees.size());
        assert (! a.chords.empty());

        // voice-leading: consecutive chords stay close (mean move < an octave)
        for (size_t i = 1; i < a.chords.size(); ++i)
        {
            auto mean = [] (const std::vector<int>& c)
            {
                double m = 0; for (int n : c) m += n; return m / (double) c.size();
            };
            assert (std::abs (mean (a.chords[i]) - mean (a.chords[i - 1])) < 12.0);
        }

        // chordAtBeat maps beats onto bars (clamped at the ends)
        assert (&chordAtBeat (a, 0.0)   == &a.chords[0]);
        assert (&chordAtBeat (a, 4.5)   == &a.chords[1]);
        assert (&chordAtBeat (a, 999.0) == &a.chords.back());
        std::printf ("OK  SongPlan deterministic + voice-led (%zu chords)\n",
                     a.chords.size());
    }

    // ---- 8-bar A/B phrase: bar 8 gets a cadential turnaround chord ----
    {
        MusicParams p;
        p.bars = 8;
        const auto& st = findStyle (p.genre);
        auto plan = buildSongPlan (p, st.chordTones);
        assert ((int) plan.degrees.size() == 8);
        // first half: with seed 0, style default template is used
        for (int bar = 0; bar < 4; ++bar)
            assert (plan.degrees[(size_t) bar] == st.progression[(size_t) bar]);
        // bar 8 (index 7) is the deterministic cadence substitute (seed 0)
        assert (plan.degrees[7] == cadenceDegree (st.progression));
        assert (plan.degrees[7] != st.progression[3]); // it actually changes
        // 4-bar loops are untouched by the cadence rule
        MusicParams p4; p4.bars = 4;
        auto plan4 = buildSongPlan (p4, st.chordTones);
        assert (plan4.degrees[3] == st.progression[3]);
        // every style's cadence bar still voice-leads smoothly
        for (const auto& sty : allStyles())
        {
            MusicParams ps; ps.bars = 8; ps.genre = sty.name; ps.scale = sty.scale;
            auto pl = buildSongPlan (ps, sty.chordTones);
            auto mean = [] (const std::vector<int>& c)
            { double m = 0; for (int n : c) m += n; return m / (double) c.size(); };
            assert (std::abs (mean (pl.chords[7]) - mean (pl.chords[6])) < 12.0);
        }
        std::printf ("OK  8-bar A/B plan: cadence bar %d -> degree %d\n",
                     8, plan.degrees[7]);
    }

    // ---- Critic: repairs melody strong beats, bass register, kick clashes ----
    {
        MusicParams p;
        std::array<GeneratedPart, (size_t) InstrumentType::NumTypes> parts;
        std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> kit;
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
            parts[(size_t) t].type = (InstrumentType) t;

        // melody: an out-of-chord note ON beat 1 must get snapped
        parts[(size_t) InstrumentType::Melody].notes.push_back ({ 0.0, 0.5, 61, 0.8f });
        // bass: way out of register
        parts[(size_t) InstrumentType::Bass].notes.push_back ({ 1.0, 0.5, 80, 0.8f });
        // kick at beat 0 + clashing bass note at beat 0 (Tech House = Offbeat style)
        kit[(size_t) DrumPiece::Kick].notes.push_back ({ 0.0, 0.25, 36, 1.0f });
        parts[(size_t) InstrumentType::Bass].notes.push_back ({ 0.0, 0.5, 41, 0.8f });

        auto rep = reviewArrangement (parts, kit, p);
        assert (rep.fixes >= 2); // bass clamp + kick clash at minimum
        const auto& bassNotes = parts[(size_t) InstrumentType::Bass].notes;
        assert (bassNotes[0].pitch >= 28 && bassNotes[0].pitch <= 55);
        assert (bassNotes[1].startBeats > 0.2); // nudged off the kick

        // melody strong beat now sits on a chord tone (snapped or already ok)
        {
            const auto& st2 = findStyle (p.genre);
            auto plan = buildSongPlan (p, st2.chordTones);
            const auto& mel = parts[(size_t) InstrumentType::Melody].notes[0];
            assert (theory::isChordTone (mel.pitch, chordAtBeat (plan, 0.0)));
        }

        // locked lanes are untouched
        std::array<GeneratedPart, (size_t) InstrumentType::NumTypes> parts2;
        std::array<GeneratedPart, (size_t) DrumPiece::NumPieces> kit2;
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
            parts2[(size_t) t].type = (InstrumentType) t;
        parts2[(size_t) InstrumentType::Bass].locked = true;
        parts2[(size_t) InstrumentType::Bass].notes.push_back ({ 1.0, 0.5, 80, 0.8f });
        auto rep2 = reviewArrangement (parts2, kit2, p);
        assert (parts2[(size_t) InstrumentType::Bass].notes[0].pitch == 80);
        std::printf ("OK  Critic repairs (%d fixes) + respects locks (%d)\n",
                     rep.fixes, rep2.fixes);
    }

    // ---- ChatDirector: local intent parser ----
    {
        auto has = [] (const ChatIntent& in, InstrumentType t)
        {
            return std::find (in.lanes.begin(), in.lanes.end(), t) != in.lanes.end();
        };
        auto hasP = [] (const ChatIntent& in, DrumPiece p)
        {
            return std::find (in.pieces.begin(), in.pieces.end(), p) != in.pieces.end();
        };

        // Generate specific lanes
        {
            auto in = parseChatIntent ("make a bassline");
            assert (in.action == ChatAction::Generate);
            assert (has (in, InstrumentType::Bass) && in.lanes.size() == 1);
        }
        {
            auto in = parseChatIntent ("can you write a new melody and chords");
            assert (in.action == ChatAction::Generate);
            assert (has (in, InstrumentType::Melody) && has (in, InstrumentType::Chords));
        }
        // Terse lane name alone still generates
        {
            auto in = parseChatIntent ("bassline");
            assert (in.action == ChatAction::Generate && has (in, InstrumentType::Bass));
        }

        // Drum pieces
        {
            auto in = parseChatIntent ("give me a new kick and open hats");
            assert (in.action == ChatAction::Generate);
            assert (hasP (in, DrumPiece::Kick) && hasP (in, DrumPiece::OpenHat));
            assert (has (in, InstrumentType::Drums) && ! in.wholeKit);
        }
        {
            auto in = parseChatIntent ("regenerate the drums");
            assert (in.action == ChatAction::Generate && in.wholeKit);
        }
        {
            auto in = parseChatIntent ("more percussion please");
            assert (hasP (in, DrumPiece::Shaker) && hasP (in, DrumPiece::CongaHi)
                    && hasP (in, DrumPiece::CongaLo) && hasP (in, DrumPiece::Rim));
        }

        // Generate all / new idea / vary / undo / help
        assert (parseChatIntent ("generate everything").action == ChatAction::GenerateAll);
        assert (parseChatIntent ("make a full track").action == ChatAction::GenerateAll);
        assert (parseChatIntent ("generate").action == ChatAction::GenerateAll);
        assert (parseChatIntent ("new idea").action == ChatAction::NewIdea);
        assert (parseChatIntent ("surprise me").action == ChatAction::NewIdea);
        {
            auto in = parseChatIntent ("vary the melody");
            assert (in.action == ChatAction::Vary && has (in, InstrumentType::Melody));
        }
        assert (parseChatIntent ("undo that").action == ChatAction::Undo);
        assert (parseChatIntent ("help").action == ChatAction::Help);
        assert (parseChatIntent ("what can you do").action == ChatAction::Help);

        // BPM / bars
        {
            auto in = parseChatIntent ("128 bpm");
            assert (in.action == ChatAction::AdjustOnly && in.bpm == 128.0);
            assert (! in.paramChangeWantsRegen()); // bpm alone: no regen
        }
        {
            auto in = parseChatIntent ("set tempo at 122");
            assert (in.bpm == 122.0);
        }
        {
            auto in = parseChatIntent ("make it faster");
            assert (in.action == ChatAction::AdjustOnly && in.bpmDelta > 0.0);
        }
        {
            auto in = parseChatIntent ("8 bars please");
            assert (in.action == ChatAction::AdjustOnly && in.bars == 8);
            assert (in.paramChangeWantsRegen());
        }

        // Key parsing
        {
            auto in = parseChatIntent ("switch to f# minor");
            assert (in.root == "F#" && in.scale == "minor");
            assert (in.action == ChatAction::AdjustOnly);
        }
        {
            auto in = parseChatIntent ("in the key of a major");
            assert (in.root == "A" && in.scale == "major");
        }
        {
            // article "a" must NOT become the key of A
            auto in = parseChatIntent ("make a minor tweak to the bassline");
            assert (in.root.empty());
        }
        {
            auto in = parseChatIntent ("try eb dorian");
            assert (in.root == "Eb" && in.scale == "dorian");
        }
        {
            auto in = parseChatIntent ("darker please");
            assert (in.scale == "minor" && in.action == ChatAction::AdjustOnly);
        }

        // Genre via presets
        {
            auto in = parseChatIntent ("switch to afro house");
            assert (in.genre == std::string ("Afro House"));
            assert (in.action == ChatAction::AdjustOnly);
        }
        {
            auto in = parseChatIntent ("make some keinemusik vibes");
            assert (in.genre == std::string ("Afro House"));
            assert (in.action == ChatAction::GenerateAll);
        }

        // Feel dials
        {
            auto in = parseChatIntent ("make it harder and busier");
            assert (in.energyDelta > 0.0f && in.densityDelta > 0.0f);
            assert (in.action == ChatAction::AdjustOnly);
        }
        {
            auto in = parseChatIntent ("make it looser with some swing");
            assert (in.humanizeDelta > 0.0f);
            assert (in.swingDelta > 0.0f);
            auto in2 = parseChatIntent ("quantize it, too sloppy");
            assert (in2.humanizeDelta == 0.0f); // opposing words cancel safely
        }
        {
            auto in = parseChatIntent ("simpler hats");
            assert (in.densityDelta < 0.0f && hasP (in, DrumPiece::ClosedHat));
            assert (in.action == ChatAction::Generate); // dial change + rebuild hats only
        }

        // Conversation falls through to the AI
        assert (parseChatIntent ("what is a chord inversion?").action == ChatAction::Conversation);
        assert (parseChatIntent ("how does sidechain compression work").action == ChatAction::Conversation);
        assert (parseChatIntent ("why does my melody clash with the chords?").action == ChatAction::Conversation);
        assert (parseChatIntent ("i love this one").action == ChatAction::None);
        assert (parseChatIntent ("").action == ChatAction::None);

        // Question + explicit command still executes locally
        {
            auto in = parseChatIntent ("can you make a bassline?");
            assert (in.action == ChatAction::Generate && has (in, InstrumentType::Bass));
        }
        // Advice-seeking never triggers generation, even with a lane named
        assert (parseChatIntent ("give me feedback on the melody").action == ChatAction::Conversation);
        assert (parseChatIntent ("any suggestions for my chords").action == ChatAction::Conversation);

        assert (std::string (chatDirectorHelpText()).size() > 100);
        std::printf ("OK  ChatDirector: 30+ intent parses routed correctly\n");
    }

    // ---- Seed-aware SongPlan: different seeds → different harmony; same seed stable ----
    {
        MusicParams base;
        base.genre = "Tech House";
        base.bars = 8;
        base.seed = 0;
        const auto& st = findStyle (base.genre);
        auto a = buildSongPlan (base, st.chordTones);
        auto a2 = buildSongPlan (base, st.chordTones);
        assert (a.degrees == a2.degrees);
        assert (songPlanFingerprint (a) == songPlanFingerprint (a2));

        std::set<std::string> fingerprints;
        fingerprints.insert (songPlanFingerprint (a));
        for (unsigned int s = 1; s < 48; ++s)
        {
            MusicParams p = base;
            p.seed = s;
            fingerprints.insert (songPlanFingerprint (buildSongPlan (p, st.chordTones)));
        }
        assert (fingerprints.size() >= 3); // seed must change harmonic identity

        // Shared plan: every instrument call with the same params builds the
        // same SongPlan (chords/bass/pad/melody all consume this skeleton).
        MusicParams shared = base;
        shared.seed = 17;
        auto planChords = buildSongPlan (shared, st.chordTones);
        auto planBass   = buildSongPlan (shared, st.chordTones);
        auto planPad    = buildSongPlan (shared, st.chordTones);
        assert (planChords.degrees == planBass.degrees);
        assert (planBass.degrees == planPad.degrees);
        assert (songPlanFingerprint (planChords) == songPlanFingerprint (planPad));
        // Same key, different seed → different fingerprint (not rejected merely for key)
        MusicParams other = shared;
        other.seed = 18;
        assert (songPlanFingerprint (buildSongPlan (other, st.chordTones))
                != songPlanFingerprint (planChords));
        std::printf ("OK  Seed-aware SongPlan (%zu unique fps in 48 seeds) + shared harmony\n",
                     fingerprints.size());
    }

    // ---- New Idea local: rolled seed avoids previous fingerprint ----
    {
        MusicParams p;
        p.genre = "Deep House";
        p.bars = 8;
        p.seed = 3;
        const auto& st = findStyle (p.genre);
        const auto prev = songPlanFingerprint (buildSongPlan (p, st.chordTones));
        bool foundDifferent = false;
        for (int tryIdx = 0; tryIdx < 24; ++tryIdx)
        {
            p.seed = 1u + (unsigned int) (1000 + tryIdx * 17);
            if (songPlanFingerprint (buildSongPlan (p, st.chordTones)) != prev)
            {
                foundDifferent = true;
                break;
            }
        }
        assert (foundDifferent);
        std::printf ("OK  New Idea can escape previous chord fingerprint\n");
    }

    // ---- Generation source labels (no silent AI claim for offline / failure) ----
    {
        assert (std::string (generationModeLabel (GenerationMode::ClaudeBrain))
                    == "Generated with Claude");
        assert (std::string (generationModeLabel (GenerationMode::OfflineLocal))
                    .find ("local") != std::string::npos);
        assert (std::string (generationModeLabel (GenerationMode::FailedClaude))
                    == "Claude failed");
        GenerationReport failed;
        failed.mode = GenerationMode::FailedClaude;
        failed.ok = false;
        failed.statusLine = "Claude failed";
        failed.detail = "HTTP 401";
        assert (! failed.ok);
        assert (failed.statusLine.find ("Claude") != std::string::npos);
        assert (failed.detail.find ("401") != std::string::npos);
        // Offline still identified clearly — never as Claude
        GenerationReport offline;
        offline.mode = GenerationMode::OfflineLocal;
        offline.ok = true;
        offline.statusLine = "Generated locally — no API key";
        assert (std::string (generationModeLabel (offline.mode)).find ("Claude")
                == std::string::npos);
        assert (offline.statusLine.find ("Claude") == std::string::npos);
        std::printf ("OK  Generation source labels distinguish Claude / local / failure\n");
    }

    // ---- Offline SongPlan still works without any API key concept ----
    {
        MusicParams p;
        p.seed = 42;
        p.bars = 4;
        const auto& st = findStyle (p.genre);
        auto plan = buildSongPlan (p, st.chordTones);
        assert ((int) plan.chords.size() == p.bars);
        assert (! plan.chords[0].empty());
        // In-key: every chord tone pitch-class is diatonic
        const int rootPc = theory::rootPitchClass (p.root);
        const auto ivals = theory::scaleIntervals (p.scale);
        for (const auto& chord : plan.chords)
            for (int n : chord)
            {
                const int pc = ((n % 12) - rootPc + 12) % 12;
                bool inScale = false;
                for (int iv : ivals) if (iv == pc) inScale = true;
                assert (inScale);
            }
        std::printf ("OK  Offline SongPlan works without API key (in-key chords)\n");
    }

    std::printf ("\nAll engine tests passed.\n");
    return 0;
}
