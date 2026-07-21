#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aimidi
{
/** Remy CRM-inspired dark SaaS theme. Header-only. */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId,   surface2);
        setColour (juce::TextButton::textColourOnId,    text);
        setColour (juce::TextButton::textColourOffId,   text);
        setColour (juce::TextEditor::backgroundColourId, surface2);
        setColour (juce::TextEditor::textColourId,       text);
        setColour (juce::TextEditor::outlineColourId,    divider);
        setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.75f));
        setColour (juce::Label::textColourId,            text);
        setColour (juce::Slider::thumbColourId,          accent);
        setColour (juce::Slider::trackColourId,          accent.withAlpha (0.5f));
        setColour (juce::Slider::backgroundColourId,     surface2);
        setColour (juce::ComboBox::backgroundColourId,   surface2);
        setColour (juce::ComboBox::textColourId,         text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& colour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? surface3 : colour;
        if (down) c = c.brighter (0.10f);
        else if (over) c = c.brighter (0.05f);
        g.setColour (c);
        g.fillRoundedRectangle (r, radius);
        g.setColour (b.getToggleState() ? accent.withAlpha (0.80f) : divider);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool, bool) override
    {
        g.setFont (font (12.5f, juce::Font::bold));
        g.setColour (b.isEnabled() ? text : muted);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 1),
                          juce::Justification::centred, 1);
    }

    static juce::Font font (float size, int style = juce::Font::plain)
    {
        return juce::Font ("Inter", size, style);
    }

    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (surface);
        g.fillRoundedRectangle (r, radius);
        g.setColour (divider);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    static const float radius;
    static const juce::Colour bg, surface, surface2, surface3, divider, text, muted, accent, accent2;
};

inline const float CustomLookAndFeel::radius           = 6.0f;
inline const juce::Colour CustomLookAndFeel::bg        { 0xff050608 };
inline const juce::Colour CustomLookAndFeel::surface   { 0xff0b0d10 };
inline const juce::Colour CustomLookAndFeel::surface2  { 0xff12151a };
inline const juce::Colour CustomLookAndFeel::surface3  { 0xff1a1e25 };
inline const juce::Colour CustomLookAndFeel::divider   { 0xff252a32 };
inline const juce::Colour CustomLookAndFeel::text      { 0xfff3f4f6 };
inline const juce::Colour CustomLookAndFeel::muted     { 0xff8a9099 };
inline const juce::Colour CustomLookAndFeel::accent    { 0xffd8e2ff };
inline const juce::Colour CustomLookAndFeel::accent2   { 0xfff0f5ff };

} // namespace aimidi
