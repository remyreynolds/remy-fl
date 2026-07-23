#include "AIClient.h"
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
        anthropicApiKey = env;
        if (provider == Provider::Claude)
            loadedFromEnv = true;
    }
    else
    {
        anthropicApiKey = props.getValue ("anthropicApiKey");
    }

    if (auto env = juce::SystemStats::getEnvironmentVariable ("OPENAI_API_KEY", {});
        env.isNotEmpty())
    {
        openAiApiKey = env;
        if (provider == Provider::OpenAI)
            loadedFromEnv = true;
    }
    else
    {
        openAiApiKey = props.getValue ("openAiApiKey");
    }

    claudeModel = props.getValue ("claudeModel", "claude-sonnet-5");
    openAiModel = props.getValue ("openAiModel", "gpt-4o");
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
            anthropicApiKey = env;
            loadedFromEnv = true;
        }
    }
    else if (auto env = juce::SystemStats::getEnvironmentVariable ("OPENAI_API_KEY", {});
             env.isNotEmpty())
    {
        openAiApiKey = env;
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

    if (provider == Provider::OpenAI)
    {
        openAiApiKey = key;
        props.setValue ("openAiApiKey", key);
    }
    else
    {
        anthropicApiKey = key;
        props.setValue ("anthropicApiKey", key);
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
R"(You are **Groovewright**, the house-music MIDI agent inside AI MIDI Gen.

Obey the MASTER SYSTEM PROMPT (THE BRAIN) and any MUSIC THEORY REFERENCES from
the local brain knowledge folder (same corpus the MIDI generator uses).

Behavior:
- Conversational by default — short, producer-to-producer (under ~4 sentences).
- Prefer local brain docs when they cover the topic.
- You MAY also use your Claude/model knowledge to fill gaps, explain deeper
  theory, or compare approaches — clearly prefer brain docs when they conflict.
- Do NOT output MIDI JSON unless the user clearly asks to make/generate/compose/vary/continue/extend/remix MIDI.
- Groove over theory. Prefer deep/tech/French/prog/afro/garage/piano house authenticity.
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
                                      int maxTokens)
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

AIClient::LlmHttpResult AIClient::postChatCompletion (const juce::String& system,
                                                      const juce::String& user,
                                                      int maxTokens) const
{
    LlmHttpResult result;
    const auto key = activeApiKey();
    const auto mdl = activeModel();

    if (key.isEmpty())
    {
        result.error = "Add your " + providerDisplayName() + " API key first (header field).";
        return result;
    }

    juce::String json;
    juce::URL url;
    juce::String headers;

    if (provider == Provider::OpenAI)
    {
        json = juce::JSON::toString (buildOpenAiRequestBody (system, user, mdl, maxTokens));
        url = juce::URL ("https://api.openai.com/v1/chat/completions").withPOSTData (json);
        headers = "Authorization: Bearer " + key + "\r\n"
                  "content-type: application/json\r\n";
    }
    else
    {
        json = juce::JSON::toString (buildRequestBody (system, user, mdl, maxTokens));
        url = juce::URL ("https://api.anthropic.com/v1/messages").withPOSTData (json);
        headers = "x-api-key: " + key + "\r\n"
                  "anthropic-version: 2023-06-01\r\n"
                  "content-type: application/json\r\n";
    }

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
        result.error = provider == Provider::OpenAI
                           ? "Network error: could not reach api.openai.com."
                           : "Network error: could not reach api.anthropic.com.";
        return result;
    }

    const auto raw = stream->readEntireStreamAsString();
    result.statusCode = statusCode;
    if (statusCode < 200 || statusCode >= 300)
    {
        result.error = describeHttpError (statusCode, raw);
        return result;
    }

    result.text = provider == Provider::OpenAI
                      ? extractOpenAiTextContent (raw)
                      : extractTextContent (raw);
    if (result.text.isEmpty())
    {
        result.error = providerDisplayName() + " returned an empty response.";
        return result;
    }

    result.ok = true;
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

    if (statusCode == 401 || statusCode == 403)
        return "Invalid or unauthorized API key. Check your Anthropic key.";
    if (statusCode == 429)
        return "Rate limited by Anthropic. Wait a moment and try again.";
    if (statusCode == 400)
        return "Bad request to Claude" + (detail.isNotEmpty() ? (": " + detail) : ".");
    if (statusCode >= 500)
        return "Anthropic server error (HTTP " + juce::String (statusCode) + "). Try again.";

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
            [this, prompt, callback] (PatternResponse r)
            {
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
        [this, prompt, callback] (ChatResponse r)
        {
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

    juce::Thread::launch ([this, system, finalUser, user, useClaudeHistory, bodyVar, matched, callback]
    {
        ChatResponse resp;
        resp.matchedDocTitles = matched;

        LlmHttpResult http;
        if (useClaudeHistory)
        {
            // Use the prebuilt Anthropic history body path.
            const auto key = activeApiKey();
            const auto json = juce::JSON::toString (bodyVar);
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
                               .withConnectionTimeoutMs (45000)
                               .withResponseHeaders (&responseHeaders)
                               .withStatusCode (&statusCode);
            auto stream = url.createInputStream (options);
            if (stream == nullptr)
            {
                resp.ok = false;
                resp.error = "Network error: could not reach api.anthropic.com.";
                juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
                return;
            }
            const auto raw = stream->readEntireStreamAsString();
            if (statusCode < 200 || statusCode >= 300)
            {
                resp.ok = false;
                resp.error = describeHttpError (statusCode, raw);
                juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
                return;
            }
            http.text = extractTextContent (raw);
            http.ok = http.text.isNotEmpty();
            if (! http.ok)
                http.error = "Claude returned an empty response.";
        }
        else
        {
            http = postChatCompletion (system, finalUser, 1024);
        }

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
    auto retrieval = knowledgeBase.retrieveForQuery (prompt);
    const auto knowledgeCtx = retrieval.context;
    const auto matchedDocs = retrieval.matchedDocs;
    const int docsUsed = matchedDocs.size();
    const auto projCtx = projectContext; // copy for thread safety
    const auto recentProgressions = recentProgressionsForPrompt();
    const auto variationNonce = juce::Uuid().toString();

    juce::Thread::launch ([this, prompt, lockKey, knowledgeCtx, projCtx, matchedDocs,
                           docsUsed, recentProgressions, variationNonce, callback]
    {
        PatternResponse resp;
        resp.knowledgeDocsUsed = docsUsed;
        resp.matchedDocTitles = matchedDocs;

        juce::String system;
        const auto masterBrain = knowledgeBase.masterPromptText();
        if (masterBrain.trim().isNotEmpty())
        {
            system << "===== MASTER SYSTEM PROMPT (bundled Brain) =====\n"
                   << masterBrain.trim()
                   << "\n===== END MASTER SYSTEM PROMPT =====\n\n";
        }
        system << buildClaudeMidiSystemPrompt();
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

        if (knowledgeCtx.isNotEmpty())
        {
            user << "\n\n===== MUSIC THEORY REFERENCES (shared brain knowledge) =====\n"
                 << knowledgeCtx
                 << "\n===== END REFERENCES =====\n"
                 << "These are the same brain guides the generator uses. Prefer them, "
                    "then fill gaps with Claude knowledge. Compose MIDI that follows "
                    "those rules and the project key/BPM/bars.";
        }

        auto http = postChatCompletion (system, user, 8192);
        if (! http.ok)
        {
            resp.ok = false;
            resp.error = http.error;
            juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
            return;
        }

        auto parsed = parseClaudeMidiJson (http.text);
        if (! parsed.ok)
        {
            resp.ok = false;
            resp.error = parsed.error;
            juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
            return;
        }

        // One automatic retry when Claude returns harmony already heard in this
        // session. The second prompt is explicit instead of silently accepting
        // the familiar progression again.
        if (! rememberProgressionIfFresh (parsed.pattern))
        {
            auto retryUser = user + "\n\nREJECTED: that chord progression matches a recent result. "
                "Compose a genuinely different progression now. Change the functional root motion "
                "and at least two of: chord rhythm, extensions, inversions, or voice-leading contour.";
            http = postChatCompletion (system, retryUser, 8192);
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
                resp.ok = false;
                resp.error = parsed.error;
                juce::MessageManager::callAsync ([callback, resp] { callback (resp); });
                return;
            }
            (void) rememberProgressionIfFresh (parsed.pattern);
        }

        resp.ok = true;
        resp.pattern = std::move (parsed.pattern);
        if (lockKey.isNotEmpty())
            resp.pattern.key = lockKey;

        resp.assistantText =
            "Ready — " + juce::String (resp.pattern.totalNotes())
            + " notes across " + resp.pattern.instrumentSummary()
            + " · " + resp.pattern.key
            + " · " + juce::String (resp.pattern.bpm) + " BPM · "
            + juce::String (resp.pattern.bars) + " bars";

        if (matchedDocs.size() > 0)
        {
            resp.assistantText << " · from docs: ";
            for (int i = 0; i < matchedDocs.size(); ++i)
            {
                if (i) resp.assistantText << ", ";
                if (i >= 3) { resp.assistantText << "…"; break; }
                resp.assistantText << matchedDocs[i];
            }
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

    juce::Thread::launch ([this, fb, contextLine, callback]
    {
        Response resp;
        auto http = postChatCompletion (buildLegacySystemPrompt(), contextLine, 1024);
        if (http.ok)
        {
            // parseLegacyResponse expects Anthropic envelope; wrap plain text as content.
            if (provider == Provider::OpenAI)
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
