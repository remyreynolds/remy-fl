#include "InstrumentLane.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

InstrumentLane::InstrumentLane (InstrumentType t) : type (t)
{
    title.setText (toString (t), juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (12.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    generateBtn.setComponentID ("ghost");
    generateBtn.setTooltip ("Regenerate this part");
    generateBtn.onClick = [this]
    {
        if (onGenerate) onGenerate();
    };
    addAndMakeVisible (generateBtn);

    lockBtn.setComponentID ("ghost");
    lockBtn.setClickingTogglesState (true);
    lockBtn.setTooltip ("Lock — skip on Generate all");
    lockBtn.onClick = [this]
    {
        if (onLockChanged) onLockChanged (lockBtn.getToggleState());
    };
    addAndMakeVisible (lockBtn);

    muteBtn.setComponentID ("ghost");
    muteBtn.setClickingTogglesState (true);
    muteBtn.setTooltip ("Mute in preview / MIDI out");
    muteBtn.onClick = [this]
    {
        if (onMuteChanged) onMuteChanged (muteBtn.getToggleState());
    };
    addAndMakeVisible (muteBtn);

    dragBtn.setComponentID ("ghost");
    dragBtn.setTooltip ("Drag MIDI into FL Studio");
    dragBtn.getFileToDrag = [this] () -> juce::File
    {
        if (! hasContent || ! requestMidiFile) return {};
        return requestMidiFile();
    };
    addAndMakeVisible (dragBtn);
}

void InstrumentLane::setSelected (bool shouldSelect)
{
    selected = shouldSelect;
    repaint();
}

void InstrumentLane::setHasContent (bool has)
{
    hasContent = has;
    dragBtn.setEnabled (has);
    repaint();
}

void InstrumentLane::setLocked (bool locked)
{
    lockBtn.setToggleState (locked, juce::dontSendNotification);
}

void InstrumentLane::setMuted (bool muted)
{
    muteBtn.setToggleState (muted, juce::dontSendNotification);
}

void InstrumentLane::setThumbnailNotes (std::vector<std::pair<double, double>> notes, double beats)
{
    thumbNotes = std::move (notes);
    loopBeats = juce::jmax (1.0, beats);
    repaint();
}

void InstrumentLane::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect();
}

void InstrumentLane::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? CustomLookAndFeel::bg3 : CustomLookAndFeel::bg2);
    g.fillRoundedRectangle (bounds, CustomLookAndFeel::radiusSm);
    g.setColour (selected ? CustomLookAndFeel::line2 : CustomLookAndFeel::line);
    g.drawRoundedRectangle (bounds, CustomLookAndFeel::radiusSm, 1.0f);

    if (selected)
    {
        g.setColour (CustomLookAndFeel::txt1);
        g.fillRoundedRectangle (bounds.removeFromLeft (3.0f).reduced (0.0f, 6.0f), 1.5f);
    }

    // Status dot
    auto dotArea = getLocalBounds().reduced (8, 0).removeFromLeft (14).withSizeKeepingCentre (7, 7);
    g.setColour (hasContent ? CustomLookAndFeel::success : CustomLookAndFeel::txt3);
    g.fillEllipse (dotArea.toFloat());

    // Mini-roll thumbnail strip
    auto thumb = getLocalBounds().reduced (10, 8);
    thumb.removeFromLeft (78);
    thumb.removeFromRight (118);
    if (thumb.getWidth() > 20 && thumb.getHeight() > 8)
    {
        g.setColour (CustomLookAndFeel::bg0);
        g.fillRoundedRectangle (thumb.toFloat(), 4.0f);
        g.setColour (CustomLookAndFeel::line);
        g.drawRoundedRectangle (thumb.toFloat(), 4.0f, 1.0f);

        const auto col = CustomLookAndFeel::colourForInstrument ((int) type);
        for (auto& n : thumbNotes)
        {
            const float x = thumb.getX() + (float) (n.first / loopBeats) * (float) thumb.getWidth();
            const float w = juce::jmax (2.0f, (float) (n.second / loopBeats) * (float) thumb.getWidth());
            g.setColour (col.withAlpha (0.85f));
            g.fillRoundedRectangle (x, (float) thumb.getY() + 3.0f, w, (float) thumb.getHeight() - 6.0f, 2.0f);
        }
    }
}

void InstrumentLane::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromLeft (14); // status dot
    title.setBounds (r.removeFromLeft (64));

    dragBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (2);
    muteBtn.setBounds (r.removeFromRight (26));
    r.removeFromRight (2);
    lockBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (2);
    generateBtn.setBounds (r.removeFromRight (28));
}

} // namespace aimidi
