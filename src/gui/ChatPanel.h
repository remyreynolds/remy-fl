#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace aimidi
{
/** The AI chat window: scrolling transcript + input box + send button.
    Emits the user's prompt via onSend; the editor wires it to the processor. */
class ChatPanel : public juce::Component
{
public:
    ChatPanel();

    std::function<void (juce::String)> onSend;

    void addUserMessage (const juce::String& text)      { append ("You", text); }
    void addAssistantMessage (const juce::String& text) { append ("AI", text); }
    void setBusy (bool busy);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void append (const juce::String& who, const juce::String& text);
    void fireSend();

    juce::TextEditor transcript;   // read-only, multi-line
    juce::TextEditor input;        // user entry
    juce::TextButton sendButton { "Send" };
    bool busy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatPanel)
};

} // namespace aimidi
