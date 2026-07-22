#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace aimidi
{
/** Cursor-like agent chat: bubbled messages, thinking row, @ context, composer. */
class ChatPanel : public juce::Component,
                  private juce::Timer
{
public:
    ChatPanel();
    ~ChatPanel() override;

    std::function<void (juce::String)> onSend;
    std::function<void()> onAddDocs;
    std::function<void()> onShowDocsFolder;
    std::function<juce::String()> onRequestMidiAttach;

    void addUserMessage (const juce::String& text);
    void addAssistantMessage (const juce::String& text);
    void setBusy (bool busy);
    void setDocsStatus (const juce::String& status);
    void setModelLabel (const juce::String& modelName);
    void clearMidiAttachment();
    void clearConversation();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    enum class Role { User, Assistant, Thinking };

    class Bubble : public juce::Component
    {
    public:
        Bubble (Role role, juce::String text);
        void setText (const juce::String& t);
        void paint (juce::Graphics&) override;
        void resized() override;
        int preferredHeight (int width) const;
        Role getRole() const { return role; }

    private:
        Role role;
        juce::String text;
        juce::AttributedString attributed;
        juce::TextLayout layout;
        void rebuildLayout (int width);
    };

    class ThreadContent : public juce::Component
    {
    public:
        void clear();
        void addBubble (Role role, const juce::String& text);
        void setThinking (bool on, const juce::String& label = "Thinking...");
        void layoutBubbles (int width);
        int contentHeight() const { return totalHeight; }

    private:
        std::vector<std::unique_ptr<Bubble>> bubbles;
        std::unique_ptr<Bubble> thinking;
        int totalHeight = 0;
    };

    void fireSend();
    void toggleAttachMidi();
    void pushQuick (const juce::String& prompt);
    void refreshThreadLayout();
    void scrollToBottom();
    void timerCallback() override;
    void updateSuggestionVisibility();

    juce::Label titleLabel { {}, "Agent" };
    juce::Label modelLabel;
    juce::Label docsLabel;
    juce::TextButton newChatButton { "New" };
    juce::TextButton attachMidiButton { "@ MIDI" };
    juce::TextButton addDocsButton { "@ Docs" };
    juce::TextButton openDocsButton { "Brain" };

    juce::Viewport viewport;
    ThreadContent thread;

    juce::TextButton tipMake { "Full loop" };
    juce::TextButton tipVary { "Vary this" };
    juce::TextButton tipBass { "Bassline" };
    juce::TextButton tipTheory { "Melody" };

    juce::TextEditor input;
    juce::TextButton sendButton { "↑" };
    juce::Label contextChip;
    juce::Rectangle<int> composerBounds;

    bool busy = false;
    int thinkingTick = 0;
    juce::String attachedMidiContext;
    juce::String docsStatusText { "No theory docs" };
    bool hasUserMessage = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatPanel)
};

} // namespace aimidi
