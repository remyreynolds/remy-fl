#include "ChordDashboardView.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

ChordDashboardView::ChordDashboardView()
{
    rows[0] = { InstrumentType::Chords, "Chords", {}, "—" };
    rows[1] = { InstrumentType::Pad, "Pad", {}, "—" };
    rows[2] = { InstrumentType::Bass, "Bass", {}, "—" };
    rows[3] = { InstrumentType::Melody, "Melody", {}, "—" };
}

void ChordDashboardView::setLoopBars (int b)
{
    bars = juce::jmax (1, b);
    repaint();
}

void ChordDashboardView::setPlayheadBeats (double beats)
{
    playhead = beats;
    repaint();
}

void ChordDashboardView::setPartChords (InstrumentType type,
                                        const std::vector<ChordHit>& hits,
                                        const juce::String& nowChord)
{
    for (auto& row : rows)
    {
        if (row.type == type)
        {
            row.hits = hits;
            row.nowChord = nowChord.isNotEmpty() ? nowChord : "—";
            repaint();
            return;
        }
    }
}

void ChordDashboardView::resized() {}

void ChordDashboardView::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto r = getLocalBounds().reduced (10, 8);
    auto title = r.removeFromTop (18);
    g.setFont (CustomLookAndFeel::font (12.0f, juce::Font::bold));
    g.setColour (CustomLookAndFeel::txt1);
    g.drawText ("Chord dashboard", title.removeFromLeft (140),
                juce::Justification::centredLeft, false);

    g.setFont (CustomLookAndFeel::font (11.0f));
    g.setColour (CustomLookAndFeel::txt2);
    g.drawText ("What's sounding in each part · follow the playhead",
                title, juce::Justification::centredLeft, false);

    r.removeFromTop (6);

    const double loopBeats = (double) bars * 4.0;
    const int labelW = 64;
    const int nowW = 56;
    const int rowH = juce::jmax (22, r.getHeight() / (int) rows.size());

    for (auto& row : rows)
    {
        auto rowR = r.removeFromTop (rowH).reduced (0, 2);
        auto nameR = rowR.removeFromLeft (labelW);
        auto nowR  = rowR.removeFromRight (nowW);

        g.setColour (CustomLookAndFeel::txt2);
        g.setFont (CustomLookAndFeel::font (11.0f, juce::Font::bold));
        g.drawText (row.title, nameR, juce::Justification::centredLeft, false);

        // "now" chip — derived from playhead against chord hits
        juce::String nowName = row.nowChord;
        if (playhead >= 0.0 && ! row.hits.empty())
        {
            nowName = row.hits.front().name;
            for (size_t i = 0; i < row.hits.size(); ++i)
            {
                const double start = row.hits[i].startBeat;
                const double end = (i + 1 < row.hits.size())
                                       ? row.hits[i + 1].startBeat
                                       : loopBeats;
                if (playhead >= start && playhead < end)
                {
                    nowName = row.hits[i].name;
                    break;
                }
            }
        }

        g.setColour (CustomLookAndFeel::bg3);
        g.fillRoundedRectangle (nowR.toFloat().reduced (1.0f), 4.0f);
        g.setColour (CustomLookAndFeel::accent);
        g.setFont (CustomLookAndFeel::font (11.0f, juce::Font::bold));
        g.drawText (nowName, nowR, juce::Justification::centred, false);

        auto lane = rowR.reduced (4, 2).toFloat();
        g.setColour (CustomLookAndFeel::bg1);
        g.fillRoundedRectangle (lane, 4.0f);
        g.setColour (CustomLookAndFeel::line);
        g.drawRoundedRectangle (lane, 4.0f, 1.0f);

        // Bar grid
        g.setColour (CustomLookAndFeel::line.withAlpha (0.7f));
        for (int b = 1; b < bars; ++b)
        {
            const float x = lane.getX() + (float) (b * 4.0 / loopBeats) * lane.getWidth();
            g.drawVerticalLine ((int) x, lane.getY() + 1.0f, lane.getBottom() - 1.0f);
        }

        // Chord cells
        for (size_t i = 0; i < row.hits.size(); ++i)
        {
            const double start = row.hits[i].startBeat;
            const double end = (i + 1 < row.hits.size())
                                   ? row.hits[i + 1].startBeat
                                   : loopBeats;
            const float x0 = lane.getX() + (float) (start / loopBeats) * lane.getWidth();
            const float x1 = lane.getX() + (float) (end / loopBeats) * lane.getWidth();
            auto cell = juce::Rectangle<float> (x0 + 1.0f, lane.getY() + 2.0f,
                                                juce::jmax (8.0f, x1 - x0 - 2.0f),
                                                lane.getHeight() - 4.0f);

            const bool active = playhead >= 0.0
                             && playhead >= start
                             && playhead < end;

            g.setColour (active ? CustomLookAndFeel::accent.withAlpha (0.28f)
                                : CustomLookAndFeel::bg4);
            g.fillRoundedRectangle (cell, 3.0f);

            g.setColour (active ? CustomLookAndFeel::txt1 : CustomLookAndFeel::txt2);
            g.setFont (CustomLookAndFeel::font (10.5f, juce::Font::bold));
            g.drawText (row.hits[i].name, cell.toNearestInt(),
                        juce::Justification::centred, true);
        }

        // Playhead
        if (playhead >= 0.0 && loopBeats > 0.0)
        {
            const float px = lane.getX()
                + (float) (std::fmod (playhead, loopBeats) / loopBeats) * lane.getWidth();
            g.setColour (CustomLookAndFeel::accent);
            g.drawLine (px, lane.getY(), px, lane.getBottom(), 1.5f);
        }
    }
}

} // namespace aimidi
