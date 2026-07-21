#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aimidi
{
/** Modern dark theme. Header-only. */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId,   panel);
        setColour (juce::TextButton::textColourOnId,    text);
        setColour (juce::TextButton::textColourOffId,   text);
        setColour (juce::TextEditor::backgroundColourId, panelDark);
        setColour (juce::TextEditor::textColourId,       text);
        setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,            text);
        setColour (juce::Slider::thumbColourId,          accent);
        setColour (juce::Slider::trackColourId,          accent.withAlpha (0.5f));
        setColour (juce::Slider::backgroundColourId,     panelDark);
        setColour (juce::ComboBox::backgroundColourId,   panelDark);
        setColour (juce::ComboBox::textColourId,         text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& colour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        auto c = colour;
        if (down) c = c.brighter (0.15f);
        else if (over) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        if (b.getToggleState())
        {
            g.setColour (accent);
            g.drawRoundedRectangle (r, 6.0f, 1.5f);
        }
    }

    static const juce::Colour bg, panel, panelDark, text, accent, accent2;
};

inline const juce::Colour CustomLookAndFeel::bg        { 0xff14161c };
inline const juce::Colour CustomLookAndFeel::panel     { 0xff21252e };
inline const juce::Colour CustomLookAndFeel::panelDark { 0xff0e1015 };
inline const juce::Colour CustomLookAndFeel::text      { 0xffe6e8ee };
inline const juce::Colour CustomLookAndFeel::accent    { 0xff6c7bff };
inline const juce::Colour CustomLookAndFeel::accent2   { 0xff37d99e };

} // namespace aimidi
