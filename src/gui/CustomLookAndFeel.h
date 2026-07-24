#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace aimidi
{
/** ComposerAI VST v4 — SF Pro dark chrome, coral #FF5757 accent. */
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- Theme tokens (ComposerAI VST v4.dc.html) ---------------------------
    static inline const juce::Colour bg0   { 0xff131315 }; // deep inset / roll well
    static inline const juce::Colour bg1   { 0xff18181A }; // app ground mid
    static inline const juce::Colour bg2   { 0xff1F1F22 }; // card face
    static inline const juce::Colour bg3   { 0xff242428 }; // card top / raised
    static inline const juce::Colour bg4   { 0xff2E2E33 }; // hover
    static inline const juce::Colour bg5   { 0xff3A3A3E }; // pressed
    static inline const juce::Colour line  { 0x12ffffff }; // ~7% white
    static inline const juce::Colour line2 { 0x1Affffff }; // ~10% white
    static inline const juce::Colour txt1  { 0xffF5F5F7 };
    static inline const juce::Colour txt2  { 0xff98989D };
    static inline const juce::Colour txt3  { 0xff636366 };
    static inline const juce::Colour accent{ 0xffFF5757 }; // brand coral
    static inline const juce::Colour accentDim { 0xffE84848 };
    static inline const juce::Colour accentHi  { 0xffFF7A66 };
    static inline const juce::Colour success { 0xff30D158 };
    static inline const juce::Colour& ok = success;
    static inline const juce::Colour warn  { 0xffFF9F0A };
    static inline const juce::Colour danger{ 0xffFF375F };
    static inline const juce::Colour userBubble { 0xff242428 };
    static inline const juce::Colour aiMuted    { 0xff98989D };

    static inline const juce::Colour& bg       = bg1;
    static inline const juce::Colour& surface  = bg2;
    static inline const juce::Colour& surface2 = bg3;
    static inline const juce::Colour& surface3 = bg4;
    static inline const juce::Colour& divider  = line;
    static inline const juce::Colour& text     = txt1;
    static inline const juce::Colour& muted    = txt2;
    static inline const juce::Colour& accent2  = accent;

    static constexpr float radius = 14.0f;   // cards
    static constexpr float radiusSm = 9.0f;  // rows / controls
    static constexpr float radiusLg = 14.0f;
    static constexpr float radiusPill = 999.0f;
    static constexpr float radiusBtn = 10.0f;

    CustomLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg1);
        setColour (juce::TextButton::buttonColourId,          bg3);
        setColour (juce::TextButton::buttonOnColourId,        bg4);
        setColour (juce::TextButton::textColourOnId,          txt1);
        setColour (juce::TextButton::textColourOffId,         txt1);
        setColour (juce::TextEditor::backgroundColourId,      bg0);
        setColour (juce::TextEditor::textColourId,            txt1);
        setColour (juce::TextEditor::outlineColourId,         line);
        setColour (juce::TextEditor::focusedOutlineColourId,  accent.withAlpha (0.55f));
        setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.28f));
        setColour (juce::Label::textColourId,                 txt1);
        setColour (juce::ScrollBar::thumbColourId,            bg5);
        setColour (juce::ScrollBar::trackColourId,            bg0);
        setColour (juce::TooltipWindow::backgroundColourId,   bg2);
        setColour (juce::TooltipWindow::textColourId,         txt1);
        setColour (juce::TooltipWindow::outlineColourId,      line2);

        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (0x12ffffff));
        setColour (juce::ComboBox::textColourId,              txt1);
        setColour (juce::ComboBox::outlineColourId,           juce::Colours::transparentBlack);
        setColour (juce::ComboBox::buttonColourId,            bg3);
        setColour (juce::ComboBox::arrowColourId,             txt2);
        setColour (juce::ComboBox::focusedOutlineColourId,    accent.withAlpha (0.45f));
        setColour (juce::PopupMenu::backgroundColourId,       bg2);
        setColour (juce::PopupMenu::textColourId,             txt1);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bg4);
        setColour (juce::PopupMenu::highlightedTextColourId,  txt1);

        setColour (juce::Slider::backgroundColourId,          bg3);
        setColour (juce::Slider::trackColourId,               accent);
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
        const bool hero    = id == "primary" || id == "hero";
        const bool glow    = id == "hero"; // right-rail Generate <Track> only
        const bool ghost   = id == "ghost";
        const bool outline = id == "outline";
        const bool icon    = id == "icon";
        const bool tab     = id == "tab";
        const bool on      = b.getToggleState();

        if (tab)
        {
            if (on)
            {
                g.setColour (juce::Colour (0x21ffffff)); // ~13%
                g.fillRoundedRectangle (r.reduced (1.0f), 8.0f);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawRoundedRectangle (r.reduced (1.0f).translated (0.0f, 0.5f), 8.0f, 1.0f);
            }
            else if (over || down)
            {
                g.setColour (juce::Colour (0x14ffffff));
                g.fillRoundedRectangle (r.reduced (1.0f), 8.0f);
            }
            return;
        }

        if (ghost || icon)
        {
            // Toggle-on for ghost chrome stays quiet (v4 Host BPM / MIDI out).
            // Accent fill is reserved for hero Generate / Preview / primary actions.
            juce::Colour fill = juce::Colour (0x12ffffff);
            if (down)      fill = juce::Colour (0x24ffffff);
            else if (over) fill = juce::Colour (0x1cffffff);
            else if (on)   fill = juce::Colour (0x22ffffff);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 6.0f);
            return;
        }

        if (id == "railTab")
        {
            if (on)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (r, radiusPill);
            }
            else if (over || down)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r, radiusPill);
            }
            return;
        }

        if (outline)
        {
            g.setColour (over || down ? juce::Colour (0x21ffffff) : juce::Colour (0x12ffffff));
            g.fillRoundedRectangle (r, radiusBtn);
            g.setColour (juce::Colour (0x10ffffff));
            g.drawRoundedRectangle (r, radiusBtn, 1.0f);
            return;
        }

        if (hero)
        {
            juce::ColourGradient grad (accent.interpolatedWith (juce::Colours::white, 0.12f),
                                       r.getX(), r.getY(),
                                       accent, r.getX(), r.getBottom(), false);
            if (! b.isEnabled())
            {
                g.setColour (accent.withAlpha (0.35f));
                g.fillRoundedRectangle (r, radiusBtn);
            }
            else
            {
                if (glow)
                {
                    g.setColour (accent.withAlpha (0.45f));
                    g.fillRoundedRectangle (r.expanded (3.0f), radiusBtn + 2.0f);
                }
                if (down)      grad = juce::ColourGradient (accentDim, r.getX(), r.getY(), accentDim.darker (0.15f), r.getX(), r.getBottom(), false);
                else if (over) grad = juce::ColourGradient (accentHi, r.getX(), r.getY(), accent, r.getX(), r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, radiusBtn);
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
            }
            return;
        }

        juce::Colour fill = on ? bg4 : juce::Colour (0x12ffffff);
        if (! b.isEnabled())
            fill = fill.withMultipliedAlpha (0.40f);
        else if (down)
            fill = juce::Colour (0x24ffffff);
        else if (over)
            fill = juce::Colour (0x21ffffff);

        g.setColour (fill);
        g.fillRoundedRectangle (r, radiusBtn);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool, bool) override
    {
        const auto id = b.getComponentID();
        const bool hero = id == "primary" || id == "hero";
        const bool tab = id == "tab";
        const bool on = b.getToggleState();
        g.setFont (font (tab ? 13.0f : 12.5f, (hero || (tab && on)) ? juce::Font::bold : juce::Font::plain));
        juce::Colour c = txt1;
        if (hero)
            c = juce::Colours::white;
        else if (tab && ! on)
            c = txt2;
        else if (id == "ghost" && ! on)
            c = txt2;
        else if (id == "railTab")
            c = on ? juce::Colours::white : txt2;
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
        g.setColour (juce::Colour (0x12ffffff));
        g.fillRoundedRectangle (r, 8.0f);
        if (box.hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.45f));
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
        }

        const float arrowX = (float) width - 14.0f;
        const float arrowY = (float) height * 0.5f;
        juce::Path p;
        p.addTriangle (arrowX - 3.5f, arrowY - 1.5f,
                       arrowX + 3.5f, arrowY - 1.5f,
                       arrowX, arrowY + 3.0f);
        g.setColour (txt2);
        g.fillPath (p);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& e) override
    {
        auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour (e.hasKeyboardFocus (true) ? accent.withAlpha (0.45f) : line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           const juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 1.5f,
                                             (float) width, 3.0f);
        g.setColour (juce::Colour (0x14ffffff));
        g.fillRoundedRectangle (track, 1.5f);
        auto filled = track.withWidth (juce::jmax (3.0f, sliderPos - (float) x));
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, 1.5f);
        g.setColour (accent);
        g.fillEllipse (sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (bg0);
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

    static juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions()
                               .withName (juce::Font::getDefaultMonospacedFontName())
                               .withHeight (size));
    }

    /** VST v4 card surface */
    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                           float corner = radius)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        juce::ColourGradient grad (bg3, r.getX(), r.getY(),
                                   bg2, r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r, corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);
    }

    static void drawSegmentedTray (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRoundedRectangle (r.translated (0.0f, 0.5f), 10.0f, 1.0f);
    }

    static void drawInsetWell (juce::Graphics& g, juce::Rectangle<int> bounds, float corner = 10.0f)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (bg0);
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    /** Soft bloom (CSS box-shadow / text-shadow) — never a hard stroked ring. */
    static void drawSoftGlow (juce::Graphics& g, juce::Point<float> centre,
                              float coreRadius, juce::Colour colour, float strength = 1.0f)
    {
        // Layered discs approximate a gaussian blur glow.
        const float layers[][2] = {
            { 3.6f, 0.10f }, { 2.6f, 0.16f }, { 1.8f, 0.22f }, { 1.25f, 0.32f }
        };
        for (auto& layer : layers)
        {
            const float rad = coreRadius * layer[0];
            g.setColour (colour.withAlpha (layer[1] * strength));
            g.fillEllipse (centre.x - rad, centre.y - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    static void drawBadge (juce::Graphics& g, juce::Rectangle<int> bounds,
                           const juce::String& label, bool live)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (live ? success.withAlpha (0.10f) : juce::Colour (0x12ffffff));
        g.fillRoundedRectangle (r, radiusPill);
        g.setColour (live ? success.withAlpha (0.22f) : line2);
        g.drawRoundedRectangle (r, radiusPill, 1.0f);

        const juce::Point<float> centre { r.getX() + 14.0f, r.getCentreY() };
        // HTML: breathe 2.6s — opacity 1↔0.55, glow 6px@0.8 ↔ 2px@0.3
        const float phase = (float) std::sin (juce::Time::getMillisecondCounterHiRes()
                                              * (juce::MathConstants<double>::twoPi / 2600.0));
        const float breathe = live ? (0.55f + 0.45f * (0.5f + 0.5f * phase)) : 1.0f;

        if (live)
            drawSoftGlow (g, centre, 3.5f, success, 0.35f + 0.65f * breathe);

        auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre);
        g.setColour ((live ? success : danger).withAlpha (live ? breathe : 1.0f));
        g.fillEllipse (dot);

        g.setColour (live ? success : txt2);
        g.setFont (font (11.5f));
        g.drawText (label, bounds.withTrimmedLeft (24).reduced (4, 0),
                    juce::Justification::centredLeft, false);
    }

    static void fillBackdrop (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        juce::ColourGradient grad (juce::Colour (0xff222225), 0.0f, 0.0f,
                                   juce::Colour (0xff161618), 0.0f, (float) bounds.getHeight(), false);
        grad.addColour (0.40, juce::Colour (0xff18181A));
        g.setGradientFill (grad);
        g.fillRect (bounds);
        // Window chrome: border + inner top highlight
        auto r = bounds.toFloat().reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r, 16.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 12.0f, r.getRight() - 12.0f);
    }

    /** macOS-style title bar (42px) — traffic lights + centred title */
    static void drawTitleBar (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        juce::ColourGradient hg (juce::Colour (0x0fffffff), 0.0f, (float) bounds.getY(),
                                 juce::Colour (0x04ffffff), 0.0f, (float) bounds.getBottom(), false);
        g.setGradientFill (hg);
        g.fillRect (bounds);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (bounds.getBottom() - 1, 0.0f, (float) bounds.getRight());

        auto lights = bounds.withTrimmedLeft (16).removeFromLeft (52).withSizeKeepingCentre (52, 12);
        const juce::Colour cols[] = { juce::Colour (0xffFF5F57), juce::Colour (0xffFEBC2E),
                                      juce::Colour (0xff28C840) };
        for (int i = 0; i < 3; ++i)
        {
            auto d = juce::Rectangle<float> ((float) lights.getX() + (float) i * 20.0f,
                                             (float) lights.getCentreY() - 6.0f, 12.0f, 12.0f);
            g.setColour (cols[i]);
            g.fillEllipse (d);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.drawEllipse (d.reduced (0.5f), 1.0f);
        }

        g.setColour (txt2);
        g.setFont (font (13.0f, juce::Font::bold));
        g.drawText ("ComposerAI", bounds, juce::Justification::centred, false);
    }

    static void drawChatSurface (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        // Liquid-glass aurora ground (design 2a)
        g.setColour (juce::Colour (0xff171514));
        g.fillRect (bounds);

        auto r = bounds.toFloat();
        juce::ColourGradient a (accent.withAlpha (0.22f),
                                r.getX() + r.getWidth() * 0.12f, r.getY() - 20.0f,
                                juce::Colours::transparentBlack,
                                r.getX() + r.getWidth() * 0.45f, r.getY() + r.getHeight() * 0.55f,
                                true);
        g.setGradientFill (a);
        g.fillEllipse (r.getX() - 40.0f, r.getY() - 80.0f, 420.0f, 360.0f);

        juce::ColourGradient b (accent.withAlpha (0.12f),
                                r.getRight() - 40.0f, r.getBottom() + 20.0f,
                                juce::Colours::transparentBlack,
                                r.getX() + r.getWidth() * 0.55f, r.getY() + r.getHeight() * 0.4f,
                                true);
        g.setGradientFill (b);
        g.fillEllipse (r.getRight() - 380.0f, r.getBottom() - 320.0f, 460.0f, 380.0f);
    }

    static void drawComposerShell (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        drawInsetWell (g, bounds, radius);
    }

    static void drawBrandMark (juce::Graphics& g, juce::Rectangle<float> area,
                               juce::Colour keyColour = txt1,
                               juce::Colour accentColour = accent,
                               juce::Colour maskColour = juce::Colour (0xff1B1B1D))
    {
        if (area.getWidth() < 2.0f || area.getHeight() < 2.0f)
            return;

        const float s = juce::jmin (area.getWidth(), area.getHeight()) / 100.0f;
        const float ox = area.getCentreX() - 50.0f * s;
        const float oy = area.getCentreY() - 50.0f * s;

        auto key = [&] (float x, float y, float w, float h, float rx, juce::Colour c)
        {
            g.setColour (c);
            g.fillRoundedRectangle (ox + x * s, oy + y * s, w * s, h * s, rx * s);
        };

        key (14.0f, 22.0f, 13.2f, 56.0f, 3.4f, keyColour.withAlpha (0.92f));
        key (28.6f, 22.0f, 13.2f, 56.0f, 3.4f, accentColour);
        key (43.2f, 22.0f, 13.2f, 56.0f, 3.4f, keyColour.withAlpha (0.92f));
        key (57.8f, 22.0f, 13.2f, 56.0f, 3.4f, keyColour.withAlpha (0.92f));
        key (72.4f, 22.0f, 13.2f, 56.0f, 3.4f, accentColour);
        key (24.2f, 22.0f, 7.4f, 32.0f, 2.8f, maskColour);
        key (38.8f, 22.0f, 7.4f, 32.0f, 2.8f, maskColour);
        key (53.4f, 22.0f, 7.4f, 32.0f, 2.8f, accentColour);
        key (68.0f, 22.0f, 7.4f, 32.0f, 2.8f, maskColour);
    }

    static void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto f = font (juce::jlimit (16.0f, 22.0f, area.getHeight() * 0.78f), juce::Font::bold);
        g.setFont (f);
        const float composerW = (float) f.getStringWidth ("Composer");
        g.setColour (txt1);
        g.drawText ("Composer", area.withWidth (composerW + 2.0f),
                    juce::Justification::centredLeft, false);

        // CSS: text-shadow: 0 0 18px accent — soft bloom, not a solid circle
        auto aiArea = area.withTrimmedLeft (composerW);
        const float aiW = (float) f.getStringWidth ("AI");
        const juce::Point<float> glowCentre {
            aiArea.getX() + aiW * 0.5f,
            aiArea.getCentreY()
        };
        drawSoftGlow (g, glowCentre, 7.0f, accent, 0.85f);

        g.setColour (accent);
        g.setFont (font (juce::jlimit (16.0f, 22.0f, area.getHeight() * 0.78f), juce::Font::bold));
        g.drawText ("AI", aiArea, juce::Justification::centredLeft, false);
    }

    static void drawBrandLockup (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat();
        auto mark = r.removeFromLeft (r.getHeight()).reduced (1.0f);
        r.removeFromLeft (9.0f);
        drawBrandMark (g, mark);
        drawWordmark (g, r);
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
        g.setColour (juce::Colour (0x17ffffff));
        g.strokePath (base, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (txt1);
        g.setFont (font (13.0f, juce::Font::bold));
        g.drawText (valueText, area.reduced (8.0f), juce::Justification::centred, false);
        g.setColour (txt2);
        g.setFont (font (10.0f));
        g.drawText (caption, area.translated (0.0f, 28.0f), juce::Justification::centred, false);
    }

    /** Instrument palette from VST v4 mock */
    static juce::Colour colourForInstrument (int typeIndex)
    {
        static const juce::Colour tones[] = {
            juce::Colour (0xff0A84FF), // Melody
            juce::Colour (0xff30D158), // Chords
            juce::Colour (0xffFF9F0A), // Bass
            juce::Colour (0xff98989D), // Drums
            juce::Colour (0xffBF5AF2), // Counter
            juce::Colour (0xffFF375F), // Arp
            juce::Colour (0xff64D2FF), // Pad
        };
        return tones[juce::jlimit (0, 6, typeIndex)];
    }
};

} // namespace aimidi
