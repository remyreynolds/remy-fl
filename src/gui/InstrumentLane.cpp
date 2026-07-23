#include "InstrumentLane.h"
#include "CustomLookAndFeel.h"
#include <cmath>

namespace aimidi
{

InstrumentLane::InstrumentLane (InstrumentType t) : type (t)
{
    title.setText (toString (t), juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (12.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    auto styleIcon = [] (juce::TextButton& b, const juce::String& tip)
    {
        b.setComponentID ("ghost");
        b.setTooltip (tip);
    };

    styleIcon (generateBtn, "Regenerate this part");
    generateBtn.onClick = [this]
    {
        if (onGenerate) onGenerate();
    };
    addAndMakeVisible (generateBtn);

    styleIcon (diceBtn, "Randomize / vary");
    diceBtn.onClick = [this]
    {
        if (onRandomize) onRandomize();
        else if (onGenerate) onGenerate();
    };
    addAndMakeVisible (diceBtn);

    muteBtn.setComponentID ("ghost");
    muteBtn.setClickingTogglesState (true);
    muteBtn.setTooltip ("Mute");
    muteBtn.onClick = [this]
    {
        // Spec: M toggled = accent fill / white text
        muteBtn.setComponentID (muteBtn.getToggleState() ? "primary" : "ghost");
        if (onMuteChanged) onMuteChanged (muteBtn.getToggleState());
        repaint();
    };
    addAndMakeVisible (muteBtn);

    soloBtn.setComponentID ("ghost");
    soloBtn.setClickingTogglesState (true);
    soloBtn.setTooltip ("Solo");
    soloBtn.onClick = [this]
    {
        // Spec: S toggled = #FF9F0A fill / dark text (painted in paint())
        soloBtn.setButtonText (soloBtn.getToggleState() ? "" : "S");
        if (onSoloChanged) onSoloChanged (soloBtn.getToggleState());
        repaint();
    };
    addAndMakeVisible (soloBtn);
}

void InstrumentLane::setSelected (bool shouldSelect)
{
    selected = shouldSelect;
    title.setColour (juce::Label::textColourId,
                     selected ? CustomLookAndFeel::txt1
                              : (hasContent ? juce::Colour (0xffD1D1D6) : CustomLookAndFeel::txt2));
    repaint();
}

void InstrumentLane::setHasContent (bool has)
{
    hasContent = has;
    title.setColour (juce::Label::textColourId,
                     selected ? CustomLookAndFeel::txt1
                              : (hasContent ? juce::Colour (0xffD1D1D6) : CustomLookAndFeel::txt2));
    repaint();
}

void InstrumentLane::setMuted (bool muted)
{
    muteBtn.setToggleState (muted, juce::dontSendNotification);
    muteBtn.setComponentID (muted ? "primary" : "ghost");
}

void InstrumentLane::setSoloed (bool soloed)
{
    soloBtn.setToggleState (soloed, juce::dontSendNotification);
    soloBtn.setButtonText (soloed ? "" : "S");
}

void InstrumentLane::setThumbnailNotes (std::vector<std::pair<double, double>> notes, double beats)
{
    thumbNotes = std::move (notes);
    loopBeats = juce::jmax (1.0, beats);
    repaint();
}

void InstrumentLane::mouseDown (const juce::MouseEvent& e)
{
    if (e.originalComponent == &generateBtn || e.originalComponent == &diceBtn
        || e.originalComponent == &muteBtn || e.originalComponent == &soloBtn)
        return;
    if (onSelect) onSelect();
}

void InstrumentLane::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    if (selected)
    {
        g.setColour (juce::Colour (0x14ffffff)); // rgba(255,255,255,.08)
        g.fillRoundedRectangle (bounds, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) bounds.getY() + 1, bounds.getX() + 6.0f, bounds.getRight() - 6.0f);
    }

    const auto col = CustomLookAndFeel::colourForInstrument ((int) type);

    auto dotArea = getLocalBounds().reduced (10, 0).removeFromLeft (10).withSizeKeepingCentre (8, 8);
    g.setColour (hasContent ? col : juce::Colour (0x24ffffff));
    g.fillEllipse (dotArea.toFloat());
    if (hasContent)
    {
        g.setColour (col.withAlpha (0.55f));
        g.drawEllipse (dotArea.toFloat().expanded (1.5f), 1.0f);
    }

    // Seeded-style mini bars (32 × 3px) in sunken well
    auto thumb = getLocalBounds().reduced (8, 11);
    thumb.removeFromLeft (78);
    thumb.removeFromRight (104);
    if (thumb.getWidth() > 24 && thumb.getHeight() > 8)
    {
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (thumb.toFloat(), 6.0f);

        if (hasContent)
        {
            const int bars = 32;
            const float cell = ((float) thumb.getWidth() - 10.0f) / (float) bars;
            for (int j = 0; j < bars; ++j)
            {
                // Spec: seeded sin heights 4..19px
                const double x = std::sin ((int) type * 127.1 + j * 311.7) * 43758.5;
                const double frac = x - std::floor (x);
                float h = 4.0f + (float) frac * 15.0f;
                if (! thumbNotes.empty())
                {
                    const double t0 = (double) j / (double) bars * loopBeats;
                    const double t1 = (double) (j + 1) / (double) bars * loopBeats;
                    bool hit = false;
                    for (auto& n : thumbNotes)
                        if (n.first < t1 && n.first + n.second > t0) { hit = true; break; }
                    if (! hit) h *= 0.35f;
                }
                const float bx = (float) thumb.getX() + 5.0f + (float) j * cell;
                g.setColour (col);
                g.fillRoundedRectangle (bx, (float) thumb.getCentreY() - h * 0.5f, 3.0f, h, 2.0f);
            }
        }
    }

    // Solo active paint overlay (orange chip)
    if (soloBtn.getToggleState())
    {
        auto s = soloBtn.getBounds().toFloat();
        g.setColour (juce::Colour (0xffFF9F0A));
        g.fillRoundedRectangle (s, 6.0f);
        g.setColour (juce::Colour (0xff1A1A1C));
        g.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
        g.drawText ("S", soloBtn.getBounds(), juce::Justification::centred, false);
    }
}

void InstrumentLane::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    r.removeFromLeft (14);
    title.setBounds (r.removeFromLeft (62));

    soloBtn.setBounds (r.removeFromRight (22));
    r.removeFromRight (4);
    muteBtn.setBounds (r.removeFromRight (22));
    r.removeFromRight (4);
    diceBtn.setBounds (r.removeFromRight (22));
    r.removeFromRight (4);
    generateBtn.setBounds (r.removeFromRight (22));
}

} // namespace aimidi
