#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include <functional>
#include <vector>

namespace aimidi
{
/** VST v4 track row — select / mini bars / regen · dice · M · S. */
class InstrumentLane : public juce::Component
{
public:
    explicit InstrumentLane (InstrumentType type);

    std::function<void()> onSelect;
    std::function<void()> onGenerate;
    std::function<void()> onRandomize;
    std::function<void (bool)> onMuteChanged;
    std::function<void (bool)> onSoloChanged;

    InstrumentType getType() const { return type; }
    void setSelected (bool shouldSelect);
    void setHasContent (bool has);
    void setMuted (bool muted);
    void setSoloed (bool soloed);
    void setThumbnailNotes (std::vector<std::pair<double, double>> notes, double loopBeats);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int kHeight = 46;

private:
    InstrumentType type;
    juce::Label title;
    juce::TextButton generateBtn { "↻" };
    juce::TextButton diceBtn { "⚄" };
    juce::TextButton muteBtn { "M" };
    juce::TextButton soloBtn { "S" };

    bool selected = false;
    bool hasContent = false;
    double loopBeats = 16.0;
    std::vector<std::pair<double, double>> thumbNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentLane)
};

} // namespace aimidi
