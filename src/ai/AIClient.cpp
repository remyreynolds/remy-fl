#include "AIClient.h"
#include "../engine/StylePresets.h"
#include <juce_events/juce_events.h>

namespace aimidi
{

AIClient::AIClient()
{
    // Convenience: pick up a key from the environment for local dev.
    if (auto env = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});
        env.isNotEmpty())
        apiKey = env;
}

//==============================================================================
juce::String AIClient::buildSystemPrompt()
{
    // The engine's style presets are the ground truth — build the genre list
    // (with BPM + vibe descriptions) straight from them so prompt and engine
    // can never drift apart.
    juce::String styleList;
    for (const auto& st : allStyles())
        styleList << "  - \"" << st.name << "\" (" << (int) st.bpm << " bpm, swing "
                  << juce::String (st.swing, 2) << ", " << st.scale << "): "
                  << st.vibe << "\n";

    return
R"(You are the music-director brain of a MIDI generation plugin.
You DO NOT write MIDI notes. You translate a producer's request into a compact
JSON object of musical instructions that a separate deterministic engine renders.

Respond with ONLY a single JSON object, no prose, matching this schema:
{
  "reply": "<one short sentence to show the user>",
  "params": {
    "root": "F", "scale": "minor", "genre": "Tech House",
    "bpm": 126, "bars": 4, "octave": 4,
    "complexity": 0.5, "energy": 0.6, "swing": 0.15, "humanize": 0.3,
    "noteDensity": 0.5, "rhythmComplexity": 0.5, "chordComplexity": 0.5
  },
  "generate": ["Melody","Chords","Bass","Drums","Arp","Pad","Counter Melody"]
}
Rules:
- "genre" MUST be exactly one of the style names below. Map artist references,
  labels and vibe words to the closest style (e.g. "John Summit" -> Tech House,
  "Keinemusik" -> Afro House, "Anyma" -> Melodic House, "2-step" -> UK Garage):
)" + styleList + R"(- When switching style, default bpm/swing/scale to that style's values above
  unless the user explicitly asked for something else.
- "scale" must be one of: major, minor, dorian, phrygian, lydian, mixolydian,
  locrian, harmonicMinor, minorPentatonic, majorPentatonic.
- All 0..1 dials are floats. Only include instruments the user asked to change
  in "generate"; if they say "keep the chords", omit "Chords".
- Interpret production slang (harder/darker/bouncier/more tension) as dial moves.)";
}

juce::var AIClient::buildRequestBody (const juce::String& system,
                                      const juce::String& user,
                                      const juce::String& model)
{
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("role", "user");
    msg->setProperty ("content", user);

    juce::Array<juce::var> messages;
    messages.add (juce::var (msg));

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", model);
    body->setProperty ("max_tokens", 1024);
    body->setProperty ("system", system);
    body->setProperty ("messages", messages);
    return juce::var (body);
}

//==============================================================================
void AIClient::sendPrompt (const juce::String& userPrompt,
                           const MusicParams& current,
                           const std::vector<bool>& /*lockedMask*/,
                           Callback callback)
{
    if (! hasApiKey())
    {
        auto r = localFallback (userPrompt, current);
        juce::MessageManager::callAsync ([callback, r] { callback (r); });
        return;
    }

    const auto key   = apiKey;
    const auto mdl   = model;
    const auto fb    = current;

    // Give the model the current state so it can do relative edits.
    juce::String contextLine;
    contextLine << "Current: root=" << current.root << " scale=" << current.scale
                << " genre=" << current.genre << " bpm=" << (int) current.bpm
                << " bars=" << current.bars << ". Request: " << userPrompt;

    juce::Thread::launch ([key, mdl, fb, contextLine, callback]
    {
        Response resp;
        const auto bodyVar = buildRequestBody (buildSystemPrompt(), contextLine, mdl);
        const auto json    = juce::JSON::toString (bodyVar);

        juce::URL url ("https://api.anthropic.com/v1/messages");
        url = url.withPOSTData (json);

        juce::StringPairArray responseHeaders;
        int statusCode = 0;

        const juce::String headers =
            "x-api-key: " + key + "\r\n"
            "anthropic-version: 2023-06-01\r\n"
            "content-type: application/json\r\n";

        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (30000)
                           .withResponseHeaders (&responseHeaders)
                           .withStatusCode (&statusCode);

        if (auto stream = url.createInputStream (options))
        {
            const auto raw = stream->readEntireStreamAsString();
            if (statusCode >= 200 && statusCode < 300)
                resp = parseResponse (raw, fb);
            else
            {
                resp.ok = false;
                resp.error = "HTTP " + juce::String (statusCode) + ": " + raw;
                resp.params = fb;
            }
        }
        else
        {
            resp = localFallback (contextLine, fb);
            resp.error = "No network — used local fallback.";
        }

        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
    });
}

//==============================================================================
AIClient::Response AIClient::parseResponse (const juce::String& raw,
                                            const MusicParams& fallback)
{
    Response resp;
    resp.params = fallback;

    auto top = juce::JSON::parse (raw);
    // Anthropic returns { content: [ { type:"text", text:"..." } ], ... }
    juce::String text;
    if (auto* content = top.getProperty ("content", {}).getArray())
        for (auto& block : *content)
            if (block.getProperty ("type", {}).toString() == "text")
                text << block.getProperty ("text", {}).toString();

    auto inner = juce::JSON::parse (text);
    if (! inner.isObject())
    {
        resp.ok = false;
        resp.error = "Could not parse model JSON.";
        resp.assistantText = text.isNotEmpty() ? text : raw;
        return resp;
    }

    resp.assistantText = inner.getProperty ("reply", "Updated.").toString();

    if (auto p = inner.getProperty ("params", {}); p.isObject())
    {
        auto& mp = resp.params;
        auto getS = [&] (const char* k, const juce::String& d)
                    { auto v = p.getProperty (k, {}); return v.isVoid() ? d : v.toString(); };
        auto getF = [&] (const char* k, float d)
                    { auto v = p.getProperty (k, {}); return v.isVoid() ? d : (float) v; };
        auto getI = [&] (const char* k, int d)
                    { auto v = p.getProperty (k, {}); return v.isVoid() ? d : (int) v; };

        mp.root  = getS ("root",  mp.root).toStdString();
        mp.scale = getS ("scale", mp.scale).toStdString();
        mp.genre = getS ("genre", mp.genre).toStdString();
        mp.bpm   = getI ("bpm", (int) mp.bpm);
        mp.bars  = getI ("bars", mp.bars);
        mp.octave= getI ("octave", mp.octave);
        mp.complexity       = getF ("complexity", mp.complexity);
        mp.energy           = getF ("energy", mp.energy);
        mp.swing            = getF ("swing", mp.swing);
        mp.humanize         = getF ("humanize", mp.humanize);
        mp.noteDensity      = getF ("noteDensity", mp.noteDensity);
        mp.rhythmComplexity = getF ("rhythmComplexity", mp.rhythmComplexity);
        mp.chordComplexity  = getF ("chordComplexity", mp.chordComplexity);
    }

    if (auto* gen = inner.getProperty ("generate", {}).getArray())
    {
        for (auto& g : *gen)
        {
            const auto s = g.toString();
            for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
                if (s == toString ((InstrumentType) t))
                    resp.toGenerate.push_back ((InstrumentType) t);
        }
    }

    resp.ok = true;
    return resp;
}

//==============================================================================
AIClient::Response AIClient::localFallback (const juce::String& userPrompt,
                                            const MusicParams& current)
{
    Response resp;
    resp.ok = true;
    resp.params = current;
    auto& p = resp.params;
    const auto lower = userPrompt.toLowerCase();

    // Style detection first: match against the preset keyword lists
    // ("afro", "keinemusik", "garage", "melodic", …) and adopt that style's
    // groove defaults, exactly like the online AI would.
    if (const auto* st = findStyleOrNull (lower.toStdString()))
    {
        p.genre = st->name;
        p.bpm   = st->bpm;
        p.swing = st->swing;
        p.scale = st->scale;
    }

    if (lower.contains ("dark"))    { p.scale = "phrygian"; p.energy = 0.4f; }
    if (lower.contains ("happy") || lower.contains ("uplift")) p.scale = "major";
    if (lower.contains ("harder") || lower.contains ("aggress")) p.energy = juce::jmin (1.0f, p.energy + 0.25f);
    if (lower.contains ("bounce") || lower.contains ("bouncy"))  p.swing  = juce::jmin (0.6f, p.swing + 0.2f);
    if (lower.contains ("simple")) p.complexity = juce::jmax (0.0f, p.complexity - 0.3f);
    if (lower.contains ("complex")) p.complexity = juce::jmin (1.0f, p.complexity + 0.3f);

    // crude BPM parse: "124 bpm"
    auto bpmIdx = lower.indexOf ("bpm");
    if (bpmIdx > 0)
    {
        auto num = lower.substring (juce::jmax (0, bpmIdx - 5), bpmIdx).retainCharacters ("0123456789");
        if (num.isNotEmpty()) p.bpm = juce::jlimit (60, 200, num.getIntValue());
    }

    // figure out which instrument(s)
    struct KW { const char* k; InstrumentType t; };
    const KW kws[] = { {"melod", InstrumentType::Melody}, {"chord", InstrumentType::Chords},
                       {"bass", InstrumentType::Bass}, {"808", InstrumentType::Bass},
                       {"drum", InstrumentType::Drums}, {"arp", InstrumentType::Arp},
                       {"pad", InstrumentType::Pad} };
    for (auto& kw : kws)
        if (lower.contains (kw.k)) resp.toGenerate.push_back (kw.t);

    if (resp.toGenerate.empty())
        resp.toGenerate = { InstrumentType::Chords, InstrumentType::Bass,
                            InstrumentType::Melody, InstrumentType::Drums };

    resp.assistantText = "(offline) Applied a local interpretation. Add an API key for full AI.";
    return resp;
}

} // namespace aimidi
