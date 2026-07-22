#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aimidi
{
/** shadcn/ui New York · zinc dark — full VST chrome matching the companion. */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- Theme tokens (exact companion mapping) ----------------------------
    static inline const juce::Colour bg0   { 0xff09090b }; // --background
    static inline const juce::Colour bg1   { 0xff09090b };
    static inline const juce::Colour bg2   { 0xff0c0c0e }; // raised card
    static inline const juce::Colour bg3   { 0xff27272a }; // --muted / secondary
    static inline const juce::Colour bg4   { 0xff3f3f46 }; // hover
    static inline const juce::Colour bg5   { 0xff52525b }; // pressed
    static inline const juce::Colour line  { 0xff27272a }; // --border
    static inline const juce::Colour line2 { 0xff3f3f46 };
    static inline const juce::Colour txt1  { 0xfffafafa }; // --foreground
    static inline const juce::Colour txt2  { 0xffa1a1aa }; // --muted-foreground
    static inline const juce::Colour txt3  { 0xff71717a };
    static inline const juce::Colour accent{ 0xfffafafa }; // --primary
    static inline const juce::Colour accentDim { 0xffe4e4e7 };
    static inline const juce::Colour success { 0xff22c55e };
    static inline const juce::Colour warn  { 0xfff59e0b };
    static inline const juce::Colour danger{ 0xffef4444 };
    static inline const juce::Colour userBubble { 0xff18181b };
    static inline const juce::Colour aiMuted    { 0xffa1a1aa };

    static inline const juce::Colour& bg       = bg1;
    static inline const juce::Colour& surface  = bg2;
    static inline const juce::Colour& surface2 = bg3;
    static inline const juce::Colour& surface3 = bg4;
    static inline const juce::Colour& divider  = line;
    static inline const juce::Colour& text     = txt1;
    static inline const juce::Colour& muted    = txt2;
    static inline const juce::Colour& accent2  = accent;

    static constexpr float radius = 8.0f;   // --radius 0.5rem
    static constexpr float radiusSm = 6.0f;
    static constexpr float radiusLg = 12.0f;
    static constexpr float radiusPill = 999.0f;

    CustomLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg1);
        setColour (juce::TextButton::buttonColourId,          bg3);
        setColour (juce::TextButton::buttonOnColourId,        bg1);
        setColour (juce::TextButton::textColourOnId,          txt1);
        setColour (juce::TextButton::textColourOffId,         txt1);
        setColour (juce::TextEditor::backgroundColourId,      bg0);
        setColour (juce::TextEditor::textColourId,            txt1);
        setColour (juce::TextEditor::outlineColourId,         line);
        setColour (juce::TextEditor::focusedOutlineColourId,  line2);
        setColour (juce::TextEditor::highlightColourId,       bg4.withAlpha (0.55f));
        setColour (juce::Label::textColourId,                 txt1);
        setColour (juce::ScrollBar::thumbColourId,            bg5);
        setColour (juce::ScrollBar::trackColourId,            bg0);
        setColour (juce::TooltipWindow::backgroundColourId,   bg2);
        setColour (juce::TooltipWindow::textColourId,         txt1);
        setColour (juce::TooltipWindow::outlineColourId,      line);

        setColour (juce::ComboBox::backgroundColourId,        bg0);
        setColour (juce::ComboBox::textColourId,              txt1);
        setColour (juce::ComboBox::outlineColourId,           line);
        setColour (juce::ComboBox::buttonColourId,            bg3);
        setColour (juce::ComboBox::arrowColourId,             txt2);
        setColour (juce::ComboBox::focusedOutlineColourId,    line2);
        setColour (juce::PopupMenu::backgroundColourId,       bg2);
        setColour (juce::PopupMenu::textColourId,             txt1);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bg3);
        setColour (juce::PopupMenu::highlightedTextColourId,  txt1);

        setColour (juce::Slider::backgroundColourId,          bg3);
        setColour (juce::Slider::trackColourId,               txt1);
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
        const bool hero    = id == "primary";
        const bool ghost   = id == "ghost";
        const bool outline = id == "outline";
        const bool icon    = id == "icon";
        const bool tab     = id == "tab";
        const bool on      = b.getToggleState();

        // shadcn TabsTrigger inside TabsList
        if (tab)
        {
            if (on)
            {
                g.setColour (bg1);
                g.fillRoundedRectangle (r.reduced (1.0f), radiusSm);
                // soft shadow like data-[state=active]:shadow
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawRoundedRectangle (r.reduced (1.0f).translated (0.0f, 0.5f), radiusSm, 1.0f);
            }
            else if (over || down)
            {
                g.setColour (bg4.withAlpha (0.35f));
                g.fillRoundedRectangle (r.reduced (1.0f), radiusSm);
            }
            return;
        }

        if (ghost || icon)
        {
            juce::Colour fill = juce::Colours::transparentBlack;
            if (down)      fill = bg3;
            else if (over) fill = bg3.withAlpha (0.65f);
            else if (on)   fill = bg3;
            g.setColour (fill);
            g.fillRoundedRectangle (r, radiusSm);
            return;
        }

        if (outline)
        {
            g.setColour (over || down ? bg3.withAlpha (0.55f) : juce::Colours::transparentBlack);
            g.fillRoundedRectangle (r, radiusSm);
            g.setColour (line);
            g.drawRoundedRectangle (r, radiusSm, 1.0f);
            return;
        }

        // default / secondary / primary
        juce::Colour fill = hero ? accent : (on ? bg4 : bg3);
        if (! b.isEnabled())
            fill = fill.withMultipliedAlpha (0.40f);
        else if (down)
            fill = hero ? accentDim : bg5;
        else if (over)
            fill = hero ? accentDim : bg4;

        g.setColour (fill);
        g.fillRoundedRectangle (r, radiusSm);

        if (! hero)
        {
            g.setColour (line);
            g.drawRoundedRectangle (r, radiusSm, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool, bool) override
    {
        const auto id = b.getComponentID();
        const bool hero = id == "primary";
        const bool tab = id == "tab";
        const bool on = b.getToggleState();
        g.setFont (font (tab ? 12.0f : 12.5f, (hero || (tab && on)) ? juce::Font::bold : juce::Font::plain));
        juce::Colour c = txt1;
        if (hero)
            c = bg0; // primary-foreground
        else if (tab && ! on)
            c = txt2;
        if (! b.isEnabled())
            c = txt2.withMultipliedAlpha (0.7f);
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 1),
                          juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, radiusSm);
        g.setColour (box.hasKeyboardFocus (true) ? line2
                                                 : box.findColour (juce::ComboBox::outlineColourId));
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

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& e) override
    {
        auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour (e.hasKeyboardFocus (true) ? line2 : line);
        g.drawRoundedRectangle (r, radiusSm, 1.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           const juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 1.5f,
                                             (float) width, 3.0f);
        g.setColour (bg3);
        g.fillRoundedRectangle (track, 1.5f);
        auto filled = track.withWidth (juce::jmax (3.0f, sliderPos - (float) x));
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, 1.5f);
        g.setColour (txt1);
        g.fillEllipse (sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (line);
        g.drawEllipse (sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f, 1.0f);
    }

    static juce::Font font (float size, int style = juce::Font::plain)
    {
        auto opts = juce::FontOptions()
                        .withName (juce::Font::getDefaultSansSerifFontName())
                        .withHeight (size);
        if (style == juce::Font::bold)
            opts = opts.withStyle ("Bold");
        return juce::Font (std::move (opts));
    }

    /** shadcn Card */
    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                           float corner = radius)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg2);
        g.fillRoundedRectangle (r, corner);
        g.setColour (line);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    /** shadcn muted well / TabsList tray */
    static void drawSegmentedTray (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg3);
        g.fillRoundedRectangle (r, radius);
    }

    /** Deep inset editor surface */
    static void drawInsetWell (juce::Graphics& g, juce::Rectangle<int> bounds, float corner = radius)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg0);
        g.fillRoundedRectangle (r, corner);
        g.setColour (line);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    /** Status badge pill (connected / offline) */
    static void drawBadge (juce::Graphics& g, juce::Rectangle<int> bounds,
                           const juce::String& label, bool live)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg3);
        g.fillRoundedRectangle (r, radiusSm);
        g.setColour (line);
        g.drawRoundedRectangle (r, radiusSm, 1.0f);

        auto dot = r.withWidth (6.0f).withHeight (6.0f)
                      .withCentre ({ r.getX() + 12.0f, r.getCentreY() });
        g.setColour (live ? success : danger);
        g.fillEllipse (dot);

        g.setColour (txt2);
        g.setFont (font (10.5f));
        g.drawText (label, bounds.withTrimmedLeft (22).reduced (4, 0),
                    juce::Justification::centredLeft, false);
    }

    static void fillBackdrop (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour (bg1);
        g.fillRect (bounds);
    }

    static void drawChatSurface (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setColour (bg1);
        g.fillRect (bounds);
    }

    static void drawComposerShell (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawInsetWell (g, bounds, radius);
    }

    static void drawDial (juce::Graphics& g, juce::Rectangle<float> area,
                          float normalised01, const juce::String& valueText,
                          const juce::String& caption)
    {
        const float start = juce::MathConstants<float>::pi * 1.20f;
        const float end = juce::MathConstants<float>::pi * 3.80f;
        const float t = juce::jlimit (0.0f, 1.0f, normalised01);
        juce::Path base, value;
        base.addCentredArc (area.getCentreX(), area.getCentreY(), 22.0f, 22.0f, 0.0f, start, end, true);
        value.addCentredArc (area.getCentreX(), area.getCentreY(), 22.0f, 22.0f, 0.0f,
                             start, start + (end - start) * t, true);
        g.setColour (line);
        g.strokePath (base, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (txt1);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (txt1);
        g.setFont (font (13.0f, juce::Font::bold));
        g.drawText (valueText, area.reduced (8.0f), juce::Justification::centred, false);
        g.setColour (txt2);
        g.setFont (font (10.0f));
        g.drawText (caption, area.translated (0.0f, 28.0f), juce::Justification::centred, false);
    }

    static juce::Colour colourForInstrument (int typeIndex)
    {
        static const juce::Colour tones[] = {
            juce::Colour (0xff3b82f6),
            juce::Colour (0xff22c55e),
            juce::Colour (0xfff59e0b),
            juce::Colour (0xffe4e4e7),
            juce::Colour (0xffa855f7),
            juce::Colour (0xffec4899),
            juce::Colour (0xff38bdf8),
        };
        return tones[juce::jlimit (0, 6, typeIndex)];
    }
};

} // namespace aimidi
