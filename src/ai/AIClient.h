#pragma once

#include "../engine/MusicInstructions.h"
#include "MidiPattern.h"
#include "KnowledgeBase.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <deque>
#include <vector>

namespace aimidi
{
/** LLM client (Claude / OpenAI): conversational chat by default; MIDI generation
    when the user asks to make/vary/continue something. Knowledge docs are
    retrieved and injected for both paths. */
class AIClient
{
public:
    enum class Provider { Claude, OpenAI };

    struct Response
    {
        bool ok = false;
        juce::String assistantText;
        MusicParams params;
        std::vector<InstrumentType> toGenerate;
        juce::String error;
    };

    struct PatternResponse
    {
        bool ok = false;
        juce::String assistantText;
        juce::String error;
        MidiPattern pattern;
        int knowledgeDocsUsed = 0;
        juce::StringArray matchedDocTitles;
    };

    struct ChatResponse
    {
        bool ok = false;
        juce::String assistantText;
        juce::String error;
        juce::StringArray matchedDocTitles;
    };

    /** Result of routing a chat box message. */
    struct TurnResponse
    {
        bool ok = false;
        bool generatedMidi = false;
        juce::String assistantText;
        juce::String error;
        MidiPattern pattern;
        juce::StringArray matchedDocTitles;
    };

    using Callback = std::function<void (Response)>;
    using PatternCallback = std::function<void (PatternResponse)>;
    using ChatCallback = std::function<void (ChatResponse)>;
    using TurnCallback = std::function<void (TurnResponse)>;

    AIClient();

    void setProvider (Provider p);
    Provider getProvider() const { return provider; }
    void setApiKey (const juce::String& key);
    bool hasApiKey() const;
    bool apiKeyFromEnvironment() const { return loadedFromEnv; }
    juce::String apiKeyPlaceholder() const;
    juce::String providerDisplayName() const;

    void setClaudeModel (const juce::String& modelId);
    juce::String getClaudeModel() const { return claudeModel; }
    void setOpenAiModel (const juce::String& modelId);
    juce::String getOpenAiModel() const { return openAiModel; }

    KnowledgeBase& knowledge() { return knowledgeBase; }
    const KnowledgeBase& knowledge() const { return knowledgeBase; }

    /** Live "PROJECT STATE" brief injected into every AI turn so answers are
        grounded in what's actually loaded (set by the processor each turn). */
    void setProjectContext (const juce::String& brief) { projectContext = brief; }

    /** True when the user is asking to create / transform MIDI (vs just chatting). */
    static bool looksLikeMidiGenerateRequest (const juce::String& prompt);

    /** Chat box entry point: converse OR generate MIDI.
        If forcedKey is set (e.g. "F minor"), MIDI generation must stay in that key. */
    void handleUserTurn (const juce::String& userPrompt,
                         const juce::String& forcedKey,
                         TurnCallback callback);

    void requestMidiPattern (const juce::String& userPrompt,
                             const juce::String& forcedKey,
                             PatternCallback callback);

    /** Vary or continue an existing part (MIDI Agent-style). */
    void requestMidiTransform (const juce::String& mode, // "vary" | "continue"
                               const juce::String& instrumentName,
                               const juce::String& midiContextJson,
                               const juce::String& forcedKey,
                               PatternCallback callback);

    void sendChatMessage (const juce::String& userPrompt, ChatCallback callback);

    void sendPrompt (const juce::String& userPrompt,
                     const MusicParams& current,
                     const std::vector<bool>& lockedMask,
                     Callback callback);

    static Response localFallback (const juce::String& userPrompt,
                                   const MusicParams& current);

private:
    struct Turn
    {
        bool fromUser = true;
        juce::String text;
    };

    struct LlmHttpResult
    {
        bool ok = false;
        juce::String text;
        juce::String error;
        int statusCode = 0;
    };

    Provider provider = Provider::Claude;
    juce::String anthropicApiKey;
    juce::String openAiApiKey;
    bool loadedFromEnv = false;
    juce::String claudeModel { "claude-sonnet-5" };
    juce::String openAiModel { "gpt-4o" };
    KnowledgeBase knowledgeBase;
    juce::String projectContext; // live plugin snapshot for grounding
    std::vector<Turn> history; // short conversational memory
    std::deque<juce::String> recentChordProgressions;
    mutable juce::CriticalSection progressionHistoryLock;

    void pushHistory (bool fromUser, const juce::String& text);
    juce::var buildChatMessagesBody (const juce::String& system,
                                     const juce::String& latestUserWithRefs) const;

    juce::String activeApiKey() const;
    juce::String activeModel() const;
    LlmHttpResult postChatCompletion (const juce::String& system,
                                      const juce::String& user,
                                      int maxTokens) const;

    static juce::String buildChatSystemPrompt();
    static juce::String buildLegacySystemPrompt();
    static juce::var    buildRequestBody (const juce::String& system,
                                          const juce::String& user,
                                          const juce::String& model,
                                          int maxTokens);
    static juce::var    buildOpenAiRequestBody (const juce::String& system,
                                                const juce::String& user,
                                                const juce::String& model,
                                                int maxTokens);
    static Response     parseLegacyResponse (const juce::String& raw,
                                             const MusicParams& fallback);
    static juce::String extractTextContent (const juce::String& anthropicRaw);
    static juce::String extractOpenAiTextContent (const juce::String& openAiRaw);
    static juce::String describeHttpError (int statusCode, const juce::String& body);
    juce::String recentProgressionsForPrompt() const;
    bool rememberProgressionIfFresh (const MidiPattern& pattern);
};

} // namespace aimidi
