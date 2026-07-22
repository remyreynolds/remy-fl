#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aimidi
{
/** Splice-instrument-inspired UI: blue-black studio surfaces, tight radii,
    hairline borders, one electric-blue accent. */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- Theme tokens (Splice dark) -----------------------------------------
    static inline const juce::Colour bg1   { 0xff101216 }; // app backdrop
    static inline const juce::Colour bg2   { 0xff171a1f }; // sidebar / panel
    static inline const juce::Colour bg3   { 0xff1f242b }; // control / composer
    static inline const juce::Colour bg4   { 0xff272d35 }; // hover
    static inline const juce::Colour bg5   { 0xff303741 }; // pressed / chip
    static inline const juce::Colour line  { 0xff2a3038 }; // hairline
    static inline const juce::Colour txt1  { 0xfff2f4f7 }; // primary
    static inline const juce::Colour txt2  { 0xff98a2b3 }; // secondary
    static inline const juce::Colour accent{ 0xff3d7eff }; // Splice electric blue
    static inline const juce::Colour userBubble { 0xff2a313c };
    static inline const juce::Colour aiMuted    { 0xffb4bcc9 };

    static inline const juce::Colour& bg       = bg1;
    static inline const juce::Colour& surface  = bg2;
    static inline const juce::Colour& surface2 = bg3;
    static inline const juce::Colour& surface3 = bg4;
    static inline const juce::Colour& divider  = line;
    static inline const juce::Colour& text     = txt1;
    static inline const juce::Colour& muted    = txt2;
    static inline const juce::Colour& accent2  = accent;

    static constexpr float radius = 8.0f;
    static constexpr float radiusSm = 6.0f;
    static constexpr float radiusLg = 12.0f;

    CustomLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg1);
        setColour (juce::TextButton::buttonColourId,          bg3);
        setColour (juce::TextButton::buttonOnColourId,        bg5);
        setColour (juce::TextButton::textColourOnId,          txt1);
        setColour (juce::TextButton::textColourOffId,         txt1);
        setColour (juce::TextEditor::backgroundColourId,      bg3);
        setColour (juce::TextEditor::textColourId,            txt1);
        setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colours::transparentBlack);
        setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.28f));
        setColour (juce::Label::textColourId,                 txt1);
        setColour (juce::ScrollBar::thumbColourId,            bg5);
        setColour (juce::ScrollBar::trackColourId,            bg2);
        setColour (juce::TooltipWindow::backgroundColourId,   bg3);
        setColour (juce::TooltipWindow::textColourId,         txt1);
        setColour (juce::TooltipWindow::outlineColourId,      line);

        setColour (juce::ComboBox::backgroundColourId,        bg3);
        setColour (juce::ComboBox::textColourId,              txt1);
        setColour (juce::ComboBox::outlineColourId,           line);
        setColour (juce::ComboBox::buttonColourId,            bg4);
        setColour (juce::ComboBox::arrowColourId,             txt2);
        setColour (juce::ComboBox::focusedOutlineColourId,    accent.withAlpha (0.45f));
        setColour (juce::PopupMenu::backgroundColourId,       bg3);
        setColour (juce::PopupMenu::textColourId,             txt1);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bg5);
        setColour (juce::PopupMenu::highlightedTextColourId,  txt1);

        setColour (juce::Slider::backgroundColourId,          bg1);
        setColour (juce::Slider::trackColourId,               accent.withAlpha (0.55f));
        setColour (juce::Slider::thumbColourId,               txt1);
        setColour (juce::Slider::textBoxTextColourId,         txt2);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto id = b.getComponentID();
        const bool hero  = id == "primary";
        const bool ghost = id == "ghost";
        const bool icon  = id == "icon";
        const bool on    = b.getToggleState();

        if (ghost || icon)
        {
            juce::Colour fill = juce::Colours::transparentBlack;
            if (down)      fill = bg5;
            else if (over) fill = bg4;
            else if (on)   fill = bg4;
            g.setColour (fill);
            g.fillRoundedRectangle (r, icon ? radiusSm : radius);
            return;
        }

        juce::Colour fill = hero ? accent : (on ? bg5 : bg3);
        if (! b.isEnabled())
            fill = fill.withMultipliedAlpha (0.40f);
        else if (down)
            fill = hero ? accent.darker (0.10f) : bg5;
        else if (over)
            fill = hero ? accent.brighter (0.06f) : bg4;

        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);

        if (! hero)
        {
            g.setColour (line);
            g.drawRoundedRectangle (r, radius, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool, bool) override
    {
        const auto id = b.getComponentID();
        const bool hero = id == "primary";
        g.setFont (font (12.5f, juce::Font::plain));
        g.setColour (b.isEnabled()
                         ? (hero ? juce::Colours::white : txt1)
                         : txt2.withMultipliedAlpha (0.7f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 1),
                          juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, radiusSm);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, radiusSm, 1.0f);

        const float arrowX = (float) width - 16.0f;
        const float arrowY = (float) height * 0.5f;
        juce::Path p;
        p.addTriangle (arrowX - 4.0f, arrowY - 2.0f,
                       arrowX + 4.0f, arrowY - 2.0f,
                       arrowX, arrowY + 3.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (p);
    }

    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}

    static juce::Font font (float size, int style = juce::Font::plain)
    {
        // Prefer system UI (Cursor/Claude feel on macOS)
        auto opts = juce::FontOptions()
                        .withName (juce::Font::getDefaultSansSerifFontName())
                        .withHeight (size);
        if (style == juce::Font::bold)
            opts = opts.withStyle ("Bold");
        return juce::Font (std::move (opts));
    }

    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                           float corner = radius)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg2);
        g.fillRoundedRectangle (r, corner);
        g.setColour (line.withAlpha (0.85f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    /** Flat chat surface — no hard card chrome. */
    static void drawChatSurface (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour (bg1);
        g.fillRect (bounds);
        g.setColour (line.withAlpha (0.6f));
        g.drawVerticalLine (bounds.getRight() - 1, (float) bounds.getY(), (float) bounds.getBottom());
    }

    static void fillBackdrop (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour (bg1);
        g.fillRect (bounds);
    }

    static void drawComposerShell (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg3);
        g.fillRoundedRectangle (r, radiusLg);
        g.setColour (line);
        g.drawRoundedRectangle (r, radiusLg, 1.0f);
    }

    static void drawDial (juce::Graphics& g, juce::Rectangle<float> area,
                          float normalised01, const juce::String& valueText,
                          const juce::String& caption)
    {
        const float start = juce::MathConstants<float>::pi * 1.20f;
        const float end   = juce::MathConstants<float>::pi * 3.80f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY() - 2.0f;
        const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.38f;

        juce::Path track, value;
        track.addCentredArc (cx, cy, rad, rad, 0.0f, start, end, true);
        value.addCentredArc (cx, cy, rad, rad, 0.0f, start,
                             start + (end - start) * juce::jlimit (0.0f, 1.0f, normalised01),
                             true);

        g.setColour (line);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (txt1);
        g.setFont (font (13.0f, juce::Font::bold));
        g.drawText (valueText, area.withSizeKeepingCentre (area.getWidth(), 18.0f)
                                   .translated (0.0f, -2.0f).toNearestInt(),
                    juce::Justification::centred);

        g.setColour (txt2);
        g.setFont (font (10.0f));
        g.drawText (caption, juce::Rectangle<float> (area.getX(), area.getBottom() - 16.0f,
                                                     area.getWidth(), 14.0f).toNearestInt(),
                    juce::Justification::centred);
    }

    static juce::Colour colourForInstrument (int typeIndex)
    {
        static const juce::Colour tones[] = {
            juce::Colour (0xff3d7eff), juce::Colour (0xff6ea8ff),
            juce::Colour (0xff9aa7bd), juce::Colour (0xff2dd4bf),
            juce::Colour (0xffc084fc), juce::Colour (0xfffbbf24),
            juce::Colour (0xfffb7185)
        };
        return tones[juce::jlimit (0, 6, typeIndex)];
    }
};

} // namespace aimidi
