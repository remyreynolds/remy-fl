#pragma once

#include "../engine/MusicInstructions.h"
#include <juce_core/juce_core.h>
#include <functional>

namespace aimidi
{
/** Talks to the Claude Messages API. Given a natural-language prompt plus the
    current project params, it returns an *updated* MusicParams (and a list of
    which instruments to (re)generate). The AI never emits raw MIDI — only the
    structured instructions the spec mandates.

    The request runs on a background thread; the callback is delivered on the
    JUCE message thread so the UI can update safely. */
class AIClient
{
public:
    struct Response
    {
        bool ok = false;
        juce::String assistantText;               // chat reply to show the user
        MusicParams params;                        // updated musical params
        std::vector<InstrumentType> toGenerate;    // parts the AI wants to (re)make
        juce::String error;
    };

    using Callback = std::function<void (Response)>;

    AIClient();

    void setApiKey (const juce::String& key) { apiKey = key; }
    bool hasApiKey() const { return apiKey.isNotEmpty(); }

    /** Send a user prompt. `current` is the project state; `lockedMask` marks
        instruments the AI must not touch. */
    void sendPrompt (const juce::String& userPrompt,
                     const MusicParams& current,
                     const std::vector<bool>& lockedMask,
                     Callback callback);

    /** Offline fallback: no network — parse a few keywords locally so the
        plugin still does something useful without an API key. */
    static Response localFallback (const juce::String& userPrompt,
                                   const MusicParams& current);

private:
    juce::String apiKey;
    juce::String model { "claude-sonnet-4-6" };

    static juce::String buildSystemPrompt();
    static juce::var    buildRequestBody (const juce::String& system,
                                          const juce::String& user,
                                          const juce::String& model);
    static Response     parseResponse (const juce::String& raw,
                                       const MusicParams& fallback);
};

} // namespace aimidi
