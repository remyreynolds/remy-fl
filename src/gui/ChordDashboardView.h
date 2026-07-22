#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include "../engine/ChordAnalysis.h"
#include <array>
#include <vector>

namespace aimidi
{
/** Timeline strip showing chord names per harmonic part, with playhead highlight. */
class ChordDashboardView : public juce::Component
{
public:
    ChordDashboardView();

    void setLoopBars (int bars);
    void setPlayheadBeats (double beats); // < 0 hides highlight
    void setPartChords (InstrumentType type, const std::vector<ChordHit>& hits,
                        const juce::String& nowChord);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        InstrumentType type = InstrumentType::Chords;
        juce::String title;
        std::vector<ChordHit> hits;
        juce::String nowChord { "—" };
    };

    std::array<Row, 4> rows {};
    int bars = 4;
    double playhead = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordDashboardView)
};

} // namespace aimidi
