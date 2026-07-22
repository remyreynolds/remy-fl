#pragma once

#include "MusicInstructions.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <string>
#include <vector>

namespace aimidi
{
/** Data-driven style presets for modern house sub-genres (2025/26 era).

    Each preset captures how a style actually grooves — BPM, swing, drum
    step-patterns per kit piece, bassline behaviour, chord voicing/rhythm and
    a go-to progression — so the deterministic engine renders idiomatic
    patterns and the AI only has to pick a style + move dials.

    Header-only and JUCE-free, like the rest of the engine, so it stays
    unit-testable in isolation. */

//==============================================================================
enum class BassStyle
{
    OffbeatEighths,     // classic house: 1/8 notes on the "and"s
    RollingSixteenths,  // tech house: relentless rolling 16ths, octave pops
    SubSustained,       // deep/organic: long low sub notes
    StabSyncopated,     // afro house: syncopated tribal stabs
    GarageSub           // UKG/speed garage: 2-step sub with pitch movement
};

enum class ChordMode
{
    Stabs,          // short punchy hits on selected beats
    OffbeatStabs,   // classic piano/organ house: stabs on every "and"
    Sustained,      // held pads/keys, one chord per bar
    Plucked         // rhythmic short plucks across the bar
};

//==============================================================================
/** One kit-piece groove: 16 velocity steps (1/16ths of one bar, 0 = silent)
    plus a per-step trigger probability for controlled variation. */
struct DrumPattern
{
    std::array<float, 16> steps {};
    float probability = 1.0f;
};

/** Build a pattern from explicit step indices. */
inline DrumPattern hits (std::initializer_list<int> stepIdx, float vel, float prob = 1.0f)
{
    DrumPattern p;
    p.probability = prob;
    for (int i : stepIdx)
        if (i >= 0 && i < 16)
            p.steps[(size_t) i] = vel;
    return p;
}

/** Build a pattern that repeats every `interval` steps starting at `offset`. */
inline DrumPattern every (int interval, int offset, float vel, float prob = 1.0f)
{
    DrumPattern p;
    p.probability = prob;
    for (int i = offset; i < 16; i += std::max (1, interval))
        p.steps[(size_t) i] = vel;
    return p;
}

/** Build a pattern from a 4-step motif repeated each beat (velocities). */
inline DrumPattern perBeat (std::array<float, 4> motif, float prob = 1.0f)
{
    DrumPattern p;
    p.probability = prob;
    for (int i = 0; i < 16; ++i)
        p.steps[(size_t) i] = motif[(size_t) (i % 4)];
    return p;
}

inline DrumPattern silent() { return {}; }

//==============================================================================
struct StylePreset
{
    const char* name;      // exact display / AI-facing name
    const char* keywords;  // lowercase aliases & artist cues, comma-separated
    const char* sounds;    // recommended patches/kits — final tone lives in
                           // the DAW, so we guide instead of synthesize
    const char* vibe;      // one-liner used in the AI system prompt + UI
    double bpm;
    float  swing;          // 0..1
    const char* scale;     // suggested default scale
    int chordTones;        // 3=triad, 4=7th, 5=9th, 6=11th
    ChordMode chordMode;
    BassStyle bassStyle;
    std::array<int, 4> progression; // scale degrees, one per bar
    std::array<DrumPattern, (size_t) DrumPiece::NumPieces> drums;
};

namespace detail
{
    inline std::array<DrumPattern, (size_t) DrumPiece::NumPieces>
        kit (DrumPattern kick,   DrumPattern snare,  DrumPattern clap,
             DrumPattern chat,   DrumPattern ohat,   DrumPattern ride,
             DrumPattern shaker, DrumPattern rim,
             DrumPattern congaHi, DrumPattern congaLo)
    {
        std::array<DrumPattern, (size_t) DrumPiece::NumPieces> a;
        a[(size_t) DrumPiece::Kick]      = kick;
        a[(size_t) DrumPiece::Snare]     = snare;
        a[(size_t) DrumPiece::Clap]      = clap;
        a[(size_t) DrumPiece::ClosedHat] = chat;
        a[(size_t) DrumPiece::OpenHat]   = ohat;
        a[(size_t) DrumPiece::Ride]      = ride;
        a[(size_t) DrumPiece::Shaker]    = shaker;
        a[(size_t) DrumPiece::Rim]       = rim;
        a[(size_t) DrumPiece::CongaHi]   = congaHi;
        a[(size_t) DrumPiece::CongaLo]   = congaLo;
        return a;
    }
} // namespace detail

//==============================================================================
inline const std::vector<StylePreset>& allStyles()
{
    using detail::kit;

    static const std::vector<StylePreset> styles =
    {
        { "Tech House",
          "tech house,tech-house,techhouse,john summit,fisher,toolroom,hot creations,rolling",
          "Bass: Serum/Vital growly sub · Chords: dark analog stab (Diva) · Kit: punchy 909-style, tight claps",
          "Rolling 16th bassline, tight punchy drums, sparse dark stabs (John Summit / FISHER lane)",
          126.0, 0.12f, "minor", 4, ChordMode::Stabs, BassStyle::RollingSixteenths,
          { 0, 0, 5, 3 },
          kit (every (4, 0, 0.97f),                    // kick: four-on-the-floor
               silent(),                               // snare
               hits ({ 4, 12 }, 0.88f),                // clap on 2 & 4
               perBeat ({ 0.35f, 0.18f, 0.60f, 0.18f }, 0.9f), // 16th hats, "and" accent
               every (4, 2, 0.62f),                    // offbeat open hat
               silent(),
               perBeat ({ 0.0f, 0.28f, 0.0f, 0.28f }, 0.7f),   // light shaker 16ths
               hits ({ 3, 7, 11 }, 0.5f, 0.6f),        // syncopated rim percs
               silent(), silent()) },

        { "Bass House",
          "bass house,basshouse,chris lake,night bass,g-house",
          "Bass: heavy wobble/talk bass (Serum) · Chords: minimal dark keys · Kit: hard-hitting house kit",
          "Heavy offbeat bass energy, minimal dark chords, hard drums (Chris Lake lane)",
          128.0, 0.08f, "minor", 3, ChordMode::Stabs, BassStyle::OffbeatEighths,
          { 0, 0, 0, 5 },
          kit (every (4, 0, 1.0f),
               silent(),
               hits ({ 4, 12 }, 0.92f),
               perBeat ({ 0.40f, 0.0f, 0.62f, 0.0f }, 0.9f),
               every (4, 2, 0.55f),
               silent(),
               silent(),
               hits ({ 7, 15 }, 0.45f, 0.5f),
               silent(), silent()) },

        { "Afro House",
          "afro,afro house,keinemusik,black coffee,&me,rampa,tribal,adam port",
          "Bass: warm round sub · Keys: Rhodes/mallets or organic pluck · Kit: live congas, shakers, log drums",
          "Organic tribal percussion (congas, shakers), emotive 9th chords, syncopated bass (Keinemusik / Black Coffee lane)",
          120.0, 0.22f, "minor", 5, ChordMode::Sustained, BassStyle::StabSyncopated,
          { 0, 3, 5, 4 },
          kit (every (4, 0, 0.9f),
               silent(),
               hits ({ 4, 12 }, 0.6f),
               perBeat ({ 0.25f, 0.18f, 0.32f, 0.18f }, 0.8f),
               hits ({ 2, 10 }, 0.45f, 0.7f),
               silent(),
               every (1, 0, 0.34f, 0.9f),              // driving shaker 16ths
               hits ({ 3, 6, 11, 14 }, 0.55f, 0.8f),   // rim syncopation
               hits ({ 2, 5, 7, 13 }, 0.62f, 0.85f),   // hi conga
               hits ({ 3, 8, 11 }, 0.68f, 0.85f)) },   // low conga

        { "Melodic House",
          "melodic,melodic house,melodic techno,anyma,artbat,afterlife,tale of us,progressive",
          "Arp: analog saw pluck w/ delay · Pad: wide Juno/Diva strings · Kit: clean techno kit, soft claps",
          "Driving arps, big emotional pads, hypnotic minor progressions (Anyma / ARTBAT lane)",
          124.0, 0.05f, "minor", 4, ChordMode::Sustained, BassStyle::OffbeatEighths,
          { 0, 5, 3, 6 },
          kit (every (4, 0, 0.95f),
               silent(),
               hits ({ 4, 12 }, 0.7f),
               every (2, 1, 0.45f),
               every (4, 2, 0.5f),
               every (2, 0, 0.3f, 0.6f),               // washy ride 8ths
               silent(),
               hits ({ 14 }, 0.4f, 0.5f),
               silent(), silent()) },

        { "Deep House",
          "deep,deep house,warm,dusky,late night",
          "Chords: Rhodes/EP with chorus · Bass: deep sine/moog sub · Kit: dusty MPC-style kit, vinyl swing",
          "Warm m9/m11 chords, deep sub bass, smoky swung hats",
          122.0, 0.18f, "minor", 5, ChordMode::OffbeatStabs, BassStyle::SubSustained,
          { 0, 5, 3, 4 },
          kit (every (4, 0, 0.9f),
               silent(),
               hits ({ 4, 12 }, 0.75f),
               perBeat ({ 0.3f, 0.15f, 0.5f, 0.15f }, 0.85f),
               every (4, 2, 0.6f),
               silent(),
               every (2, 1, 0.25f, 0.6f),
               hits ({ 7 }, 0.4f, 0.4f),
               silent(), silent()) },

        { "Organic House",
          "organic,organic house,lane 8,ben bohmer,böhmer,anjunadeep,downtempo,sunset",
          "Keys: felt piano/kalimba plucks · Pad: airy granular · Kit: hand percussion, soft foley hits",
          "Mellow downtempo grooves, hand percussion, plucked chords (Lane 8 / Anjunadeep lane)",
          118.0, 0.20f, "dorian", 5, ChordMode::Plucked, BassStyle::SubSustained,
          { 0, 2, 5, 3 },
          kit (every (4, 0, 0.85f),
               silent(),
               hits ({ 12 }, 0.5f),
               perBeat ({ 0.2f, 0.14f, 0.26f, 0.14f }, 0.7f),
               hits ({ 6 }, 0.4f, 0.6f),
               silent(),
               every (1, 0, 0.3f, 0.85f),
               hits ({ 3, 10 }, 0.5f, 0.7f),
               hits ({ 1, 7, 9 }, 0.55f, 0.8f),
               hits ({ 5, 12 }, 0.6f, 0.8f)) },

        { "UK Garage",
          "garage,uk garage,ukg,2-step,2 step,speed garage,bassline,interplanetary",
          "Bass: warping reese/sub (Serum FM) · Chords: filtered vocal-chop keys · Kit: crisp UKG kit, shuffled",
          "Shuffled 2-step drums, skipping hats, warping sub bass (speed-garage revival lane)",
          132.0, 0.35f, "minor", 5, ChordMode::Stabs, BassStyle::GarageSub,
          { 0, 5, 3, 6 },
          kit (hits ({ 0, 10 }, 0.95f),                // 2-step kick (not 4otf)
               hits ({ 4, 12 }, 0.88f),                // snare on 2 & 4
               hits ({ 12 }, 0.5f, 0.5f),              // clap layer on 4
               perBeat ({ 0.42f, 0.0f, 0.52f, 0.3f }, 0.85f),  // skipping hats
               hits ({ 2, 14 }, 0.5f, 0.7f),
               silent(),
               every (2, 1, 0.3f, 0.6f),
               hits ({ 7, 11 }, 0.5f, 0.6f),
               silent(), silent()) },

        { "Classic House",
          "classic,classic house,piano,piano house,90s,gospel,disco,french",
          "Chords: bright piano/M1 organ · Bass: plucky analog octave bass · Kit: classic 909, big claps",
          "90s piano-house energy: gospel 7th stabs on the offbeats, disco-loop drums",
          124.0, 0.15f, "major", 4, ChordMode::OffbeatStabs, BassStyle::OffbeatEighths,
          { 0, 4, 5, 3 },
          kit (every (4, 0, 0.95f),
               hits ({ 12 }, 0.4f, 0.5f),
               hits ({ 4, 12 }, 0.85f),
               every (2, 1, 0.5f),
               every (4, 2, 0.65f),
               every (4, 0, 0.25f, 0.5f),
               silent(),
               silent(),
               silent(), silent()) },
    };

    return styles;
}

//==============================================================================
/** Case-insensitive style lookup: exact/partial name match first, then
    keyword/artist-cue match. Returns nullptr when nothing matches. */
inline const StylePreset* findStyleOrNull (const std::string& genreOrPrompt)
{
    auto lower = [] (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(),
                        [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    };

    const auto q = lower (genreOrPrompt);
    const auto& styles = allStyles();

    // 1) name match (either direction, so "Tech House" == "tech house vibes")
    for (const auto& st : styles)
    {
        const auto n = lower (st.name);
        if (q == n || q.find (n) != std::string::npos)
            return &st;
    }

    // 2) keyword / artist-cue match
    for (const auto& st : styles)
    {
        std::string kws (st.keywords);
        size_t pos = 0;
        while (pos != std::string::npos)
        {
            const auto next  = kws.find (',', pos);
            const auto token = kws.substr (pos, next == std::string::npos ? std::string::npos
                                                                          : next - pos);
            if (! token.empty() && q.find (token) != std::string::npos)
                return &st;
            pos = next == std::string::npos ? next : next + 1;
        }
    }

    return nullptr;
}

/** Like findStyleOrNull, but falls back to the first style (Tech House) so
    the engine always has a usable groove. */
inline const StylePreset& findStyle (const std::string& genreOrPrompt)
{
    if (auto* st = findStyleOrNull (genreOrPrompt))
        return *st;
    return allStyles().front();
}

} // namespace aimidi
