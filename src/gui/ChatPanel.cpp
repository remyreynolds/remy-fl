#include "ChatPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

ChatPanel::ChatPanel()
{
    transcript.setMultiLine (true);
    transcript.setReadOnly (true);
    transcript.setScrollbarsShown (true);
    transcript.setCaretVisible (false);
    transcript.setColour (juce::TextEditor::backgroundColourId, CustomLookAndFeel::panelDark);
    addAndMakeVisible (transcript);

    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Describe what you want… e.g. \"dark melodic house chords in F minor\"",
                                  CustomLookAndFeel::text.withAlpha (0.4f));
    input.onReturnKey = [this] { fireSend(); };
    addAndMakeVisible (input);

    sendButton.onClick = [this] { fireSend(); };
    addAndMakeVisible (sendButton);

    addAssistantMessage ("Hey — tell me the vibe and I'll generate the MIDI. "
                         "Try: \"summer deep house, 122 bpm, warm chords + bouncy bass\".");
}

void ChatPanel::append (const juce::String& who, const juce::String& text)
{
    if (transcript.getText().isNotEmpty())
        transcript.moveCaretToEnd(), transcript.insertTextAtCaret ("\n\n");
    transcript.moveCaretToEnd();
    transcript.insertTextAtCaret (who + ":  " + text);
    transcript.moveCaretToEnd();
}

void ChatPanel::fireSend()
{
    if (busy) return;
    const auto t = input.getText().trim();
    if (t.isEmpty()) return;
    addUserMessage (t);
    input.clear();
    if (onSend) onSend (t);
}

void ChatPanel::setBusy (bool b)
{
    busy = b;
    sendButton.setButtonText (b ? "…" : "Send");
    sendButton.setEnabled (! b);
    input.setEnabled (! b);
}

void ChatPanel::paint (juce::Graphics& g)
{
    g.setColour (CustomLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (CustomLookAndFeel::text.withAlpha (0.7f));
    g.setFont (juce::Font (14.0f, juce::Font::bold));
    g.drawText ("AI Chat", getLocalBounds().removeFromTop (24).reduced (10, 4),
                juce::Justification::centredLeft);
}

void ChatPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (22); // title
    auto bottom = r.removeFromBottom (34);
    sendButton.setBounds (bottom.removeFromRight (72));
    bottom.removeFromRight (6);
    input.setBounds (bottom);
    r.removeFromBottom (8);
    transcript.setBounds (r);
}

} // namespace aimidi
