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
    transcript.setFont (CustomLookAndFeel::font (12.5f));
    transcript.setColour (juce::TextEditor::backgroundColourId, CustomLookAndFeel::surface2);
    addAndMakeVisible (transcript);

    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Describe what you want… e.g. \"dark melodic house chords in F minor\"",
                                  CustomLookAndFeel::text.withAlpha (0.4f));
    input.setFont (CustomLookAndFeel::font (12.5f));
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
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto titleArea = getLocalBounds().removeFromTop (34);
    g.setColour (CustomLookAndFeel::text);
    g.setFont (CustomLookAndFeel::font (13.5f, juce::Font::bold));
    g.drawText ("AI Chat", titleArea.reduced (12, 6),
                juce::Justification::centredLeft);
    g.setColour (CustomLookAndFeel::divider);
    g.drawHorizontalLine (titleArea.getBottom(), 12.0f, (float) getWidth() - 12.0f);
}

void ChatPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (32); // title
    auto bottom = r.removeFromBottom (34);
    sendButton.setBounds (bottom.removeFromRight (68));
    bottom.removeFromRight (8);
    input.setBounds (bottom);
    r.removeFromBottom (10);
    transcript.setBounds (r);
}

} // namespace aimidi
