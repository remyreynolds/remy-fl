#include "MidiRollView.h"
#include "CustomLookAndFeel.h"
#include <cmath>

namespace aimidi
{

MidiRollView::MidiRollView()
{
    title.setText ("Piano roll", juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (title);

    subtitle.setText ("No notes yet", juce::dontSendNotification);
    subtitle.setFont (CustomLookAndFeel::font (11.5f));
    subtitle.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    subtitle.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (subtitle);
}

void MidiRollView::setLoopLengthBeats (double beats)
{
    loopBeats = juce::jmax (1.0, beats);
    repaint();
}

void MidiRollView::setNotes (std::vector<NoteDraw> n)
{
    notes = std::move (n);
    recomputePitchRange();
    subtitle.setText (notes.empty()
                          ? "No notes yet"
                          : (juce::String ((int) notes.size()) + " notes"),
                      juce::dontSendNotification);
    repaint();
}

void MidiRollView::setPlayheadBeats (double beats)
{
    if (std::abs (beats - playheadBeats) < 0.0005 && ((beats < 0.0) == (playheadBeats < 0.0)))
        return;
    playheadBeats = beats;
    repaint();
}

void MidiRollView::setFocusLabel (const juce::String& label)
{
    title.setText (label.isNotEmpty() ? label : "Piano roll", juce::dontSendNotification);
}

void MidiRollView::clear()
{
    notes.clear();
    playheadBeats = -1.0;
    subtitle.setText ("No notes yet", juce::dontSendNotification);
    repaint();
}

void MidiRollView::recomputePitchRange()
{
    if (notes.empty())
    {
        pitchMin = 36;
        pitchMax = 84;
        return;
    }

    pitchMin = 127;
    pitchMax = 0;
    for (auto& n : notes)
    {
        pitchMin = juce::jmin (pitchMin, n.pitch);
        pitchMax = juce::jmax (pitchMax, n.pitch);
    }
    pitchMin = juce::jmax (0, pitchMin - 2);
    pitchMax = juce::jmin (127, pitchMax + 2);
    if (pitchMax <= pitchMin) pitchMax = pitchMin + 12;
}

juce::Rectangle<float> MidiRollView::getNoteArea() const
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (24); // title
    r.removeFromTop (18); // legend
    r.removeFromTop (6);
    auto roll = r.toFloat().reduced (0.5f);
    return roll.reduced (1.0f).withTrimmedLeft (32.0f);
}

void MidiRollView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (20);
    title.setBounds (top.removeFromLeft (90));
    subtitle.setBounds (top);
}

void MidiRollView::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (24);

    auto legend = r.removeFromTop (18);
    const char* names[] = { "Mel", "Chd", "Bass", "Drm", "CMel", "Arp", "Pad" };
    int x = legend.getX();
    g.setFont (CustomLookAndFeel::font (10.5f, juce::Font::bold));
    for (int i = 0; i < 7; ++i)
    {
        auto c = CustomLookAndFeel::colourForInstrument (i);
        g.setColour (c.withAlpha (0.70f));
        g.fillRoundedRectangle ((float) x, (float) legend.getY() + 5.0f, 6.0f, 6.0f, 2.0f);
        g.setColour (CustomLookAndFeel::txt2);
        g.drawText (names[i], x + 10, legend.getY(), 34, legend.getHeight(),
                    juce::Justification::centredLeft);
        x += 44;
    }

    r.removeFromTop (6);
    auto roll = r.toFloat().reduced (0.5f);

    // Deep inset grid — shadcn input/well
    CustomLookAndFeel::drawInsetWell (g, roll.toNearestInt(), CustomLookAndFeel::radiusSm);
    // Re-read float after border draw for note area
    g.setColour (CustomLookAndFeel::bg0);
    g.fillRoundedRectangle (roll.reduced (1.0f), CustomLookAndFeel::radiusSm - 1.0f);

    const float leftGutter = 32.0f;
    auto grid = roll.reduced (1.0f);
    auto noteArea = grid.withTrimmedLeft (leftGutter);

    const int pitchSpan = juce::jmax (1, pitchMax - pitchMin + 1);
    const float rowH = noteArea.getHeight() / (float) pitchSpan;
    const float beatW = (float) (noteArea.getWidth() / juce::jmax (1.0, loopBeats));

    for (int p = pitchMin; p <= pitchMax; ++p)
    {
        const int rowFromTop = pitchMax - p;
        const float y = noteArea.getY() + rowFromTop * rowH;
        const bool blackKey = (p % 12 == 1 || p % 12 == 3 || p % 12 == 6
                               || p % 12 == 8 || p % 12 == 10);

        g.setColour (blackKey ? CustomLookAndFeel::bg3.withAlpha (0.55f)
                              : CustomLookAndFeel::bg2.withAlpha (0.20f));
        g.fillRect (noteArea.getX(), y, noteArea.getWidth(), rowH);

        if (p % 12 == 0)
        {
            g.setColour (CustomLookAndFeel::txt2);
            g.setFont (CustomLookAndFeel::font (9.5f, juce::Font::bold));
            g.drawText ("C" + juce::String (p / 12 - 1),
                        (int) grid.getX(), (int) y, (int) leftGutter - 4, (int) rowH,
                        juce::Justification::centredRight);
        }
    }

    const int bars = juce::jmax (1, (int) std::round (loopBeats / 4.0));
    for (int b = 0; b <= (int) std::ceil (loopBeats); ++b)
    {
        const float xPos = noteArea.getX() + b * beatW;
        const bool isBar = (b % 4) == 0;
        g.setColour (isBar ? CustomLookAndFeel::line.brighter (0.15f)
                           : CustomLookAndFeel::line.withAlpha (0.55f));
        g.drawVerticalLine ((int) xPos, noteArea.getY(), noteArea.getBottom());
    }

    g.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    g.setColour (CustomLookAndFeel::txt2);
    for (int bar = 0; bar < bars; ++bar)
    {
        const float xPos = noteArea.getX() + bar * 4 * beatW + 4.0f;
        g.drawText (juce::String (bar + 1), (int) xPos, (int) noteArea.getY() + 2, 20, 12,
                    juce::Justification::centredLeft);
    }

    if (notes.empty())
    {
        g.setColour (CustomLookAndFeel::txt2);
        g.setFont (CustomLookAndFeel::font (12.5f));
        g.drawText ("Generate a part to populate the roll",
                    noteArea.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        for (auto& n : notes)
        {
            if (n.lengthBeats <= 0.0) continue;
            const int rowFromTop = pitchMax - n.pitch;
            float nx = noteArea.getX() + (float) n.startBeats * beatW;
            float nw = juce::jmax (2.5f, (float) n.lengthBeats * beatW - 1.0f);
            float ny = noteArea.getY() + rowFromTop * rowH + 1.0f;
            float nh = juce::jmax (2.0f, rowH - 2.0f);

            if (nx + nw < noteArea.getX() || nx > noteArea.getRight()) continue;
            nx = juce::jmax (nx, noteArea.getX());
            if (nx + nw > noteArea.getRight()) nw = noteArea.getRight() - nx;

            const bool underPlayhead = playheadBeats >= 0.0
                && playheadBeats >= n.startBeats
                && playheadBeats < n.startBeats + n.lengthBeats;

            auto col = CustomLookAndFeel::colourForInstrument (n.colourIndex);
            float a = n.muted ? 0.18f : (0.40f + 0.45f * n.velocity);
            if (underPlayhead && ! n.muted)
                a = juce::jmin (1.0f, a + 0.35f);

            g.setColour (col.withAlpha (a));
            g.fillRoundedRectangle (nx, ny, nw, nh, 2.0f);

            if (underPlayhead && ! n.muted)
            {
                g.setColour (CustomLookAndFeel::accent.withAlpha (0.85f));
                g.drawRoundedRectangle (nx, ny, nw, nh, 2.0f, 1.0f);
            }
        }
    }

    // FL-style playhead bar
    if (playheadBeats >= 0.0 && loopBeats > 0.0)
    {
        const float t = (float) (playheadBeats / loopBeats);
        const float px = noteArea.getX() + t * noteArea.getWidth();

        g.setColour (CustomLookAndFeel::accent.withAlpha (0.14f));
        g.fillRect (noteArea.getX(), noteArea.getY(),
                    juce::jmax (0.0f, px - noteArea.getX()), noteArea.getHeight());

        g.setColour (CustomLookAndFeel::accent);
        g.drawLine (px, noteArea.getY(), px, noteArea.getBottom(), 1.5f);

        juce::Path tip;
        const float topY = noteArea.getY();
        tip.addTriangle (px - 5.0f, topY, px + 5.0f, topY, px, topY + 7.0f);
        g.fillPath (tip);
    }
}

} // namespace aimidi
