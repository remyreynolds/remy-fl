#include "ChatPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

//==============================================================================
ChatPanel::Bubble::Bubble (Role r, juce::String t)
    : role (r), text (std::move (t))
{
    setInterceptsMouseClicks (false, false);
}

void ChatPanel::Bubble::setText (const juce::String& t)
{
    text = t;
    resized();
    repaint();
}

void ChatPanel::Bubble::rebuildLayout (int width)
{
    const int padX = role == Role::User ? 36 : 16;
    const int textW = juce::jmax (40, width - padX);

    attributed = {};
    attributed.setJustification (juce::Justification::topLeft);
    attributed.append (text,
                       CustomLookAndFeel::font (13.5f),
                       role == Role::Thinking ? CustomLookAndFeel::txt2
                                             : CustomLookAndFeel::txt1);
    layout.createLayout (attributed, (float) textW);
}

int ChatPanel::Bubble::preferredHeight (int width) const
{
    ChatPanel::Bubble tmp (role, text);
    tmp.rebuildLayout (width);
    const float h = tmp.layout.getHeight();
    if (role == Role::User)
        return (int) std::ceil (h + 28.0f);
    if (role == Role::Thinking)
        return 34;
    return (int) std::ceil (h + 44.0f);
}

void ChatPanel::Bubble::resized()
{
    rebuildLayout (getWidth());
}

void ChatPanel::Bubble::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (role == Role::User)
    {
        auto bubble = bounds.reduced (12.0f, 3.0f);
        g.setColour (CustomLookAndFeel::userBubble);
        g.fillRoundedRectangle (bubble, 14.0f);
        rebuildLayout (getWidth());
        layout.draw (g, bubble.reduced (14.0f, 10.0f));
        return;
    }

    if (role == Role::Thinking)
    {
        auto chip = bounds.reduced (14.0f, 6.0f);
        g.setColour (CustomLookAndFeel::bg3);
        g.fillRoundedRectangle (chip, 10.0f);
        g.setColour (CustomLookAndFeel::txt2);
        g.setFont (CustomLookAndFeel::font (12.5f));
        g.drawText (text, chip.toNearestInt().reduced (12, 0), juce::Justification::centredLeft);
        return;
    }

    auto header = getLocalBounds().reduced (14, 0).removeFromTop (22);
    auto avatar = header.removeFromLeft (18).withSizeKeepingCentre (16, 16).toFloat();
    g.setColour (CustomLookAndFeel::accent);
    g.fillRoundedRectangle (avatar, 4.0f);
    g.setColour (CustomLookAndFeel::bg1);
    g.setFont (CustomLookAndFeel::font (9.0f, juce::Font::bold));
    g.drawText ("AI", avatar.toNearestInt(), juce::Justification::centred);

    header.removeFromLeft (8);
    g.setColour (CustomLookAndFeel::txt2);
    g.setFont (CustomLookAndFeel::font (11.5f));
    g.drawText ("Agent", header, juce::Justification::centredLeft);

    rebuildLayout (getWidth());
    auto body = getLocalBounds().reduced (14, 0);
    body.removeFromTop (26);
    layout.draw (g, body.toFloat());
}

//==============================================================================
void ChatPanel::ThreadContent::clear()
{
    bubbles.clear();
    thinking.reset();
    totalHeight = 0;
    removeAllChildren();
}

void ChatPanel::ThreadContent::addBubble (Role role, const juce::String& text)
{
    auto b = std::make_unique<Bubble> (role, text);
    addAndMakeVisible (*b);
    bubbles.push_back (std::move (b));
}

void ChatPanel::ThreadContent::setThinking (bool on, const juce::String& label)
{
    if (on)
    {
        if (thinking == nullptr)
        {
            thinking = std::make_unique<Bubble> (Role::Thinking, label);
            addAndMakeVisible (*thinking);
        }
        else
        {
            thinking->setText (label);
        }
    }
    else if (thinking != nullptr)
    {
        removeChildComponent (thinking.get());
        thinking.reset();
    }
}

void ChatPanel::ThreadContent::layoutBubbles (int width)
{
    int y = 10;
    const int gap = 8;

    for (auto& b : bubbles)
    {
        const int h = b->preferredHeight (width);
        b->setBounds (0, y, width, h);
        y += h + gap;
    }

    if (thinking != nullptr)
    {
        const int h = thinking->preferredHeight (width);
        thinking->setBounds (0, y, width, h);
        y += h + gap;
    }

    totalHeight = juce::jmax (y + 12, 48);
    setSize (width, totalHeight);
}

//==============================================================================
ChatPanel::ChatPanel()
{
    titleLabel.setFont (CustomLookAndFeel::font (14.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (titleLabel);

    modelLabel.setFont (CustomLookAndFeel::font (11.5f));
    modelLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    modelLabel.setText ("claude-sonnet-5", juce::dontSendNotification);
    addAndMakeVisible (modelLabel);

    docsLabel.setFont (CustomLookAndFeel::font (11.0f));
    docsLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    docsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (docsLabel);

    newChatButton.setComponentID ("ghost");
    newChatButton.setTooltip ("New chat");
    newChatButton.onClick = [this] { clearConversation(); };
    addAndMakeVisible (newChatButton);

    attachMidiButton.setComponentID ("ghost");
    attachMidiButton.setTooltip ("Attach current part as @ MIDI context");
    attachMidiButton.onClick = [this] { toggleAttachMidi(); };
    addAndMakeVisible (attachMidiButton);

    addDocsButton.setComponentID ("ghost");
    addDocsButton.setTooltip ("Add docs to brain");
    addDocsButton.onClick = [this] { if (onAddDocs) onAddDocs(); };
    addAndMakeVisible (addDocsButton);

    openDocsButton.setComponentID ("ghost");
    openDocsButton.setTooltip ("Open brain folder");
    openDocsButton.onClick = [this] { if (onShowDocsFolder) onShowDocsFolder(); };
    addAndMakeVisible (openDocsButton);

    viewport.setViewedComponent (&thread, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    auto wireTip = [this] (juce::TextButton& b, juce::String prompt)
    {
        b.setComponentID ("ghost");
        b.onClick = [this, prompt] { pushQuick (prompt); };
        addAndMakeVisible (b);
    };
    wireTip (tipMake, "make a full 8-bar tech house loop with chords, bass, melody, and drums in the project key");
    wireTip (tipVary, "vary this");
    wireTip (tipBass, "make a groovy tech house bassline in the project key, 8 bars");
    wireTip (tipTheory, "make a memorable house topline melody over the current key, 8 bars");

    input.setMultiLine (true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Plan, search, build anything…",
                                  CustomLookAndFeel::txt2.withAlpha (0.65f));
    input.setFont (CustomLookAndFeel::font (13.5f));
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.setIndents (4, 6);
    input.onReturnKey = [this] { fireSend(); };
    addAndMakeVisible (input);

    sendButton.setComponentID ("primary");
    sendButton.setTooltip ("Send");
    sendButton.onClick = [this] { fireSend(); };
    addAndMakeVisible (sendButton);

    contextChip.setFont (CustomLookAndFeel::font (11.5f));
    contextChip.setColour (juce::Label::textColourId, CustomLookAndFeel::accent);
    contextChip.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (contextChip);

    setDocsStatus ("No theory docs");
    addAssistantMessage ("Hi — I'm your MIDI agent.\n\n"
                         "I can generate full loops or single parts:\n"
                         "• full loop — chords + bass + melody + drums\n"
                         "• one role — bassline, chords, melody, arp, pad\n"
                         "• @ MIDI then say vary this / continue this\n\n"
                         "Brain docs + Claude guide what sounds good.");
    updateSuggestionVisibility();
}

ChatPanel::~ChatPanel()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

void ChatPanel::addUserMessage (const juce::String& text)
{
    hasUserMessage = true;
    thread.addBubble (Role::User, text);
    refreshThreadLayout();
    updateSuggestionVisibility();
}

void ChatPanel::addAssistantMessage (const juce::String& text)
{
    thread.addBubble (Role::Assistant, text);
    refreshThreadLayout();
}

void ChatPanel::setDocsStatus (const juce::String& status)
{
    docsStatusText = status;
    docsLabel.setText (status, juce::dontSendNotification);
}

void ChatPanel::setModelLabel (const juce::String& modelName)
{
    modelLabel.setText (modelName.isNotEmpty() ? modelName : "model",
                        juce::dontSendNotification);
}

void ChatPanel::clearMidiAttachment()
{
    attachedMidiContext.clear();
    attachMidiButton.setButtonText ("@ MIDI");
    contextChip.setText ({}, juce::dontSendNotification);
    resized();
}

void ChatPanel::clearConversation()
{
    stopTimer();
    busy = false;
    thread.clear();
    hasUserMessage = false;
    clearMidiAttachment();
    sendButton.setButtonText ("↑");
    input.setEnabled (true);
    addAssistantMessage ("New chat. What should we make?");
    updateSuggestionVisibility();
    refreshThreadLayout();
}

void ChatPanel::toggleAttachMidi()
{
    if (attachedMidiContext.isNotEmpty())
    {
        clearMidiAttachment();
        addAssistantMessage ("Removed @ MIDI context.");
        return;
    }

    if (! onRequestMidiAttach)
    {
        addAssistantMessage ("Generate a part first, then attach with @ MIDI.");
        return;
    }

    const auto ctx = onRequestMidiAttach();
    if (ctx.isEmpty())
    {
        addAssistantMessage ("No MIDI ready — Generate a part, then @ MIDI.");
        return;
    }

    attachedMidiContext = ctx;
    attachMidiButton.setButtonText ("@ MIDI ✓");
    contextChip.setText ("@ MIDI · included in next message", juce::dontSendNotification);
    addAssistantMessage ("Attached @ MIDI. Try: vary this · continue this · make it darker.");
    resized();
}

void ChatPanel::pushQuick (const juce::String& prompt)
{
    if (busy) return;

    if (prompt.equalsIgnoreCase ("vary this") && attachedMidiContext.isEmpty() && onRequestMidiAttach)
    {
        if (const auto ctx = onRequestMidiAttach(); ctx.isNotEmpty())
        {
            attachedMidiContext = ctx;
            attachMidiButton.setButtonText ("@ MIDI ✓");
            contextChip.setText ("@ MIDI · included in next message", juce::dontSendNotification);
            resized();
        }
    }

    input.setText (prompt, false);
    fireSend();
}

void ChatPanel::fireSend()
{
    if (busy) return;
    auto t = input.getText().trim();
    if (t.isEmpty()) return;

    addUserMessage (t);
    input.clear();

    if (attachedMidiContext.isNotEmpty())
        t << "\n\n[ATTACHED MIDI CONTEXT]\n" << attachedMidiContext;

    if (onSend) onSend (t);
}

void ChatPanel::setBusy (bool b)
{
    busy = b;
    updateSuggestionVisibility();

    if (b)
    {
        thinkingTick = 0;
        thread.setThinking (true, "Thinking...");
        startTimerHz (5);
        sendButton.setButtonText ("…");
        sendButton.setEnabled (false);
    }
    else
    {
        stopTimer();
        thread.setThinking (false);
        sendButton.setButtonText ("↑");
        sendButton.setEnabled (true);
    }

    input.setEnabled (! b);
    tipMake.setEnabled (! b);
    tipVary.setEnabled (! b);
    tipBass.setEnabled (! b);
    tipTheory.setEnabled (! b);
    attachMidiButton.setEnabled (! b);
    addDocsButton.setEnabled (! b);
    newChatButton.setEnabled (! b);
    refreshThreadLayout();
}

void ChatPanel::timerCallback()
{
    static const char* frames[] = { "Thinking", "Thinking.", "Thinking..", "Thinking..." };
    ++thinkingTick;
    thread.setThinking (true, frames[thinkingTick % 4]);
    refreshThreadLayout();
}

void ChatPanel::refreshThreadLayout()
{
    const int w = juce::jmax (200, viewport.getWidth());
    thread.layoutBubbles (w);
    scrollToBottom();
}

void ChatPanel::scrollToBottom()
{
    viewport.setViewPosition (0, juce::jmax (0, thread.getHeight() - viewport.getHeight()));
}

void ChatPanel::updateSuggestionVisibility()
{
    const bool show = ! hasUserMessage && ! busy;
    const bool changed = tipMake.isVisible() != show;
    tipMake.setVisible (show);
    tipVary.setVisible (show);
    tipBass.setVisible (show);
    tipTheory.setVisible (show);
    if (changed)
        resized();
}

void ChatPanel::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawChatSurface (g, getLocalBounds());

    auto top = getLocalBounds().removeFromTop (52);
    g.setColour (CustomLookAndFeel::line.withAlpha (0.55f));
    g.drawHorizontalLine (top.getBottom() - 1, 0.0f, (float) getWidth());

    if (! composerBounds.isEmpty())
        CustomLookAndFeel::drawComposerShell (g, composerBounds);
}

void ChatPanel::resized()
{
    auto r = getLocalBounds();

    auto titleRow = r.removeFromTop (52).reduced (14, 10);
    titleLabel.setBounds (titleRow.removeFromLeft (56));
    titleRow.removeFromLeft (6);
    modelLabel.setBounds (titleRow.removeFromLeft (128));
    newChatButton.setBounds (titleRow.removeFromRight (48).withSizeKeepingCentre (48, 28));
    titleRow.removeFromRight (4);
    openDocsButton.setBounds (titleRow.removeFromRight (52).withSizeKeepingCentre (52, 28));
    titleRow.removeFromRight (4);
    addDocsButton.setBounds (titleRow.removeFromRight (62).withSizeKeepingCentre (62, 28));
    titleRow.removeFromRight (4);
    attachMidiButton.setBounds (titleRow.removeFromRight (76).withSizeKeepingCentre (76, 28));
    titleRow.removeFromRight (6);
    docsLabel.setBounds (titleRow);

    r.removeFromBottom (12);
    r = r.reduced (12, 0);

    const bool hasChip = attachedMidiContext.isNotEmpty();
    composerBounds = r.removeFromBottom (hasChip ? 108 : 92);

    auto composerInner = composerBounds.reduced (12, 10);
    if (hasChip)
    {
        contextChip.setBounds (composerInner.removeFromTop (18));
        composerInner.removeFromTop (4);
    }
    else
    {
        contextChip.setBounds ({});
    }

    sendButton.setBounds (composerInner.removeFromRight (40).withSizeKeepingCentre (36, 36));
    composerInner.removeFromRight (8);
    input.setBounds (composerInner);

    r.removeFromBottom (10);

    if (tipMake.isVisible())
    {
        auto tips = r.removeFromBottom (34);
        const int gap = 6;
        const int tw = (tips.getWidth() - gap * 3) / 4;
        tipMake.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipBass.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipVary.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipTheory.setBounds (tips);
        r.removeFromBottom (8);
    }
    else
    {
        tipMake.setBounds ({});
        tipBass.setBounds ({});
        tipVary.setBounds ({});
        tipTheory.setBounds ({});
    }

    viewport.setBounds (r);
    refreshThreadLayout();
}

} // namespace aimidi
