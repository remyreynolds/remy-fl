#pragma once

#include "MusicInstructions.h"
#include "StylePresets.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace aimidi
{
/** Local-first chat "director".

    Most chat messages in a music tool are COMMANDS ("make a bassline",
    "128 bpm", "switch to deep house", "undo"), not conversation. Round-tripping
    those to an LLM is slow, expensive, and error-prone. This parser recognises
    the command vocabulary locally so the plugin can act in ~0 ms with the
    deterministic engine; only genuine conversation falls through to the AI.

    JUCE-free by design so it is unit-testable with a plain g++ harness. */

enum class ChatAction
{
    None,          // nothing recognised — fall through to the AI
    Conversation,  // explicitly conversational — fall through to the AI
    Generate,      // (re)generate specific lanes / drum pieces
    GenerateAll,   // (re)generate every unlocked lane
    NewIdea,       // fresh seed + generate all
    Vary,          // variation of specific lanes (new seed, same params)
    Undo,          // revert last generation
    Help,          // list the local command vocabulary
    AdjustOnly     // parameter changes only (bpm/key/genre/dials)
};

struct ChatIntent
{
    ChatAction action = ChatAction::None;

    std::vector<InstrumentType> lanes;   // specific lanes named
    std::vector<DrumPiece> pieces;       // specific drum pieces named
    bool wholeKit = false;               // "drums"/"beat" without a piece

    // Parameter changes (0 / empty = not requested)
    double bpm       = 0.0;              // absolute BPM
    double bpmDelta  = 0.0;              // relative ("faster"/"slower")
    std::string root;                    // e.g. "F#"
    std::string scale;                   // e.g. "minor"
    std::string genre;                   // matched style-preset name
    int bars = 0;

    // Creative-dial nudges (added to current value, then clamped 0..1)
    float energyDelta   = 0.0f;
    float densityDelta  = 0.0f;
    float swingDelta    = 0.0f;
    float humanizeDelta = 0.0f;

    bool hasParamChange() const
    {
        return bpm > 0.0 || bpmDelta != 0.0 || ! root.empty() || ! scale.empty()
            || ! genre.empty() || bars > 0 || energyDelta != 0.0f
            || densityDelta != 0.0f || swingDelta != 0.0f || humanizeDelta != 0.0f;
    }

    /** Param changes that should trigger a regenerate of unlocked parts
        (BPM alone just changes playback speed — no regen needed). */
    bool paramChangeWantsRegen() const
    {
        return ! root.empty() || ! scale.empty() || ! genre.empty() || bars > 0
            || energyDelta != 0.0f || densityDelta != 0.0f
            || swingDelta != 0.0f || humanizeDelta != 0.0f;
    }
};

namespace chatdetail
{
    inline std::string normalize (const std::string& in)
    {
        std::string out;
        out.reserve (in.size() + 2);
        out.push_back (' ');
        for (unsigned char c : in)
        {
            if (std::isalnum (c) || c == '#')
                out.push_back ((char) std::tolower (c));
            else
                out.push_back (' ');
        }
        out.push_back (' ');
        return out;
    }

    inline std::vector<std::string> tokenize (const std::string& padded)
    {
        std::vector<std::string> toks;
        std::istringstream ss (padded);
        std::string t;
        while (ss >> t)
            toks.push_back (t);
        return toks;
    }

    /** Whole-word match inside the space-padded lowercase text. */
    inline bool hasWord (const std::string& padded, const char* word)
    {
        return padded.find (" " + std::string (word) + " ") != std::string::npos;
    }

    inline bool hasAny (const std::string& padded, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (hasWord (padded, w))
                return true;
        return false;
    }

    inline bool isRootToken (const std::string& t)
    {
        if (t.empty() || t.size() > 2) return false;
        const char n = t[0];
        if (n < 'a' || n > 'g') return false;
        if (t.size() == 2 && t[1] != '#' && t[1] != 'b') return false;
        return true;
    }

    inline std::string canonicalRoot (const std::string& t)
    {
        std::string r;
        r.push_back ((char) std::toupper ((unsigned char) t[0]));
        if (t.size() == 2)
            r.push_back (t[1]); // '#' stays, 'b' stays lowercase (matches MusicTheory flats)
        return r;
    }

    /** Maps a scale token to the engine's canonical scale names ("" = not a scale). */
    inline std::string scaleFromToken (const std::string& t)
    {
        if (t == "minor" || t == "min" || t == "aeolian")    return "minor";
        if (t == "major" || t == "maj" || t == "ionian")     return "major";
        if (t == "dorian")     return "dorian";
        if (t == "phrygian")   return "phrygian";
        if (t == "lydian")     return "lydian";
        if (t == "mixolydian") return "mixolydian";
        return {};
    }

    inline bool isNumber (const std::string& t)
    {
        return ! t.empty() && std::all_of (t.begin(), t.end(),
                                           [] (unsigned char c) { return std::isdigit (c); });
    }
} // namespace chatdetail

/** Parse one chat message into a locally-executable intent.
    Returns action == None/Conversation when the message should go to the AI. */
inline ChatIntent parseChatIntent (const std::string& message)
{
    using namespace chatdetail;

    ChatIntent intent;
    const auto padded = normalize (message);
    const auto toks   = tokenize (padded);
    if (toks.empty())
        return intent;

    //==========================================================================
    // 1) Whole-message commands
    if (hasAny (padded, { "help", "commands" })
        || padded.find (" what can you do ") != std::string::npos)
    {
        intent.action = ChatAction::Help;
        return intent;
    }

    if (hasAny (padded, { "undo", "revert" })
        || padded.find (" go back ") != std::string::npos)
    {
        intent.action = ChatAction::Undo;
        return intent;
    }

    const bool newIdea = padded.find (" new idea ")   != std::string::npos
                      || padded.find (" start over ") != std::string::npos
                      || padded.find (" start fresh ")!= std::string::npos
                      || padded.find (" surprise me ")!= std::string::npos
                      || padded.find (" from scratch ") != std::string::npos;

    //==========================================================================
    // 2) Vocabulary scans
    const bool genVerb  = hasAny (padded, { "make", "generate", "create", "build",
                                            "write", "roll", "reroll", "regenerate",
                                            "redo", "another", "gimme", "give" });
    const bool varyVerb = hasAny (padded, { "vary", "variation", "different",
                                            "remix", "flip" });

    // Lanes
    auto addLane = [&intent] (InstrumentType t)
    {
        if (std::find (intent.lanes.begin(), intent.lanes.end(), t) == intent.lanes.end())
            intent.lanes.push_back (t);
    };
    if (hasAny (padded, { "bass", "bassline", "sub", "808" }))       addLane (InstrumentType::Bass);
    if (hasAny (padded, { "melody", "lead", "hook", "topline" }))    addLane (InstrumentType::Melody);
    if (hasAny (padded, { "chord", "chords", "keys", "stabs", "piano", "harmony" }))
                                                                     addLane (InstrumentType::Chords);
    if (hasAny (padded, { "pad", "pads", "atmosphere", "texture" })) addLane (InstrumentType::Pad);
    if (hasAny (padded, { "arp", "arpeggio", "arps" }))              addLane (InstrumentType::Arp);
    if (hasAny (padded, { "counter", "countermelody" }))             addLane (InstrumentType::CounterMelody);

    // Drum pieces
    auto addPiece = [&intent] (DrumPiece p)
    {
        if (std::find (intent.pieces.begin(), intent.pieces.end(), p) == intent.pieces.end())
            intent.pieces.push_back (p);
    };
    if (hasAny (padded, { "kick", "kicks" }))            addPiece (DrumPiece::Kick);
    if (hasAny (padded, { "snare", "snares" }))          addPiece (DrumPiece::Snare);
    if (hasAny (padded, { "clap", "claps" }))            addPiece (DrumPiece::Clap);
    if (hasAny (padded, { "hat", "hats", "hihat", "hihats", "hh" }))
        addPiece (hasWord (padded, "open") ? DrumPiece::OpenHat : DrumPiece::ClosedHat);
    if (hasAny (padded, { "ride" }))                     addPiece (DrumPiece::Ride);
    if (hasAny (padded, { "shaker", "shakers" }))        addPiece (DrumPiece::Shaker);
    if (hasAny (padded, { "rim", "rimshot" }))           addPiece (DrumPiece::Rim);
    if (hasAny (padded, { "conga", "congas" }))          { addPiece (DrumPiece::CongaHi); addPiece (DrumPiece::CongaLo); }
    if (hasAny (padded, { "perc", "percussion" }))
        { addPiece (DrumPiece::Shaker); addPiece (DrumPiece::Rim); addPiece (DrumPiece::CongaHi); addPiece (DrumPiece::CongaLo); }

    if (hasAny (padded, { "drums", "drum", "kit", "beat", "groove" }) && intent.pieces.empty())
    {
        intent.wholeKit = true;
        addLane (InstrumentType::Drums);
    }
    if (! intent.pieces.empty())
        addLane (InstrumentType::Drums);

    const bool allWords = hasAny (padded, { "everything", "all", "whole", "full",
                                            "track", "song", "loop" });

    //==========================================================================
    // 3) Numbers: BPM and bars
    for (size_t i = 0; i < toks.size(); ++i)
    {
        if (isNumber (toks[i]))
        {
            const int v = std::stoi (toks[i]);
            const bool nextIsBpm  = i + 1 < toks.size() && toks[i + 1] == "bpm";
            const bool nextIsBars = i + 1 < toks.size() && (toks[i + 1] == "bars" || toks[i + 1] == "bar");
            const bool prevIsAt   = i > 0 && (toks[i - 1] == "at" || toks[i - 1] == "bpm" || toks[i - 1] == "tempo");

            if (nextIsBpm || (prevIsAt && v >= 60 && v <= 200))
                intent.bpm = (double) v;
            else if (nextIsBars && v >= 1 && v <= 64)
                intent.bars = v;
        }
    }
    if (hasAny (padded, { "faster", "quicker", "speedup" }))  intent.bpmDelta += 4.0;
    if (hasAny (padded, { "slower", "slowdown" }))            intent.bpmDelta -= 4.0;

    //==========================================================================
    // 4) Key: "<root> <scale>" pairs (root 'a' needs a preceding in/of/to/key)
    for (size_t i = 0; i + 1 < toks.size(); ++i)
    {
        if (! isRootToken (toks[i]))
            continue;
        std::string sc = scaleFromToken (toks[i + 1]);
        if (toks[i + 1] == "harmonic" && i + 2 < toks.size() && toks[i + 2] == "minor")
            sc = "harmonicMinor";
        if (sc.empty())
            continue;
        if (toks[i] == "a") // "a minor" vs the article "a" — require context
        {
            const bool ctx = i > 0 && (toks[i - 1] == "in" || toks[i - 1] == "of"
                                    || toks[i - 1] == "to" || toks[i - 1] == "key");
            if (! ctx)
                continue;
        }
        intent.root  = canonicalRoot (toks[i]);
        intent.scale = sc;
        break;
    }
    // "harmonic minor" / bare scale switch without a root ("switch to dorian")
    if (intent.scale.empty())
    {
        if (padded.find (" harmonic minor ") != std::string::npos) intent.scale = "harmonicMinor";
        else if (hasWord (padded, "dorian"))     intent.scale = "dorian";
        else if (hasWord (padded, "phrygian"))   intent.scale = "phrygian";
        else if (hasWord (padded, "lydian"))     intent.scale = "lydian";
        else if (hasWord (padded, "mixolydian")) intent.scale = "mixolydian";
    }

    //==========================================================================
    // 5) Genre via style presets. findStyleOrNull's substring keyword match is
    //    too loose for chat ("bassline" would hit UK Garage), so match style
    //    names and keywords as whole words/phrases and skip terms that collide
    //    with lane vocabulary.
    {
        auto matchPhrase = [&padded] (std::string phrase)
        {
            phrase = normalize (phrase);              // " tech house "
            return padded.find (phrase) != std::string::npos;
        };
        static const char* laneCollisions[] = { "bassline", "bass", "keys", "piano",
                                                "chords", "pads", "sub", "hats" };
        auto collides = [] (const std::string& kw)
        {
            for (auto* c : laneCollisions)
                if (kw == c)
                    return true;
            return false;
        };

        const StylePreset* match = nullptr;
        for (const auto& st : allStyles())            // 1) names
            if (matchPhrase (st.name)) { match = &st; break; }
        if (match == nullptr)
            for (const auto& st : allStyles())        // 2) keywords
            {
                std::string kws (st.keywords);
                size_t pos = 0;
                while (pos != std::string::npos && match == nullptr)
                {
                    const auto next  = kws.find (',', pos);
                    const auto token = kws.substr (pos, next == std::string::npos
                                                        ? std::string::npos : next - pos);
                    if (! token.empty() && ! collides (token) && matchPhrase (token))
                        match = &st;
                    pos = next == std::string::npos ? next : next + 1;
                }
                if (match != nullptr)
                    break;
            }
        if (match != nullptr)
            intent.genre = match->name;
    }

    //==========================================================================
    // 6) Creative-dial nudges
    if (hasAny (padded, { "harder", "pumping", "banging", "intense", "energetic", "hype" }))
        intent.energyDelta += 0.15f;
    if (hasAny (padded, { "chill", "chiller", "softer", "mellow", "calm", "relaxed" }))
        intent.energyDelta -= 0.15f;
    if (hasAny (padded, { "busier", "busy", "denser", "fuller" }))
        intent.densityDelta += 0.15f;
    if (hasAny (padded, { "simpler", "sparse", "sparser", "minimal", "emptier" }))
        intent.densityDelta -= 0.15f;
    if (hasAny (padded, { "swing", "swung", "shuffle", "shuffled" }))
        intent.swingDelta += 0.10f;
    if (hasAny (padded, { "tight", "tighter", "quantize", "quantized", "robotic" }))
        intent.humanizeDelta -= 0.15f;
    if (hasAny (padded, { "loose", "looser", "human", "humanize", "sloppy" }))
        intent.humanizeDelta += 0.15f;
    if (hasWord (padded, "darker")   && intent.scale.empty()) intent.scale = "minor";
    if (hasWord (padded, "brighter") && intent.scale.empty()) intent.scale = "major";

    //==========================================================================
    // 7) Decide the action
    static const char* hardQuestion[] = { "what", "why", "how", "who", "when",
                                          "where", "which", "explain", "tell" };
    const bool startsHardQuestion = std::any_of (std::begin (hardQuestion), std::end (hardQuestion),
                                                 [&] (const char* w) { return toks[0] == w; });
    const bool hasQuestionMark = message.find ('?') != std::string::npos;

    // Advice-seeking always goes to the AI, even when a lane is named
    // ("give me feedback on the melody" must not regenerate the melody).
    const bool wantsOpinion = hasAny (padded, { "feedback", "thoughts", "opinion",
                                                "advice", "suggest", "suggestion",
                                                "suggestions", "recommend", "review" });
    if (startsHardQuestion || wantsOpinion)
    {
        intent.action = ChatAction::Conversation;
        return intent;
    }

    if (newIdea)
    {
        intent.action = ChatAction::NewIdea;
        return intent;
    }

    const bool hasTargets = ! intent.lanes.empty() || ! intent.pieces.empty();

    if (varyVerb && hasTargets)
    {
        intent.action = ChatAction::Vary;
        return intent;
    }

    if (genVerb || varyVerb)
    {
        if (hasTargets)
            intent.action = ChatAction::Generate;
        else if (allWords || ! intent.genre.empty())
            intent.action = ChatAction::GenerateAll;
        else if (intent.hasParamChange())
            intent.action = ChatAction::AdjustOnly;   // "make it faster"
        else if (! hasQuestionMark)
            intent.action = ChatAction::GenerateAll;  // bare "generate"
        else
            intent.action = ChatAction::Conversation;
        return intent;
    }

    if (intent.hasParamChange())
    {
        // "simpler hats" -> tweak the dial AND rebuild just the hats;
        // "128 bpm" / "f minor" / "deep house" -> param change only.
        intent.action = hasTargets ? ChatAction::Generate : ChatAction::AdjustOnly;
        return intent;
    }

    if (hasTargets && ! hasQuestionMark)
    {
        intent.action = ChatAction::Generate;         // terse "bassline" / "new hats"
        return intent;
    }

    intent.action = hasQuestionMark ? ChatAction::Conversation : ChatAction::None;
    return intent;
}

/** The help text the local director answers with (kept here so tests cover it). */
inline const char* chatDirectorHelpText()
{
    return "Instant commands (no AI round-trip):\n"
           "- \"make a bassline\" / \"new melody\" / \"regenerate drums\" - rebuild a lane\n"
           "- \"new kick\" / \"busier hats\" / \"more percussion\" - rebuild one drum piece\n"
           "- \"generate everything\" / \"new idea\" - fresh take on all unlocked lanes\n"
           "- \"vary the melody\" - variation, same vibe\n"
           "- \"128 bpm\" / \"faster\" / \"8 bars\" - tempo & length\n"
           "- \"in F minor\" / \"darker\" / \"brighter\" - key & mood\n"
           "- genre names (tech house, hip hop, trance...) - switch style\n"
           "- \"harder\" \"chill\" \"busier\" \"simpler\" \"swing\" \"tight\" \"loose\" - feel dials\n"
           "- \"undo\" - revert last generation\n"
           "Anything else goes to the AI with full project context.";
}

} // namespace aimidi
