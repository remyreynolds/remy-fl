#include "AIClient.h"
#include "../engine/PreviewSounds.h" // GenreMode + detectGenreFromText: gate house-Brain-PDF routing
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <algorithm>

namespace aimidi
{

namespace
{
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "AIMidiGen";
    o.filenameSuffix      = "settings";
    o.folderName          = "AIMidiGen";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers    = false;
    return o;
}

bool containsAny (const juce::String& hay, std::initializer_list<const char*> needles)
{
    for (auto* n : needles)
        if (hay.contains (n))
            return true;
    return false;
}
} // namespace

AIClient::AIClient()
{
    juce::PropertiesFile props (settingsOptions());
    const auto savedProvider = props.getValue ("aiProvider", "claude").toLowerCase();
    provider = (savedProvider == "openai") ? Provider::OpenAI : Provider::Claude;

    if (auto env = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});
        env.isNotEmpty())
    {
        anthropicApiKey = sanitizeApiKey (env);
        if (provider == Provider::Claude)
            loadedFromEnv = true;
    }
    else
    {
        anthropicApiKey = sanitizeApiKey (props.getValue ("anthropicApiKey"));
    }

    if (auto env = juce::SystemStats::getEnvironmentVariable ("OPENAI_API_KEY", {});
        env.isNotEmpty())
    {
        openAiApiKey = sanitizeApiKey (env);
        if (provider == Provider::OpenAI)
            loadedFromEnv = true;
    }
    else
    {
        openAiApiKey = sanitizeApiKey (props.getValue ("openAiApiKey"));
    }

    claudeModel = props.getValue ("claudeModel", "claude-sonnet-4-5");
    // Migrate saved settings that still point at old invalid model ids.
    if (claudeModel == "claude-sonnet-5" || claudeModel == "claude-opus-4-8"
        || claudeModel == "claude-sonnet-4-6")
        claudeModel = "claude-sonnet-4-5";
    openAiModel = props.getValue ("openAiModel", "gpt-4o");

    // PDF knowledge store (Appendix E heading chunks) — load before any generation.
    (void) houseBrain.loadFromDefaultLocations();
}

AIClient::~AIClient()
{
    // Detached workers / queued async lambdas check this before touching us.
    alive->store (false);
}

void AIClient::setProvider (Provider p)
{
    provider = p;
    loadedFromEnv = false;
    if (provider == Provider::Claude)
    {
        if (auto env = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});
            env.isNotEmpty())
        {
            anthropicApiKey = sanitizeApiKey (env);
            loadedFromEnv = true;
        }
    }
    else if (auto env = juce::SystemStats::getEnvironmentVariable ("OPENAI_API_KEY", {});
             env.isNotEmpty())
    {
        openAiApiKey = sanitizeApiKey (env);
        loadedFromEnv = true;
    }

    juce::PropertiesFile props (settingsOptions());
    props.setValue ("aiProvider", provider == Provider::OpenAI ? "openai" : "claude");
    props.saveIfNeeded();
}

void AIClient::setApiKey (const juce::String& key)
{
    loadedFromEnv = false;
    juce::PropertiesFile props (settingsOptions());

    // Trim whitespace / CR / LF before storing or using — a pasted key with
    // trailing newlines would otherwise inject extra HTTP header lines.
    const auto cleaned = sanitizeApiKey (key);

    if (provider == Provider::OpenAI)
    {
        openAiApiKey = cleaned;
        props.setValue ("openAiApiKey", cleaned);
    }
    else
    {
        anthropicApiKey = cleaned;
        props.setValue ("anthropicApiKey", cleaned);
    }

    props.saveIfNeeded();
}

bool AIClient::hasApiKey() const
{
    return activeApiKey().isNotEmpty();
}

juce::String AIClient::activeApiKey() const
{
    return provider == Provider::OpenAI ? openAiApiKey : anthropicApiKey;
}

juce::String AIClient::activeModel() const
{
    return provider == Provider::OpenAI ? openAiModel : claudeModel;
}

juce::String AIClient::apiKeyPlaceholder() const
{
    return provider == Provider::OpenAI ? "sk-…" : "sk-ant-…";
}

juce::String AIClient::providerDisplayName() const
{
    return provider == Provider::OpenAI ? "OpenAI" : "Claude";
}

void AIClient::setClaudeModel (const juce::String& modelId)
{
    const auto id = modelId.trim();
    if (id.isEmpty()) return;
    claudeModel = id;
    juce::PropertiesFile props (settingsOptions());
    props.setValue ("claudeModel", claudeModel);
    props.saveIfNeeded();
}

void AIClient::setOpenAiModel (const juce::String& modelId)
{
    const auto id = modelId.trim();
    if (id.isEmpty()) return;
    openAiModel = id;
    juce::PropertiesFile props (settingsOptions());
    props.setValue ("openAiModel", openAiModel);
    props.saveIfNeeded();
}

//==============================================================================
bool AIClient::looksLikeMidiGenerateRequest (const juce::String& prompt)
{
    const auto p = prompt.toLowerCase().trim();
    if (p.isEmpty())
        return false;

    // Attached MIDI context from the chat "Use MIDI" button.
    if (p.contains ("[attached midi context]")
        && containsAny (p, {
            "vary", "continue", "extend", "remix", "morph", "develop",
            "iterate", "tweak", "change", "rewrite", "redo", "make",
            "generate", "compose", "create", "write", "build", "produce"
        }))
        return true;

    const bool hasGenerateVerb = containsAny (p, {
        "generate", "compose", "regenerate",
        "make me", "make a", "make an", "make the", "make some", "make it",
        "create me", "create a", "create an", "create the", "create some",
        "write me", "write a", "write an", "write the",
        "build me", "build a", "build an",
        "give me a", "give me an", "give me some",
        "produce a", "produce an", "produce some",
        "drop a", "drop me",
        "another progression", "another midi", "new midi", "new progression",
        "redo the", "redo it",
        "vary", "variation", "continue this", "continue the", "extend this",
        "extend the", "remix", "morph", "develop this", "iterate",
        "tweak this", "tweak the", "rewrite this", "rewrite the"
    })
    || p.startsWith ("make ")
    || p.startsWith ("create ")
    || p.startsWith ("write ")
    || p.startsWith ("compose ")
    || p.startsWith ("generate ")
    || p.startsWith ("vary ")
    || p.startsWith ("continue ")
    || p.startsWith ("extend ")
    || p.startsWith ("remix ");

    // Pure questions stay conversational unless they also ask to make MIDI.
    const bool looksLikeQuestion =
        p.startsWith ("what ") || p.startsWith ("why ") || p.startsWith ("how ")
        || p.startsWith ("which ") || p.startsWith ("when ") || p.startsWith ("where ")
        || p.startsWith ("who ") || p.startsWith ("is ") || p.startsWith ("are ")
        || p.startsWith ("does ") || p.startsWith ("do ") || p.startsWith ("should ")
        || p.startsWith ("could you explain") || p.startsWith ("can you explain")
        || p.startsWith ("explain ") || p.startsWith ("tell me about")
        || p.startsWith ("tell me what") || p.startsWith ("help me understand")
        || p.startsWith ("what's ") || p.startsWith ("whats ");

    if (looksLikeQuestion && ! hasGenerateVerb)
        return false;

    return hasGenerateVerb;
}

juce::String AIClient::buildChatSystemPrompt()
{
    return
R"(You are **Groovewright**, the multi-genre MIDI production agent inside AI MIDI Gen
(house, techno, hip-hop/trap, pop, classical, and anything else the user names).

Obey the MASTER SYSTEM PROMPT (THE BRAIN) and any MUSIC THEORY REFERENCES from
the local brain knowledge folder (same corpus the MIDI generator uses).

Behavior:
- Conversational by default — short, producer-to-producer (under ~4 sentences).
- Prefer local brain docs when they cover the topic.
- You MAY also use your Claude/model knowledge to fill gaps, explain deeper
  theory, or compare approaches — clearly prefer brain docs when they conflict.
- Do NOT output MIDI JSON unless the user clearly asks to make/generate/compose/vary/continue/extend/remix MIDI.
- Groove over theory, but groove authentic to the requested genre — house habits
  (four-on-the-floor, deep/tech/French/prog/afro/garage/piano house palettes) apply
  ONLY when the genre is house; otherwise use that genre's real conventions.
- When they are ready to generate, suggest a concrete prompt (key + BPM + bars +
  roles). Prefer full loops (chords+bass+melody+drums) unless they ask for one part.
- Cite style influences briefly when relevant.)";
}

juce::String AIClient::buildLegacySystemPrompt()
{
    return
R"(You are the music-director brain of a MIDI generation plugin.
You DO NOT write MIDI notes. You translate a producer's request into a compact
JSON object of musical instructions that a separate deterministic engine renders.

Respond with ONLY a single JSON object, no prose, matching this schema:
{
  "reply": "<one short sentence to show the user>",
  "params": {
    "root": "F", "scale": "minor", "genre": "House",
    "bpm": 124, "bars": 4, "octave": 4,
    "complexity": 0.5, "energy": 0.6, "swing": 0.15, "humanize": 0.3,
    "noteDensity": 0.5, "rhythmComplexity": 0.5, "chordComplexity": 0.5
  },
  "generate": ["Melody","Chords","Bass","Drums","Arp","Pad","Counter Melody"]
}
Rules:
- "scale" must be one of: major, minor, dorian, phrygian, lydian, mixolydian,
  locrian, harmonicMinor, minorPentatonic, majorPentatonic.
- All 0..1 dials are floats. Only include instruments the user asked to change
  in "generate"; if they say "keep the chords", omit "Chords".
- Interpret production slang (harder/darker/bouncier/more tension) as dial moves.)";
}

juce::var AIClient::buildRequestBody (const juce::String& system,
                                      const juce::String& user,
                                      const juce::String& model,
                                      int maxTokens,
                                      double temperature)
{
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("role", "user");
    msg->setProperty ("content", user);

    juce::Array<juce::var> messages;
    messages.add (juce::var (msg));

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", model);
    body->setProperty ("max_tokens", maxTokens);
    body->setProperty ("system", system);
    body->setProperty ("messages", messages);
    if (temperature >= 0.0)
        body->setProperty ("temperature", temperature);
    return juce::var (body);
}

juce::var AIClient::buildChatMessagesBody (const juce::String& system,
                                           const juce::String& latestUserWithRefs) const
{
    juce::Array<juce::var> messages;

    // Prior turns (without re-injecting large reference blocks)
    constexpr int kMaxHistory = 8;
    const int start = juce::jmax (0, (int) history.size() - kMaxHistory);
    for (int i = start; i < (int) history.size(); ++i)
    {
        auto* msg = new juce::DynamicObject();
        msg->setProperty ("role", history[(size_t) i].fromUser ? "user" : "assistant");
        msg->setProperty ("content", history[(size_t) i].text);
        messages.add (juce::var (msg));
    }

    auto* latest = new juce::DynamicObject();
    latest->setProperty ("role", "user");
    latest->setProperty ("content", latestUserWithRefs);
    messages.add (juce::var (latest));

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", activeModel());
    body->setProperty ("max_tokens", 1024);
    body->setProperty ("system", system);
    body->setProperty ("messages", messages);
    return juce::var (body);
}

juce::var AIClient::buildOpenAiRequestBody (const juce::String& system,
                                            const juce::String& user,
                                            const juce::String& model,
                                            int maxTokens)
{
    juce::Array<juce::var> messages;

    auto* sys = new juce::DynamicObject();
    sys->setProperty ("role", "system");
    sys->setProperty ("content", system);
    messages.add (juce::var (sys));

    auto* usr = new juce::DynamicObject();
    usr->setProperty ("role", "user");
    usr->setProperty ("content", user);
    messages.add (juce::var (usr));

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", model);
    body->setProperty ("max_tokens", maxTokens);
    body->setProperty ("messages", messages);
    body->setProperty ("temperature", 0.7);
    return juce::var (body);
}

void AIClient::pushHistory (bool fromUser, const juce::String& text)
{
    history.push_back ({ fromUser, text });
    constexpr size_t kCap = 16;
    if (history.size() > kCap)
        history.erase (history.begin(), history.begin() + (int) (history.size() - kCap));
}

juce::String AIClient::extractTextContent (const juce::String& anthropicRaw)
{
    auto top = juce::JSON::parse (anthropicRaw);
    juce::String text;
    if (auto* content = top.getProperty ("content", {}).getArray())
        for (auto& block : *content)
            if (block.getProperty ("type", {}).toString() == "text")
                text << block.getProperty ("text", {}).toString();
    return text.trim();
}

juce::String AIClient::extractOpenAiTextContent (const juce::String& openAiRaw)
{
    auto top = juce::JSON::parse (openAiRaw);
    if (auto* choices = top.getProperty ("choices", {}).getArray();
        choices != nullptr && ! choices->isEmpty())
    {
        auto msg = choices->getReference (0).getProperty ("message", {});
        return msg.getProperty ("content", {}).toString().trim();
    }
    return {};
}

AIClient::Endpoint AIClient::endpointSnapshot() const
{
    return { provider, activeApiKey(), activeModel(), providerDisplayName() };
}

AIClient::LlmHttpResult AIClient::postChatCompletion (const Endpoint& ep,
                                                      const juce::String& system,
                                                      const juce::String& user,
                                                      int maxTokens,
                                                      double temperature)
{
    const auto json = ep.provider == Provider::OpenAI
        ? juce::JSON::toString (buildOpenAiRequestBody (system, user, ep.model, maxTokens))
        : juce::JSON::toString (buildRequestBody (system, user, ep.model, maxTokens, temperature));
    return postLlmJson (ep, json);
}

AIClient::LlmHttpResult AIClient::postLlmJson (const Endpoint& ep, const juce::String& json)
{
    LlmHttpResult result;
    const auto& key = ep.key;

    if (key.isEmpty())
    {
        result.error = "Add your " + ep.displayName + " API key first (header field).";
        return result;
    }

    juce::URL url;
    juce::String headers;

    if (ep.provider == Provider::OpenAI)
    {
        url = juce::URL ("https://api.openai.com/v1/chat/completions").withPOSTData (json);
        headers = "Authorization: Bearer " + key + "\r\n"
                  "content-type: application/json\r\n";
    }
    else
    {
        url = juce::URL ("https://api.anthropic.com/v1/messages").withPOSTData (json);
        headers = "x-api-key: " + key + "\r\n"
                  "anthropic-version: 2023-06-01\r\n"
                  "content-type: application/json\r\n";
    }

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        juce::StringPairArray responseHeaders;
        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (60000)
                           .withResponseHeaders (&responseHeaders)
                           .withStatusCode (&statusCode);

        auto stream = url.createInputStream (options);
        if (stream == nullptr)
        {
            result.error = ep.provider == Provider::OpenAI
                               ? "Network error: could not reach api.openai.com."
                               : "Network error: could not reach api.anthropic.com.";
            return result;
        }

        const auto raw = stream->readEntireStreamAsString();
        result.statusCode = statusCode;

        // ONE retry on rate-limit / server errors, honouring Retry-After
        // (default 2s backoff, capped so a hostile header can't stall us).
        if (attempt == 0 && (statusCode == 429 || statusCode >= 500))
        {
            auto retryAfter = responseHeaders.getValue ("Retry-After", {});
            if (retryAfter.isEmpty())
                retryAfter = responseHeaders.getValue ("retry-after", {});
            int waitSeconds = retryAfter.getIntValue();
            if (waitSeconds <= 0)
                waitSeconds = 2;
            juce::Thread::sleep (juce::jlimit (1, 30, waitSeconds) * 1000);
            continue;
        }

        if (statusCode < 200 || statusCode >= 300)
        {
            result.error = describeHttpError (statusCode, raw);
            return result;
        }

        result.text = ep.provider == Provider::OpenAI
                          ? extractOpenAiTextContent (raw)
                          : extractTextContent (raw);
        if (result.text.isEmpty())
        {
            result.error = ep.displayName + " returned an empty response.";
            return result;
        }

        // Detect a truncated reply (hit the token cap) — a half-written MIDI
        // JSON would otherwise surface as a confusing "could not parse" error.
        {
            const auto top = juce::JSON::parse (raw);
            juce::String stopReason;
            if (ep.provider == Provider::OpenAI)
            {
                if (auto* choices = top.getProperty ("choices", {}).getArray();
                    choices != nullptr && ! choices->isEmpty())
                    stopReason = choices->getReference (0)
                                     .getProperty ("finish_reason", {}).toString();
                if (stopReason == "length") stopReason = "max_tokens";
            }
            else
            {
                stopReason = top.getProperty ("stop_reason", {}).toString();
            }

            if (stopReason == "max_tokens")
            {
                result.error = ep.displayName + " reply was cut off at the token limit. "
                               "Try fewer bars or fewer parts.";
                return result;
            }
        }

        result.ok = true;
        return result;
    }

    return result;
}

juce::String AIClient::describeHttpError (int statusCode, const juce::String& body)
{
    juce::String detail;
    if (auto parsed = juce::JSON::parse (body); parsed.isObject())
    {
        auto err = parsed.getProperty ("error", {});
        if (err.isObject())
            detail = err.getProperty ("message", {}).toString();
    }
    if (detail.isEmpty())
        detail = body.substring (0, 240);

    // Provider-neutral wording: this path serves both Claude and OpenAI.
    if (statusCode == 401 || statusCode == 403)
        return "Invalid or unauthorized API key. Check the key in the header field.";
    if (statusCode == 429)
        return "Rate limited by the AI provider. Wait a moment and try again.";
    if (statusCode == 400)
        return "Bad request to the model" + (detail.isNotEmpty() ? (": " + detail) : ".");
    if (statusCode >= 500)
        return "AI provider server error (HTTP " + juce::String (statusCode) + "). Try again.";

    return "HTTP " + juce::String (statusCode)
         + (detail.isNotEmpty() ? (": " + detail) : ".");
}

//==============================================================================
void AIClient::handleUserTurn (const juce::String& userPrompt,
                               const juce::String& forcedKey,
                               TurnCallback callback)
{
    const auto prompt = userPrompt.trim();
    if (prompt.isEmpty())
    {
        TurnResponse resp;
        resp.ok = false;
        resp.error = "Type a message first.";
        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
        return;
    }

    if (looksLikeMidiGenerateRequest (prompt))
    {
        requestMidiPattern (prompt, forcedKey,
            [this, alive = alive, prompt, callback] (PatternResponse r)
            {
                if (! alive->load())
                    return; // AIClient destroyed while the request was in flight

                if (r.ok)
                {
                    pushHistory (true, prompt);
                    pushHistory (false, r.assistantText);
                }

                TurnResponse t;
                t.ok = r.ok;
                t.generatedMidi = r.ok;
                t.assistantText = r.assistantText;
                t.error = r.error;
                t.pattern = std::move (r.pattern);
                t.matchedDocTitles = r.matchedDocTitles;
                callback (std::move (t));
            });
        return;
    }

    sendChatMessage (prompt,
        [this, alive = alive, prompt, callback] (ChatResponse r)
        {
            if (! alive->load())
                return; // AIClient destroyed while the request was in flight

            if (r.ok)
            {
                pushHistory (true, prompt);
                pushHistory (false, r.assistantText);
            }

            TurnResponse t;
            t.ok = r.ok;
            t.generatedMidi = false;
            t.assistantText = r.assistantText;
            t.error = r.error;
            t.matchedDocTitles = r.matchedDocTitles;
            callback (std::move (t));
        });
}

//==============================================================================
void AIClient::testConnection (std::function<void (bool, juce::String)> done)
{
    const auto ep = endpointSnapshot();
    if (ep.key.isEmpty())
    {
        done (false, "No API key yet — paste your " + ep.displayName + " key first");
        return;
    }

    auto aliveFlag = alive;
    juce::Thread::launch ([ep, aliveFlag, done]
    {
        const auto res = postChatCompletion (ep, "Reply with the single word: ok",
                                             "ping", 8);
        if (! aliveFlag->load()) return;

        const bool ok = res.ok;
        juce::String detail;
        if (ok)
            detail = "Connected — " + ep.model + " is ready";
        else if (res.statusCode == 401 || res.statusCode == 403)
            detail = "Key rejected — check it was pasted completely "
                     "(Claude keys start with sk-ant-)";
        else if (res.statusCode == 429)
            detail = "Key works, but the account is rate-limited right now — "
                     "try again in a minute";
        else if (res.statusCode == 0)
            detail = "Could not reach " + ep.displayName
                     + " — check your internet connection";
        else
            detail = res.error.isNotEmpty()
                         ? res.error
                         : ("Request failed (HTTP " + juce::String (res.statusCode) + ")");

        juce::MessageManager::callAsync ([aliveFlag, done, ok, detail]
        {
            if (aliveFlag->load())
                done (ok, detail);
        });
    });
}

//==============================================================================
void AIClient::sendChatMessage (const juce::String& userPrompt, ChatCallback callback)
{
    if (! hasApiKey())
    {
        ChatResponse resp;
        resp.ok = false;
        resp.error = "Add your " + providerDisplayName() + " API key first (header field).";
        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
        return;
    }

    const auto prompt = userPrompt.trim();
    auto retrieval = knowledgeBase.retrieveForQuery (prompt);
    const auto system = buildChatSystemPrompt();

    juce::String user = prompt;
    if (projectContext.isNotEmpty())
    {
        user << "\n\n===== PROJECT STATE (live plugin snapshot) =====\n"
             << projectContext
             << "\n===== END PROJECT STATE =====\n"
             << "Ground every answer in this snapshot — refer to the actual "
                "style, key, BPM, and lanes above instead of guessing.";
    }
    if (retrieval.context.isNotEmpty())
    {
        user << "\n\n===== MUSIC THEORY REFERENCES (shared brain knowledge) =====\n"
             << retrieval.context
             << "\n===== END REFERENCES =====\n"
             << "Prefer these local brain docs when relevant. You may also use your "
                "Claude knowledge to expand or clarify. Do not generate MIDI JSON "
                "unless the user asked to generate.";
    }

    // For Claude, keep multi-turn history via Anthropic messages body.
    // For OpenAI, fold history into a single user blob for simplicity.
    juce::String finalUser = user;
    if (provider == Provider::OpenAI && ! history.empty())
    {
        juce::String hist;
        constexpr int kMaxHistory = 6;
        const int start = juce::jmax (0, (int) history.size() - kMaxHistory);
        for (int i = start; i < (int) history.size(); ++i)
            hist << (history[(size_t) i].fromUser ? "User: " : "Assistant: ")
                 << history[(size_t) i].text << "\n";
        finalUser = hist + "User: " + user;
    }

    const auto matched = retrieval.matchedDocs;
    const auto useClaudeHistory = provider == Provider::Claude;
    const auto bodyVar = useClaudeHistory ? buildChatMessagesBody (system, user)
                                          : juce::var();
    const auto ep = endpointSnapshot(); // by value: worker must not read live members

    juce::Thread::launch ([system, finalUser, user, useClaudeHistory, bodyVar, matched, ep, callback]
    {
        ChatResponse resp;
        resp.matchedDocTitles = matched;

        // Same transport for both paths: 60s timeout, one retry on 429/5xx,
        // truncation detection — the history body only changes the payload.
        const auto http = useClaudeHistory
                              ? postLlmJson (ep, juce::JSON::toString (bodyVar))
                              : postChatCompletion (ep, system, finalUser, 1024);

        if (! http.ok)
        {
            resp.ok = false;
            resp.error = http.error;
            juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
            return;
        }

        resp.ok = true;
        resp.assistantText = http.text;
        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
    });
}

//==============================================================================
void AIClient::requestMidiPattern (const juce::String& userPrompt,
                                   const juce::String& forcedKey,
                                   PatternCallback callback)
{
    if (! hasApiKey())
    {
        PatternResponse resp;
        resp.ok = false;
        resp.error = "Add your " + providerDisplayName() + " API key first (header field).";
        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
        return;
    }

    if (userPrompt.trim().isEmpty())
    {
        PatternResponse resp;
        resp.ok = false;
        resp.error = "Enter a music request in chat first.";
        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
        return;
    }

    const auto prompt = userPrompt.trim();
    const auto lockKey = forcedKey.trim();

    // Genre gate: the bundled Brain PDF (BrainCorpus/BrainValidator) is a house-only
    // corpus — every archetype BrainCorpus::selectArchetype() can return
    // (minimal_deep_tech_pocket, melody_first_progressive_house, high_impact_loop_pressure,
    // rave_house_lift, pop_edm_clarity) is a house/EDM sub-style, and it falls back to a
    // house archetype when nothing matches. Route generation through it only when the
    // request is actually house (or genre-ambiguous, preserving the historical default);
    // for an explicit non-house genre (hip-hop/trap, techno, pop, classical...) skip PDF
    // routing entirely so we don't force house conventions onto it — see the DEVLOG
    // "genre/tempo-sync bugs" audit entry this guards against regressing.
    GenreMode detectedGenre = GenreMode::House;
    const bool genreExplicit = detectGenreFromText (prompt + " " + projectContext, detectedGenre);
    const bool useHouseBrain = ! genreExplicit || detectedGenre == GenreMode::House;

    // PDF-first retrieval (Part I shared + one Part II archetype + Part III rules) —
    // only meaningful when we're actually generating house material.
    if (useHouseBrain && ! houseBrain.isLoaded())
        (void) houseBrain.loadFromDefaultLocations();
    const auto brainRoute = useHouseBrain ? houseBrain.retrieveForGeneration (prompt, 16000)
                                          : BrainCorpus::RouteResult {};
    const auto brainCtx = brainRoute.context;
    const auto brainArch = brainRoute.primaryArchetype;
    const auto brainTitles = brainRoute.matchedTitles;
    const int brainChunks = brainRoute.chunksUsed;
    const auto brainRules = useHouseBrain ? houseBrain.hardRules() : BrainCorpus::HardRules {};

    // MIDI path: bias groove docs; skip the master Brain doc via includeMasterDoc=false —
    // it is injected into the system prompt below (masterBrain), so it must not appear
    // twice per request. Budget shrinks when the PDF is also competing for prompt space.
    auto retrieval = knowledgeBase.retrieveForQuery (prompt, useHouseBrain ? 8000 : 14000, true, false);
    const auto knowledgeCtx = retrieval.context;
    const auto matchedDocs = retrieval.matchedDocs;
    const int docsUsed = matchedDocs.size();
    const auto projCtx = projectContext; // copy for thread safety
    const auto recentProgressions = recentProgressionsForPrompt();
    juce::StringArray recentFpList;
    {
        const juce::ScopedLock sl (progressionHistoryLock);
        for (auto& fp : recentChordProgressions)
            recentFpList.add (fp);
    }
    const auto variationNonce = juce::Uuid().toString();
    const auto ep = endpointSnapshot();               // by value for the worker
    const auto masterBrain = knowledgeBase.masterPromptText(); // read on message thread

    juce::Thread::launch ([this, alive = alive, prompt, lockKey, useHouseBrain, brainCtx, brainArch,
                           brainTitles, brainChunks, brainRules, knowledgeCtx, projCtx, matchedDocs,
                           docsUsed, recentProgressions, recentFpList, variationNonce, ep, masterBrain,
                           callback]
    {
        if (! alive->load())
            return; // AIClient destroyed before the worker started

        PatternResponse resp;
        resp.knowledgeDocsUsed = docsUsed;
        resp.matchedDocTitles = matchedDocs;
        resp.primaryArchetype = brainArch;
        resp.brainChunksUsed = brainChunks;
        for (auto& t : brainTitles)
            if (! resp.matchedDocTitles.contains (t))
                resp.matchedDocTitles.add (t);

        juce::String system;

        // 1) PDF corpus sits ABOVE all other knowledge (user requirement) — house only.
        if (useHouseBrain)
        {
            if (brainCtx.isNotEmpty())
            {
                system << "===== HOUSE MUSIC MIDI GENERATOR BRAIN (PDF — TOP PRIORITY) =====\n"
                       << brainCtx
                       << "\n===== END HOUSE BRAIN PDF =====\n\n"
                       << "INSTRUCTION: Generate using the parameter cards, progression libraries "
                          "(Appendix A), drum pattern libraries (Appendix B), velocity bands, swing "
                          "ranges, and generation recipes from the provided document. Where the "
                          "document specifies a value or range, it overrides your general knowledge.\n\n";
            }
            else
            {
                system << "HOUSE BRAIN PDF corpus was unavailable. Still prefer house-brain docs "
                          "over generic music knowledge when present below.\n\n";
            }
        }

        if (masterBrain.trim().isNotEmpty())
        {
            system << "===== MASTER SYSTEM PROMPT (bundled Brain) =====\n"
                   << masterBrain.trim()
                   << "\n===== END MASTER SYSTEM PROMPT =====\n\n";
        }
        system << buildClaudeMidiSystemPrompt();

        if (useHouseBrain)
        {
            system << "\n\nWorkflow before writing notes:\n"
                      "1) Identify the musical style/genre the user wants and map it to ONE primary "
                      "archetype from the PDF (Appendix C). Hybrids: one primary, max two borrowed "
                      "dimensions — never average identities.\n"
                      "2) Use the HOUSE BRAIN PDF sections retrieved above first (Part I theory for "
                      "requested layers, Part II primary archetype, Part III pipeline/constraints).\n"
                      "3) Secondary: shared brain MUSIC THEORY REFERENCES below — only to fill gaps "
                      "that do not contradict the PDF.\n"
                      "4) Where the PDF specifies a value or range, it OVERRIDES general knowledge.\n"
                      "5) Decide single-part vs full-loop: if the user wants a loop/"
                      "groove/track/arrangement or multiple roles, return parts[] "
                      "with chords+bass+melody+drums (add arp/pad if asked). "
                      "If they ask for one role only, use the single-part schema.\n"
                      "6) Obey generation order/dependency (PDF ch.6): harmony before bass before "
                      "optional drums before melody before arrangement mutations.\n"
                      "7) Treat the user's text as the composition brief.\n"
                      "8) Include \"progression\" + \"chords\" metadata for chord work; include "
                      "archetype id and similarity notes when relevant.\n"
                      "9) Return ONLY the MIDI JSON schema — no prose.";
        }
        else
        {
            system << "\n\nWorkflow before writing notes:\n"
                      "1) Identify the musical style/genre the user wants.\n"
                      "2) Use the shared brain MUSIC THEORY REFERENCES (same docs as "
                      "the Python generator) — prefer matching titles/content.\n"
                      "3) You may also apply Claude music knowledge when the local "
                      "docs are incomplete, without contradicting those docs.\n"
                      "4) Decide single-part vs full-loop: if the user wants a loop/"
                      "groove/track/arrangement or multiple roles, return parts[] "
                      "with chords+bass+melody+drums (add arp/pad if asked). "
                      "If they ask for one role only, use the single-part schema.\n"
                      "5) Apply key, BPM, bars, and instrument request.\n"
                      "6) Treat the user's text as the composition brief: translate its mood, "
                      "imagery, energy, era, and adjectives into deliberate harmony, voicing, "
                      "rhythm, register, tension, and note density. Do not fall back to a house "
                      "template when the text gives creative direction.\n"
                      "7) Include explicit \"progression\" + \"chords\" metadata for any chord work.\n"
                      "8) Return ONLY the MIDI JSON schema — no prose.";
        }

        if (lockKey.isNotEmpty())
        {
            system << "\n\nHARD CONSTRAINT — PROJECT KEY:\n"
                      "The plugin key setting is \"" << lockKey << "\".\n"
                      "You MUST compose entirely in this key. Every pitch must fit the key. "
                      "Set the JSON \"key\" field exactly to \"" << lockKey << "\". "
                      "Ignore any other key mentioned in the user request.";
        }

        juce::String user = prompt;
        user << "\n\n[Variation nonce: " << variationNonce
             << ". This is a new composition request. Make materially new musical choices; "
                "do not reuse your default progression.]";
        if (brainArch.isNotEmpty())
            user << "\n[Primary archetype lock: " << brainArch << "]";
        if (recentProgressions.isNotEmpty())
            user << "\n\n===== RECENT CHORD PROGRESSIONS TO AVOID =====\n"
                 << recentProgressions
                 << "\n===== END RECENT PROGRESSIONS =====\n"
                    "Choose a different harmonic route, chord rhythm, extensions, and voicing contour.";
        if (lockKey.isNotEmpty())
            user << "\n\n[Project key lock: " << lockKey
                 << " — generate ONLY in this key.]";

        if (projCtx.isNotEmpty())
        {
            user << "\n\n===== PROJECT STATE (live plugin snapshot) =====\n"
                 << projCtx
                 << "\n===== END PROJECT STATE =====\n"
                 << "Compose to complement what is already loaded above.";
        }

        // Secondary knowledge (non-PDF guides) — below PDF in priority when the PDF
        // is in play; otherwise this is simply the primary shared-brain reference set.
        if (knowledgeCtx.isNotEmpty())
        {
            if (useHouseBrain)
            {
                user << "\n\n===== MUSIC THEORY REFERENCES (secondary — fill gaps only) =====\n"
                     << knowledgeCtx
                     << "\n===== END REFERENCES =====\n"
                     << "Do not contradict the HOUSE BRAIN PDF. Prefer PDF parameter cards and recipes.";
            }
            else
            {
                user << "\n\n===== MUSIC THEORY REFERENCES (shared brain knowledge) =====\n"
                     << knowledgeCtx
                     << "\n===== END REFERENCES =====\n"
                     << "These are the same brain guides the generator uses. Prefer them, "
                        "then fill gaps with Claude knowledge. Compose MIDI that follows "
                        "those rules and the project key/BPM/bars.";
            }
        }

        // Multi-attempt generation with Brain-hard-rule validation (originality,
        // density budgets, bass monophony, pipeline order). `rules` is the real PDF
        // rule set when useHouseBrain, otherwise sane genre-neutral defaults — the
        // validator itself is not house-specific, only the rule values can be.
        BrainValidator validator (brainRules);
        constexpr int kMaxAttempts = 3;
        MidiPatternParseResult parsed;
        parsed.ok = false;
        juce::String lastError;
        double bestScore = -1.0e9;
        MidiPattern bestPattern;
        bool haveBest = false;

        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            juce::String attemptUser = user;
            if (attempt > 0)
            {
                attemptUser << "\n\nREJECTED CANDIDATE (attempt " << juce::String (attempt + 1)
                            << "/" << juce::String (kMaxAttempts) << "): "
                            << lastError
                            << "\nRegenerate with a materially different musical solution that "
                               "obeys the hard rules (monophonic bass, density budgets, "
                               "originality thresholds, pipeline order).";
            }

            // Pattern path runs hot (0.9) so repeated requests explore new ideas.
            auto http = postChatCompletion (ep, system, attemptUser, 8192, 0.9);
            if (! http.ok)
            {
                resp.ok = false;
                resp.error = http.error;
                juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
                return;
            }

            parsed = parseClaudeMidiJson (http.text);
            if (! parsed.ok)
            {
                lastError = parsed.error;
                continue;
            }

            // Harmony-memory reject (ch.9-adjacent). Member access gated on `alive`:
            // if the AIClient was torn down mid-loop, skip the (unsafe) `this` touch
            // and just let this candidate through validation below instead.
            if (alive->load() && ! rememberProgressionIfFresh (parsed.pattern) && attempt + 1 < kMaxAttempts)
            {
                lastError = "chord progression matches a recent result";
                continue;
            }

            auto suite = validator.validateFullSuite (parsed.pattern, recentFpList,
                                                      parsed.pattern.instrumentSummary());
            if (! suite.ok)
            {
                lastError = suite.failures.joinIntoString (", ");
                resp.brainValidationNotes = suite.failures;
                resp.brainValidationNotes.addArray (suite.warnings);
                continue;
            }

            if (suite.score > bestScore)
            {
                bestScore = suite.score;
                bestPattern = parsed.pattern;
                haveBest = true;
            }
            // Accept first fully valid candidate
            haveBest = true;
            bestPattern = parsed.pattern;
            resp.brainValidationNotes = suite.warnings;
            break;
        }

        if (! haveBest)
        {
            // Last parsed attempt even if soft-failed — only if parse succeeded once
            if (parsed.ok)
            {
                bestPattern = parsed.pattern;
                haveBest = true;
                resp.brainValidationNotes.add ("accepted_after_validation_soft_fail");
            }
            else
            {
                resp.ok = false;
                resp.error = lastError.isNotEmpty() ? lastError
                                                    : "Brain validation rejected all candidates.";
                juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
                return;
            }
        }

        resp.ok = true;
        resp.pattern = std::move (bestPattern);
        if (lockKey.isNotEmpty())
            resp.pattern.key = lockKey;

        resp.assistantText =
            "Ready — " + juce::String (resp.pattern.totalNotes())
            + " notes across " + resp.pattern.instrumentSummary()
            + " · " + resp.pattern.key
            + " · " + juce::String (resp.pattern.bpm) + " BPM · "
            + juce::String (resp.pattern.bars) + " bars";

        if (brainArch.isNotEmpty())
            resp.assistantText << " · archetype: " << brainArch;
        if (brainChunks > 0)
            resp.assistantText << " · brain PDF: " << juce::String (brainChunks) << " chunks";

        if (matchedDocs.size() > 0 || brainTitles.size() > 0)
        {
            resp.assistantText << " · from: ";
            juce::StringArray shown;
            for (auto& t : brainTitles)
            {
                if (shown.size() >= 3) break;
                shown.add (t);
            }
            for (auto& t : matchedDocs)
            {
                if (shown.size() >= 3) break;
                if (! shown.contains (t))
                    shown.add (t);
            }
            resp.assistantText << shown.joinIntoString (", ");
            if (brainTitles.size() + matchedDocs.size() > shown.size())
                resp.assistantText << "…";
        }

        resp.assistantText << ". Showing in preview.";

        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
    });
}

//==============================================================================
void AIClient::requestMidiTransform (const juce::String& mode,
                                     const juce::String& instrumentName,
                                     const juce::String& midiContextJson,
                                     const juce::String& forcedKey,
                                     PatternCallback callback)
{
    const auto m = mode.toLowerCase().trim();
    juce::String prompt;

    if (m == "continue")
    {
        prompt << "Continue this existing " << instrumentName
               << " MIDI as a seamless next section. Keep the same key, BPM, bars length, "
                  "and instrument. Write a NEW loop starting at beat 0 (do not paste the "
                  "original notes unchanged). Match groove, register, and density.\n\n"
                  "EXISTING MIDI TO CONTINUE FROM:\n"
               << midiContextJson
               << "\n\nSet JSON instrument to \"" << instrumentName.toLowerCase() << "\".";
    }
    else
    {
        prompt << "Create a fresh variation of this " << instrumentName
               << " MIDI. Keep the same key, BPM, bars, instrument, and overall vibe, "
                  "but change rhythm / melodic choices enough that it feels new "
                  "(not a tiny edit).\n\n"
                  "EXISTING MIDI TO VARY:\n"
               << midiContextJson
               << "\n\nSet JSON instrument to \"" << instrumentName.toLowerCase() << "\".";
    }

    requestMidiPattern (prompt, forcedKey, std::move (callback));
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

    const auto fb = current;

    juce::String contextLine;
    contextLine << "Current: root=" << current.root << " scale=" << current.scale
                << " genre=" << current.genre << " bpm=" << (int) current.bpm
                << " bars=" << current.bars << ". Request: " << userPrompt;

    const auto ep = endpointSnapshot(); // by value for the worker

    juce::Thread::launch ([fb, contextLine, ep, callback]
    {
        Response resp;
        auto http = postChatCompletion (ep, buildLegacySystemPrompt(), contextLine, 1024);
        if (http.ok)
        {
            // parseLegacyResponse expects Anthropic envelope; wrap plain text as content.
            if (ep.provider == Provider::OpenAI)
            {
                auto* block = new juce::DynamicObject();
                block->setProperty ("type", "text");
                block->setProperty ("text", http.text);
                juce::Array<juce::var> content;
                content.add (juce::var (block));
                auto* top = new juce::DynamicObject();
                top->setProperty ("content", content);
                resp = parseLegacyResponse (juce::JSON::toString (juce::var (top)), fb);
            }
            else
            {
                // http.text is already extracted; wrap similarly
                auto* block = new juce::DynamicObject();
                block->setProperty ("type", "text");
                block->setProperty ("text", http.text);
                juce::Array<juce::var> content;
                content.add (juce::var (block));
                auto* top = new juce::DynamicObject();
                top->setProperty ("content", content);
                resp = parseLegacyResponse (juce::JSON::toString (juce::var (top)), fb);
            }
        }
        else
        {
            resp.ok = false;
            resp.error = http.error.isNotEmpty()
                ? http.error
                : juce::String ("Claude/OpenAI request failed.");
            resp.assistantText.clear();
            // Do NOT silently substitute localFallback — callers must show the
            // error and offer an explicit offline path.
        }

        juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
    });
}

//==============================================================================
AIClient::Response AIClient::parseLegacyResponse (const juce::String& raw,
                                                  const MusicParams& fallback)
{
    Response resp;
    resp.params = fallback;

    const auto text = extractTextContent (raw);
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

    if (lower.contains ("dark"))    { p.scale = "phrygian"; p.energy = 0.4f; }
    if (lower.contains ("happy") || lower.contains ("uplift")) p.scale = "major";
    if (lower.contains ("harder") || lower.contains ("aggress")) p.energy = juce::jmin (1.0f, p.energy + 0.25f);
    if (lower.contains ("bounce") || lower.contains ("bouncy"))  p.swing  = juce::jmin (0.6f, p.swing + 0.2f);
    if (lower.contains ("simple")) p.complexity = juce::jmax (0.0f, p.complexity - 0.3f);
    if (lower.contains ("complex")) p.complexity = juce::jmin (1.0f, p.complexity + 0.3f);

    auto bpmIdx = lower.indexOf ("bpm");
    if (bpmIdx > 0)
    {
        auto num = lower.substring (juce::jmax (0, bpmIdx - 5), bpmIdx).retainCharacters ("0123456789");
        if (num.isNotEmpty()) p.bpm = juce::jlimit (60, 200, num.getIntValue());
    }

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

juce::String AIClient::recentProgressionsForPrompt() const
{
    const juce::ScopedLock lock (progressionHistoryLock);
    juce::StringArray lines;
    int number = 1;
    for (const auto& fingerprint : recentChordProgressions)
        lines.add (juce::String (number++) + ". " + fingerprint);
    return lines.joinIntoString ("\n");
}

bool AIClient::rememberProgressionIfFresh (const MidiPattern& pattern)
{
    const auto fingerprint = chordProgressionFingerprint (pattern);
    if (fingerprint.isEmpty())
        return true; // bass/drum/melody-only requests have no harmony to compare

    const juce::ScopedLock lock (progressionHistoryLock);
    if (std::find (recentChordProgressions.begin(), recentChordProgressions.end(), fingerprint)
        != recentChordProgressions.end())
        return false;

    recentChordProgressions.push_back (fingerprint);
    constexpr size_t maxRememberedProgressions = 8;
    while (recentChordProgressions.size() > maxRememberedProgressions)
        recentChordProgressions.pop_front();
    return true;
}

void AIClient::rememberHarmonyFingerprint (const juce::String& fingerprint)
{
    if (fingerprint.isEmpty()) return;
    const juce::ScopedLock lock (progressionHistoryLock);
    if (std::find (recentChordProgressions.begin(), recentChordProgressions.end(), fingerprint)
        != recentChordProgressions.end())
        return;
    recentChordProgressions.push_back (fingerprint);
    constexpr size_t maxRememberedProgressions = 8;
    while (recentChordProgressions.size() > maxRememberedProgressions)
        recentChordProgressions.pop_front();
}

bool AIClient::hasRecentHarmonyFingerprint (const juce::String& fingerprint) const
{
    if (fingerprint.isEmpty()) return false;
    const juce::ScopedLock lock (progressionHistoryLock);
    return std::find (recentChordProgressions.begin(), recentChordProgressions.end(), fingerprint)
        != recentChordProgressions.end();
}

void AIClient::clearRecentHarmonyMemory()
{
    const juce::ScopedLock lock (progressionHistoryLock);
    recentChordProgressions.clear();
}

juce::String AIClient::bundledMasterPromptText() const
{
    return knowledgeBase.masterPromptText();
}

} // namespace aimidi
