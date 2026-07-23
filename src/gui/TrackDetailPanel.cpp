#include "TrackDetailPanel.h"
#include "CustomLookAndFeel.h"
#include "../engine/PreviewSounds.h"

namespace aimidi
{

TrackDetailPanel::TrackDetailPanel()
{
    title.setFont (CustomLookAndFeel::font (16.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    trackTag.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    trackTag.setColour (juce::Label::textColourId, CustomLookAndFeel::txt3);
    trackTag.setJustificationType (juce::Justification::centredRight);
    trackTag.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (trackTag);

    auto styleFieldLabel = [] (juce::Label& l)
    {
        l.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, CustomLookAndFeel::txt3);
        l.setInterceptsMouseClicks (false, false);
    };
    styleFieldLabel (synthLabel);
    addAndMakeVisible (synthLabel);

    synthCombo.onChange = [this]
    {
        if (onTimbreChanged)
            onTimbreChanged (synthCombo.getSelectedId());
    };
    addAndMakeVisible (synthCombo);

    notesWell.setFont (CustomLookAndFeel::font (11.0f));
    notesWell.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    notesWell.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    notesWell.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (notesWell);

    volLabel.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    volLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    volLabel.setJustificationType (juce::Justification::centred);
    volLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (volLabel);

    generateBtn.setComponentID ("primary");
    generateBtn.onClick = [this] { if (onGenerate) onGenerate(); };
    addAndMakeVisible (generateBtn);

    auto styleSec = [] (juce::TextButton& b)
    {
        b.setComponentID ("ghost"); // secondary: no border, inset highlight
    };
    styleSec (varyBtn);
    styleSec (continueBtn);
    styleSec (lockBtn);
    styleSec (muteBtn);
    styleSec (exportBtn);

    varyBtn.onClick = [this] { if (onVary) onVary(); };
    continueBtn.onClick = [this] { if (onContinue) onContinue(); };
    lockBtn.setClickingTogglesState (true);
    lockBtn.onClick = [this] { if (onLockChanged) onLockChanged (lockBtn.getToggleState()); };
    muteBtn.setClickingTogglesState (true);
    muteBtn.onClick = [this] { if (onMuteChanged) onMuteChanged (muteBtn.getToggleState()); };
    exportBtn.onClick = [this]
    {
        if (onExport) onExport(); // editor opens a save dialog
    };

    addAndMakeVisible (varyBtn);
    addAndMakeVisible (continueBtn);
    addAndMakeVisible (lockBtn);
    addAndMakeVisible (muteBtn);
    addAndMakeVisible (exportBtn);

    dragBtn.setComponentID ("ghost");
    dragBtn.setTooltip ("Drag this track's MIDI into FL Studio");
    dragBtn.getFileToDrag = [this] () -> juce::File
    {
        if (! hasContent || ! requestMidiFile) return {};
        return requestMidiFile();
    };
    addAndMakeVisible (dragBtn);

    setVolume (0.72f);
}

void TrackDetailPanel::setTrack (InstrumentType t, bool has, const juce::String& notes)
{
    type = t;
    hasContent = has;
    notesLine = notes.isNotEmpty() ? notes : "—";
    title.setText (toString (t), juce::dontSendNotification);
    notesWell.setText (notesLine, juce::dontSendNotification);
    generateBtn.setButtonText (generating ? "Working…" : "Generate");
    dragBtn.setEnabled (has);
    repaint();
}

void TrackDetailPanel::setVolume (float gain01)
{
    volume = juce::jlimit (0.0f, 1.0f, gain01);
    volLabel.setText ("VOL " + juce::String ((int) std::round (volume * 100.0f)),
                      juce::dontSendNotification);
    repaint (knobBounds.toNearestInt().expanded (4));
}

void TrackDetailPanel::setLocked (bool locked)
{
    lockBtn.setToggleState (locked, juce::dontSendNotification);
}

void TrackDetailPanel::setMuted (bool muted)
{
    muteBtn.setToggleState (muted, juce::dontSendNotification);
}

void TrackDetailPanel::setGenerating (bool busy)
{
    generating = busy;
    generateBtn.setButtonText (busy ? "Working…" : "Generate");
    generateBtn.setEnabled (! busy);
}

void TrackDetailPanel::setTimbreOptions (const juce::StringArray& names, int selectedId)
{
    synthCombo.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        synthCombo.addItem (names[i], i + 1);
    if (selectedId > 0)
        synthCombo.setSelectedId (selectedId, juce::dontSendNotification);
    else if (names.size() > 0)
        synthCombo.setSelectedId (1, juce::dontSendNotification);
}

void TrackDetailPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (0, 8.0f, (float) getWidth() - 8.0f);

    const auto col = CustomLookAndFeel::colourForInstrument ((int) type);
    auto header = getLocalBounds().reduced (16, 0).removeFromTop (28).withTrimmedTop (12);
    auto dot = header.removeFromLeft (14).withSizeKeepingCentre (8, 8).toFloat();
    g.setColour (col);
    g.fillEllipse (dot);
    g.setColour (col.withAlpha (0.50f));
    g.drawEllipse (dot.expanded (2.0f), 1.0f);

    // Note well background
    auto notesR = notesWell.getBounds().toFloat().expanded (2.0f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (notesR, 8.0f);

    drawKnob (g, knobBounds);

    // Dashed drag target chrome behind FileDragButton
    auto dragR = dragBtn.getBounds().toFloat().reduced (0.5f);
    juce::Path outline;
    outline.addRoundedRectangle (dragR, 10.0f);
    juce::Path dashed;
    const float dashes[2] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.strokePath (dashed, juce::PathStrokeType (1.5f));
}

void TrackDetailPanel::drawKnob (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (area.isEmpty()) return;
    const float cx = area.getCentreX();
    const float cy = area.getCentreY() - 4.0f;
    const float r = 26.0f;
    const float start = juce::degreesToRadians (135.0f);
    const float sweep = juce::degreesToRadians (270.0f);
    const float t = volume;

    juce::Path base;
    base.addCentredArc (cx, cy, r, r, 0.0f, start, start + sweep, true);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.strokePath (base, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (cx, cy, r, r, 0.0f, start, start + sweep * t, true);
    g.setColour (CustomLookAndFeel::accent);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::ColourGradient face (juce::Colour (0xff3A3A3E), cx - 6.0f, cy - 8.0f,
                               juce::Colour (0xff232326), cx + 8.0f, cy + 10.0f, true);
    g.setGradientFill (face);
    g.fillEllipse (cx - 19.0f, cy - 19.0f, 38.0f, 38.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (cx - 19.0f, cy - 19.0f, 38.0f, 38.0f, 1.0f);

    const float ang = start + sweep * t - juce::MathConstants<float>::halfPi;
    // pointer from center toward top at 0 vol offset handled by start angle
    const float tipAng = juce::degreesToRadians (-135.0f + t * 270.0f);
    const float px = cx + std::sin (tipAng) * 15.5f;
    const float py = cy - std::cos (tipAng) * 15.5f;
    g.setColour (CustomLookAndFeel::txt1);
    g.drawLine (cx, cy, px, py, 2.5f);
}

void TrackDetailPanel::resized()
{
    // Too short (host squeezed the window) — hide controls instead of negative geometry.
    if (getHeight() < 160 || getWidth() < 120)
    {
        for (auto* c : getChildren())
            if (c != nullptr) c->setBounds ({});
        knobBounds = {};
        return;
    }

    auto r = getLocalBounds().reduced (16, 14);
    auto head = r.removeFromTop (22);
    trackTag.setBounds (head.removeFromRight (48));
    title.setBounds (head.withTrimmedLeft (16));

    r.removeFromTop (10);
    auto row = r.removeFromTop (juce::jmin (78, r.getHeight()));
    auto knobCol = row.removeFromLeft (76);
    knobBounds = knobCol.removeFromTop (juce::jmin (64, knobCol.getHeight())).toFloat();
    volLabel.setBounds (knobCol);

    auto fields = row;
    auto synthRow = fields.removeFromTop (juce::jmin (28, fields.getHeight()));
    synthLabel.setBounds (synthRow.removeFromLeft (40));
    synthCombo.setBounds (synthRow);
    if (fields.getHeight() > 8) fields.removeFromTop (8);
    notesWell.setBounds (fields.removeFromTop (juce::jmin (28, fields.getHeight())));

    if (r.getHeight() > 10) r.removeFromTop (10);
    auto grid = r.removeFromTop (juce::jmin (78, r.getHeight()));
    const int gap = 7;
    const int cellW = juce::jmax (1, (grid.getWidth() - gap * 2) / 3);
    const int cellH = juce::jmax (1, (grid.getHeight() - gap) / 2);
    juce::TextButton* btns[] = {
        &generateBtn, &varyBtn, &continueBtn, &lockBtn, &muteBtn, &exportBtn
    };
    for (int i = 0; i < 6; ++i)
    {
        const int col = i % 3;
        const int rowi = i / 3;
        btns[i]->setBounds (grid.getX() + col * (cellW + gap),
                            grid.getY() + rowi * (cellH + gap),
                            cellW, cellH);
    }

    if (r.getHeight() > 10) r.removeFromTop (10);
    dragBtn.setBounds (r.removeFromTop (juce::jmin (40, r.getHeight())));
}

void TrackDetailPanel::mouseDown (const juce::MouseEvent& e)
{
    if (knobBounds.expanded (6.0f).contains (e.position))
    {
        draggingKnob = true;
        dragStartVol = volume;
        dragStartY = e.getPosition().y;
    }
}

void TrackDetailPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingKnob) return;
    const float next = juce::jlimit (0.0f, 1.0f,
                                     dragStartVol + (float) (dragStartY - e.getPosition().y) * 0.006f);
    setVolume (next);
    if (onVolumeChanged) onVolumeChanged (volume);
}

void TrackDetailPanel::mouseUp (const juce::MouseEvent&)
{
    draggingKnob = false;
}

} // namespace aimidi
