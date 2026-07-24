#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace aimidi
{
/** ComposerAI Chat — liquid-glass redesign (design doc option 2a).
    Centered conversation + frosted composer + Runs/Seeds/Docs rail. */
class ChatPanel : public juce::Component,
                  private juce::Timer
{
public:
    struct SessionRun
    {
        juce::String seed;
        juce::String harmony;
        juce::String timeLabel;
        juce::StringArray chords;
    };

    ChatPanel();
    ~ChatPanel() override;

    std::function<void (juce::String)> onSend;
    std::function<void()> onAddDocs;
    std::function<void()> onShowDocsFolder;
    std::function<juce::String()> onRequestMidiAttach;

    void addUserMessage (const juce::String& text);
    void addAssistantMessage (const juce::String& text);
    void addSessionRun (const SessionRun& run);
    void setDocTitles (const juce::StringArray& titles);
    void setBusy (bool busy);
    void setDocsStatus (const juce::String& status);
    void setModelLabel (const juce::String& modelName);
    void clearMidiAttachment();
    void clearConversation();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    enum class Role { User, Assistant, Thinking };
    enum class RailTab { Runs, Seeds, Docs };

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

    class RailList : public juce::Component
    {
    public:
        void setRuns (const std::vector<SessionRun>& runs);
        void setSeeds (const juce::StringArray& seeds);
        void setDocs (const juce::StringArray& docs);
        void setMode (RailTab tab);
        void paint (juce::Graphics&) override;
        int contentHeight() const { return totalH; }

    private:
        RailTab mode = RailTab::Runs;
        std::vector<SessionRun> runs;
        juce::StringArray seeds, docs;
        int totalH = 40;
        void rebuild();
    };

    void fireSend();
    void toggleAttachMidi();
    void pushQuick (const juce::String& prompt);
    void refreshThreadLayout();
    void scrollToBottom();
    void timerCallback() override;
    void updateSuggestionVisibility();
    void setRailTab (RailTab t);
    void refreshRail();

    // Header
    juce::Label titleLabel { {}, "Chat" };
    juce::Label modelBadge;
    juce::TextButton newChatButton { "New" };

    // Main column
    juce::Viewport viewport;
    ThreadContent thread;

    juce::TextButton tipMake { "Full loop" };
    juce::TextButton tipVary { "Vary this" };
    juce::TextButton tipBass { "Bassline" };
    juce::TextButton tipTheory { "Melody" };

    juce::TextButton attachMidiButton { "@ MIDI" };
    juce::TextButton addDocsButton { "@ Docs" };
    juce::TextButton openDocsButton { "Brain" };

    juce::TextEditor input;
    juce::TextButton sendButton { "↑" };
    juce::Label contextChip;

    // Right rail
    juce::TextButton runsTab { "Runs" };
    juce::TextButton seedsTab { "Seeds" };
    juce::TextButton docsTab { "Docs" };
    juce::Viewport railViewport;
    RailList railList;
    juce::Label railFooter;

    juce::Rectangle<int> chatColumnBounds;
    juce::Rectangle<int> railBounds;
    juce::Rectangle<int> composerBounds;
    juce::Rectangle<int> chipRowBounds;

    bool busy = false;
    int thinkingTick = 0;
    juce::String attachedMidiContext;
    juce::String docsStatusText { "No theory docs" };
    juce::String modelNameText { "claude-sonnet-4-5" };
    bool hasUserMessage = false;
    RailTab railTab = RailTab::Runs;
    std::vector<SessionRun> sessionRuns;
    juce::StringArray pinnedSeeds;
    juce::StringArray docTitles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatPanel)
};

} // namespace aimidi
