#include "ChatPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{
namespace
{
constexpr int kRailW = 280;
constexpr int kMaxChatInner = 700;
constexpr float kGlassR = 16.0f;
} // namespace

//==============================================================================
ChatPanel::Bubble::Bubble (Role r, juce::String t)
    : role (r), text (std::move (t))
{
    setInterceptsMouseClicks (false, false);
}

void ChatPanel::Bubble::setText (const juce::String& t)
{
    text = t;
    layoutWidth = -1;     // invalidate caches
    cachedPrefWidth = -1;
    resized();
    repaint();
}

void ChatPanel::Bubble::rebuildLayout (int width)
{
    if (width == layoutWidth)
        return; // layout already built for this width

    const int sidePad = role == Role::User ? 72 : 56;
    const int textW = juce::jmax (40, juce::jmin (kMaxChatInner, width) - sidePad);

    attributed = {};
    attributed.setJustification (juce::Justification::topLeft);
    const auto colour = role == Role::User ? juce::Colours::white
                        : (role == Role::Thinking ? CustomLookAndFeel::txt2
                                                  : CustomLookAndFeel::txt1);
    attributed.append (text, CustomLookAndFeel::font (role == Role::User ? 13.5f : 14.0f), colour);
    layout.createLayout (attributed, (float) textW);
    layoutWidth = width;
}

int ChatPanel::Bubble::preferredHeight (int width)
{
    if (role == Role::Thinking)
        return 32;
    if (width == cachedPrefWidth)
        return cachedPrefHeight; // avoid rebuilding a TextLayout every tick

    rebuildLayout (width);
    const float h = layout.getHeight();
    cachedPrefWidth = width;
    cachedPrefHeight = role == Role::User ? (int) std::ceil (h + 36.0f)
                                          : (int) std::ceil (h + 44.0f); // avatar row + glass padding
    return cachedPrefHeight;
}

void ChatPanel::Bubble::resized()
{
    rebuildLayout (getWidth());
}

void ChatPanel::Bubble::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float contentW = juce::jmin ((float) kMaxChatInner, bounds.getWidth());
    const float x0 = bounds.getCentreX() - contentW * 0.5f;

    if (role == Role::User)
    {
        rebuildLayout (getWidth());
        const float bw = juce::jmin (500.0f, contentW * 0.78f);
        const float bh = layout.getHeight() + 24.0f;
        auto bubble = juce::Rectangle<float> (x0 + contentW - bw, 4.0f, bw, bh);

        g.setColour (CustomLookAndFeel::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (bubble.translated (0, 2.0f).expanded (1.0f), 18.0f);
        g.setColour (CustomLookAndFeel::accent);
        // Asymmetric: 18 18 4 18
        juce::Path p;
        p.addRoundedRectangle (bubble.getX(), bubble.getY(), bubble.getWidth(), bubble.getHeight(),
                               18.0f, 18.0f, true, true, true, false);
        g.fillPath (p);
        layout.draw (g, bubble.reduced (16.0f, 12.0f));
        return;
    }

    if (role == Role::Thinking)
    {
        auto chip = juce::Rectangle<float> (x0 + 42.0f, 6.0f, 160.0f, 22.0f);
        g.setColour (CustomLookAndFeel::txt2);
        g.setFont (CustomLookAndFeel::font (13.0f));
        g.drawText (text, chip.toNearestInt(), juce::Justification::centredLeft);
        return;
    }

    // Assistant — accent avatar + frosted card
    rebuildLayout (getWidth());
    const float avatar = 28.0f;
    auto av = juce::Rectangle<float> (x0, 6.0f, avatar, avatar);
    g.setColour (CustomLookAndFeel::accent.withAlpha (0.45f));
    g.fillEllipse (av.expanded (2.0f));
    g.setColour (CustomLookAndFeel::accent);
    g.fillEllipse (av);
    // Simple note glyph
    g.setColour (juce::Colours::white);
    g.setFont (CustomLookAndFeel::font (12.0f, juce::Font::bold));
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x99\xaa"), av.toNearestInt(),
                juce::Justification::centred);

    const float cardX = x0 + avatar + 12.0f;
    const float cardW = contentW - avatar - 12.0f;
    const float cardH = layout.getHeight() + 28.0f;
    auto card = juce::Rectangle<float> (cardX, 2.0f, cardW, cardH);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (card, 18.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    juce::Path cardPath;
    cardPath.addRoundedRectangle (card.getX(), card.getY(), card.getWidth(), card.getHeight(),
                                  4.0f, 18.0f, false, true, true, true);
    g.strokePath (cardPath, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillPath (cardPath);

    layout.draw (g, card.reduced (16.0f, 14.0f));
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
    int y = 18;
    const int gap = 14;

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

    totalHeight = juce::jmax (y + 20, 64);
    setSize (width, totalHeight);
}

//==============================================================================
void ChatPanel::RailList::setRuns (const std::vector<SessionRun>& r)
{
    runs = r;
    rebuild();
}
void ChatPanel::RailList::setSeeds (const juce::StringArray& s) { seeds = s; rebuild(); }
void ChatPanel::RailList::setDocs (const juce::StringArray& d) { docs = d; rebuild(); }
void ChatPanel::RailList::setMode (RailTab tab) { mode = tab; rebuild(); }

void ChatPanel::RailList::rebuild()
{
    int rows = 0;
    if (mode == RailTab::Runs) rows = (int) runs.size();
    else if (mode == RailTab::Seeds) rows = juce::jmax (1, seeds.size());
    else rows = docs.size();
    totalH = juce::jmax (48, rows * 44 + 12);
    setSize (getWidth() > 0 ? getWidth() : kRailW, totalH);
    repaint();
}

void ChatPanel::RailList::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (CustomLookAndFeel::txt2);

    if (mode == RailTab::Runs)
    {
        if (runs.empty())
        {
            g.setFont (CustomLookAndFeel::font (12.5f));
            g.drawFittedText ("Session runs appear here after you generate.",
                              getLocalBounds().reduced (16), juce::Justification::topLeft, 3);
            return;
        }
        float y = 4.0f;
        for (auto& run : runs)
        {
            auto row = juce::Rectangle<float> (0, y, r.getWidth(), 40.0f);
            g.setColour (CustomLookAndFeel::accent);
            g.fillEllipse (row.getX() + 14.0f, row.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setColour (CustomLookAndFeel::txt1);
            g.setFont (CustomLookAndFeel::mono (12.0f));
            g.drawText (run.seed, (int) row.getX() + 30, (int) row.getY(), 110, 40,
                        juce::Justification::centredLeft, true);
            g.setColour (CustomLookAndFeel::txt2.withAlpha (0.7f));
            g.setFont (CustomLookAndFeel::font (11.0f));
            g.drawText (run.harmony, (int) row.getX() + 140, (int) row.getY(),
                        (int) row.getWidth() - 190, 40, juce::Justification::centredLeft, true);
            g.drawText (run.timeLabel, (int) row.getRight() - 48, (int) row.getY(), 40, 40,
                        juce::Justification::centredRight, true);
            y += 42.0f;
        }
        return;
    }

    if (mode == RailTab::Seeds)
    {
        g.setFont (CustomLookAndFeel::font (12.5f));
        if (seeds.isEmpty())
        {
            g.drawFittedText ("Pinned seeds appear here. Pin a run to keep its seed for reuse.",
                              getLocalBounds().reduced (16), juce::Justification::topLeft, 4);
            return;
        }
        float y = 8.0f;
        for (auto& s : seeds)
        {
            g.setColour (CustomLookAndFeel::txt1);
            g.setFont (CustomLookAndFeel::mono (12.0f));
            g.drawText (s, 16, (int) y, getWidth() - 32, 28, juce::Justification::centredLeft);
            y += 36.0f;
        }
        return;
    }

    // Docs
    float y = 4.0f;
    if (docs.isEmpty())
    {
        g.setFont (CustomLookAndFeel::font (12.5f));
        g.drawFittedText ("No docs loaded. Use @ Docs or Brain to add theory.",
                          getLocalBounds().reduced (16), juce::Justification::topLeft, 3);
        return;
    }
    for (auto& d : docs)
    {
        g.setColour (CustomLookAndFeel::txt2);
        g.fillRoundedRectangle (14.0f, y + 12.0f, 10.0f, 12.0f, 2.0f);
        g.setColour (CustomLookAndFeel::txt1);
        g.setFont (CustomLookAndFeel::font (12.5f));
        g.drawText (d, 32, (int) y, getWidth() - 48, 36, juce::Justification::centredLeft, true);
        y += 40.0f;
    }
}

//==============================================================================
ChatPanel::ChatPanel()
{
    titleLabel.setText ("Chat", juce::dontSendNotification);
    titleLabel.setFont (CustomLookAndFeel::font (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (titleLabel);

    modelBadge.setFont (CustomLookAndFeel::font (11.5f));
    modelBadge.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    modelBadge.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modelBadge);

    newChatButton.setComponentID ("ghost");
    newChatButton.setTooltip ("New chat");
    newChatButton.onClick = [this] { clearConversation(); };
    addAndMakeVisible (newChatButton);

    viewport.setViewedComponent (&thread, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    auto wireTip = [this] (juce::TextButton& b, juce::String prompt)
    {
        b.setComponentID ("outline");
        b.onClick = [this, prompt] { pushQuick (prompt); };
        addAndMakeVisible (b);
    };
    wireTip (tipMake, "make a full 8-bar tech house loop with chords, bass, melody, and drums in the project key");
    wireTip (tipVary, "vary this");
    wireTip (tipBass, "make a groovy tech house bassline in the project key, 8 bars");
    wireTip (tipTheory, "make a memorable house topline melody over the current key, 8 bars");

    auto wireChip = [this] (juce::TextButton& b)
    {
        b.setComponentID ("ghost");
        addAndMakeVisible (b);
    };
    attachMidiButton.setTooltip ("Attach current part as @ MIDI context");
    attachMidiButton.onClick = [this] { toggleAttachMidi(); };
    wireChip (attachMidiButton);

    addDocsButton.setTooltip ("Add docs to brain");
    addDocsButton.onClick = [this] { if (onAddDocs) onAddDocs(); };
    wireChip (addDocsButton);

    openDocsButton.setTooltip ("Open brain folder");
    openDocsButton.onClick = [this] { if (onShowDocsFolder) onShowDocsFolder(); };
    wireChip (openDocsButton);

    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Message ComposerAI…",
                                  CustomLookAndFeel::txt2.withAlpha (0.65f));
    input.setFont (CustomLookAndFeel::font (14.0f));
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.setIndents (4, 8);
    input.onReturnKey = [this] { fireSend(); };
    addAndMakeVisible (input);

    sendButton.setComponentID ("primary");
    sendButton.setTooltip ("Send");
    sendButton.onClick = [this] { fireSend(); };
    addAndMakeVisible (sendButton);

    contextChip.setFont (CustomLookAndFeel::font (11.5f));
    contextChip.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    addAndMakeVisible (contextChip);

    auto wireTab = [this] (juce::TextButton& b, RailTab t)
    {
        b.setComponentID ("railTab");
        b.setClickingTogglesState (true);
        b.setRadioGroupId (77001);
        b.onClick = [this, t] { setRailTab (t); };
        addAndMakeVisible (b);
    };
    wireTab (runsTab, RailTab::Runs);
    wireTab (seedsTab, RailTab::Seeds);
    wireTab (docsTab, RailTab::Docs);
    runsTab.setToggleState (true, juce::dontSendNotification);

    railViewport.setViewedComponent (&railList, false);
    railViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (railViewport);

    railFooter.setFont (CustomLookAndFeel::font (11.0f));
    railFooter.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2.withAlpha (0.7f));
    railFooter.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (railFooter);

    setModelLabel (modelNameText);
    setDocsStatus ("No theory docs");
    refreshRail();
    updateSuggestionVisibility();
}

ChatPanel::~ChatPanel()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
    railViewport.setViewedComponent (nullptr, false);
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

void ChatPanel::addSessionRun (const SessionRun& run)
{
    sessionRuns.insert (sessionRuns.begin(), run);
    if (sessionRuns.size() > 24)
        sessionRuns.resize (24);
    refreshRail();
}

void ChatPanel::setDocTitles (const juce::StringArray& titles)
{
    docTitles = titles;
    addDocsButton.setButtonText ("@ Docs · " + juce::String (titles.size()));
    refreshRail();
}

void ChatPanel::setDocsStatus (const juce::String& status)
{
    docsStatusText = status;
    // Keep chip label in sync when count unknown
    if (docTitles.isEmpty() && status.containsIgnoreCase ("doc"))
        addDocsButton.setButtonText ("@ Docs");
}

void ChatPanel::setModelLabel (const juce::String& modelName)
{
    modelNameText = modelName.isNotEmpty() ? modelName : "model";
    modelBadge.setText ("Connected · " + modelNameText, juce::dontSendNotification);
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
    updateSuggestionVisibility();
    refreshThreadLayout();
    repaint();
}

void ChatPanel::setRailTab (RailTab t)
{
    railTab = t;
    runsTab.setToggleState (t == RailTab::Runs, juce::dontSendNotification);
    seedsTab.setToggleState (t == RailTab::Seeds, juce::dontSendNotification);
    docsTab.setToggleState (t == RailTab::Docs, juce::dontSendNotification);
    refreshRail();
    repaint();
}

void ChatPanel::refreshRail()
{
    railList.setMode (railTab);
    railList.setRuns (sessionRuns);
    railList.setSeeds (pinnedSeeds);
    railList.setDocs (docTitles);
    railFooter.setText (juce::String ((int) sessionRuns.size()) + " runs this session",
                        juce::dontSendNotification);
    railList.setSize (juce::jmax (100, railViewport.getWidth()), railList.contentHeight());
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
    {
        t << "\n\n[ATTACHED MIDI CONTEXT]\n" << attachedMidiContext;
        clearMidiAttachment(); // context rides along once, not on every message
    }

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
    // Only auto-scroll when the user was already at (or near) the bottom —
    // don't yank the view away from someone reading scrollback.
    const bool wasNearBottom =
        viewport.getViewPositionY() + viewport.getViewHeight()
            >= thread.getHeight() - 48;

    const int w = juce::jmax (200, viewport.getWidth());
    thread.layoutBubbles (w);

    if (wasNearBottom)
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

    // Frosted chat column
    if (! chatColumnBounds.isEmpty())
    {
        auto r = chatColumnBounds.toFloat();
        g.setColour (juce::Colour (0xff1d1a19).withAlpha (0.55f));
        g.fillRoundedRectangle (r, kGlassR);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r, kGlassR, 1.0f);
    }

    // Frosted rail
    if (! railBounds.isEmpty())
    {
        auto r = railBounds.toFloat();
        g.setColour (juce::Colour (0xff1d1a19).withAlpha (0.55f));
        g.fillRoundedRectangle (r, kGlassR);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r, kGlassR, 1.0f);
    }

    // Connected badge pill
    auto badge = modelBadge.getBounds().toFloat().expanded (8.0f, 4.0f);
    if (badge.getWidth() > 20.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (badge, 999.0f);
        g.setColour (CustomLookAndFeel::ok.withAlpha (0.95f));
        g.fillEllipse (badge.getX() + 10.0f, badge.getCentreY() - 3.0f, 6.0f, 6.0f);
    }

    // Empty welcome
    if (! hasUserMessage && ! busy && thread.contentHeight() <= 64)
    {
        auto mid = chatColumnBounds.reduced (24).withSizeKeepingCentre (
            juce::jmin (kMaxChatInner, chatColumnBounds.getWidth() - 48), 80);
        g.setColour (CustomLookAndFeel::txt1);
        g.setFont (CustomLookAndFeel::font (22.0f, juce::Font::bold));
        g.drawText ("How can I help you today?", mid.removeFromTop (36),
                    juce::Justification::centred);
        g.setColour (CustomLookAndFeel::txt2);
        g.setFont (CustomLookAndFeel::font (13.0f));
        g.drawText ("Describe a loop, bassline, or vibe — I'll generate MIDI.",
                    mid, juce::Justification::centred);
    }

    // Pill composer
    if (! composerBounds.isEmpty())
    {
        auto r = composerBounds.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 999.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r, 999.0f, 1.0f);
    }
}

void ChatPanel::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto header = r.removeFromTop (44);
    titleLabel.setBounds (header.removeFromLeft (52));
    newChatButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (48, 28));
    header.removeFromRight (8);
    modelBadge.setBounds (header.removeFromRight (juce::jmin (260, header.getWidth()))
                              .withSizeKeepingCentre (juce::jmin (240, header.getWidth() - 8), 28));

    r.removeFromTop (8);

    const bool showRail = r.getWidth() > 720;
    if (showRail)
    {
        railBounds = r.removeFromRight (kRailW);
        r.removeFromRight (12);
    }
    else
    {
        railBounds = {};
    }
    chatColumnBounds = r;

    // Rail internals
    runsTab.setVisible (showRail);
    seedsTab.setVisible (showRail);
    docsTab.setVisible (showRail);
    railViewport.setVisible (showRail);
    railFooter.setVisible (showRail);

    if (showRail)
    {
        auto rail = railBounds.reduced (10);
        auto tabs = rail.removeFromTop (40);
        const int tw = tabs.getWidth() / 3;
        runsTab.setBounds (tabs.removeFromLeft (tw).reduced (2, 4));
        seedsTab.setBounds (tabs.removeFromLeft (tw).reduced (2, 4));
        docsTab.setBounds (tabs.reduced (2, 4));

        railFooter.setBounds (rail.removeFromBottom (32).reduced (6, 4));
        railViewport.setBounds (rail);
        refreshRail();
    }

    // Chat column
    auto col = chatColumnBounds.reduced (8, 8);

    const bool hasChip = attachedMidiContext.isNotEmpty();
    auto composerBlock = col.removeFromBottom (hasChip ? 108 : 88);
    chipRowBounds = composerBlock.removeFromTop (34);

    attachMidiButton.setBounds (chipRowBounds.removeFromLeft (88).reduced (0, 4));
    chipRowBounds.removeFromLeft (6);
    addDocsButton.setBounds (chipRowBounds.removeFromLeft (110).reduced (0, 4));
    chipRowBounds.removeFromLeft (6);
    openDocsButton.setBounds (chipRowBounds.removeFromLeft (72).reduced (0, 4));

    if (hasChip)
    {
        contextChip.setBounds (composerBlock.removeFromTop (18).reduced (8, 0));
        composerBlock.removeFromTop (4);
    }
    else
    {
        contextChip.setBounds ({});
    }

    composerBounds = composerBlock.reduced (4, 2);
    auto cin = composerBounds.reduced (10, 6);
    sendButton.setBounds (cin.removeFromRight (38).withSizeKeepingCentre (38, 38));
    cin.removeFromRight (8);
    input.setBounds (cin);

    col.removeFromBottom (8);

    if (tipMake.isVisible())
    {
        auto tips = col.removeFromBottom (36);
        // Center tips within max chat width
        const int tipsW = juce::jmin (kMaxChatInner, tips.getWidth());
        tips = tips.withSizeKeepingCentre (tipsW, tips.getHeight());
        const int gap = 8;
        const int tw = (tips.getWidth() - gap * 3) / 4;
        tipMake.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipBass.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipVary.setBounds (tips.removeFromLeft (tw));
        tips.removeFromLeft (gap);
        tipTheory.setBounds (tips);
        col.removeFromBottom (8);
    }
    else
    {
        tipMake.setBounds ({});
        tipBass.setBounds ({});
        tipVary.setBounds ({});
        tipTheory.setBounds ({});
    }

    viewport.setBounds (col);
    refreshThreadLayout();
}

} // namespace aimidi
