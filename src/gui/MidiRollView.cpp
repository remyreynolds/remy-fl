#include "MidiRollView.h"
#include "CustomLookAndFeel.h"
#include <cmath>

namespace aimidi
{

MidiRollView::MidiRollView()
{
    title.setText ("Piano roll", juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (14.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (title);

    subtitle.setText ("No notes yet", juce::dontSendNotification);
    subtitle.setFont (CustomLookAndFeel::font (11.0f));
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

void MidiRollView::setGenerating (bool busy, const juce::String& trackName)
{
    generating = busy;
    generatingTrack = trackName;
    repaint();
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
    // Same insets as paint()/getNoteArea(): reduced(12) + 24px title strip.
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (24);
    title.setBounds (top.removeFromLeft (juce::jmin (280, top.getWidth() * 45 / 100)));
    auto badge = top.removeFromRight (72).withSizeKeepingCentre (72, 20);
    subtitle.setBounds (badge);
}

void MidiRollView::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (24);

    auto legend = r.removeFromTop (18);
    const char* names[] = { "Mel", "Chd", "Bass", "Drm", "CMel", "Arp", "Pad" };
    int x = legend.getX();
    g.setFont (CustomLookAndFeel::font (10.0f));
    for (int i = 0; i < 7; ++i)
    {
        auto c = CustomLookAndFeel::colourForInstrument (i);
        g.setColour (c);
        g.fillEllipse ((float) x, (float) legend.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (CustomLookAndFeel::txt2);
        g.drawText (names[i], x + 10, legend.getY(), 34, legend.getHeight(),
                    juce::Justification::centredLeft);
        x += 48;
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
    auto noteArea = getNoteArea(); // shared with resized() — single source of truth

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
            float a = n.muted ? 0.18f : (0.55f + 0.40f * n.velocity);
            if (underPlayhead && ! n.muted)
                a = juce::jmin (1.0f, a + 0.25f);

            juce::ColourGradient noteGrad (col.interpolatedWith (juce::Colours::white, 0.14f).withAlpha (a),
                                           nx, ny, col.withAlpha (a), nx, ny + nh, false);
            g.setGradientFill (noteGrad);
            g.fillRoundedRectangle (nx, ny, nw, nh, 3.0f);

            if (underPlayhead && ! n.muted)
            {
                g.setColour (CustomLookAndFeel::accent.withAlpha (0.90f));
                g.drawRoundedRectangle (nx, ny, nw, nh, 3.0f, 1.2f);
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

        // Accent glow on playhead (spec)
        g.setColour (CustomLookAndFeel::accent.withAlpha (0.35f));
        g.drawLine (px, noteArea.getY(), px, noteArea.getBottom(), 4.0f);
        g.setColour (CustomLookAndFeel::accent);
        g.drawLine (px, noteArea.getY(), px, noteArea.getBottom(), 1.5f);
    }

    if (generating)
    {
        g.setColour (juce::Colour (0xff131315).withAlpha (0.55f));
        g.fillRoundedRectangle (roll.reduced (1.0f), CustomLookAndFeel::radiusSm - 1.0f);

        // Shimmer band (1.1s loop)
        const double ms = juce::Time::getMillisecondCounterHiRes();
        const float phase = (float) (std::fmod (ms, 1100.0) / 1100.0);
        const float bandX = noteArea.getX() + (phase * 2.0f - 0.5f) * noteArea.getWidth();
        juce::ColourGradient shim (juce::Colours::transparentBlack, bandX - 80.0f, 0.0f,
                                   CustomLookAndFeel::accent.withAlpha (0.18f), bandX, 0.0f, false);
        shim.addColour (1.0, juce::Colours::transparentBlack);
        g.setGradientFill (shim);
        g.fillRect (noteArea);

        auto pill = juce::Rectangle<float> (0, 0, 220.0f, 40.0f)
                        .withCentre (noteArea.getCentre());
        g.setColour (juce::Colour (0xe61e1e21));
        g.fillRoundedRectangle (pill, 20.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (pill, 20.0f, 1.0f);

        g.setColour (CustomLookAndFeel::accent);
        const float spin = (float) (juce::Time::getMillisecondCounter() % 800) / 800.0f * juce::MathConstants<float>::twoPi;
        juce::Path arc;
        arc.addCentredArc (pill.getX() + 22.0f, pill.getCentreY(), 7.0f, 7.0f, 0.0f,
                           spin, spin + 4.2f, true);
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour (CustomLookAndFeel::txt1);
        g.setFont (CustomLookAndFeel::font (12.5f, juce::Font::bold));
        const auto name = generatingTrack.isNotEmpty() ? generatingTrack : juce::String ("track");
        g.drawText ("Composing " + name + "…",
                    pill.toNearestInt().withTrimmedLeft (36),
                    juce::Justification::centredLeft, false);
    }
}

} // namespace aimidi
