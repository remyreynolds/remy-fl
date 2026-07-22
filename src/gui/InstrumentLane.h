#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include "FileDragButton.h"
#include <functional>
#include <vector>

namespace aimidi
{
/** Slim selectable instrument lane for the Generate left rail (GAMEPLAN stack). */
class InstrumentLane : public juce::Component
{
public:
    explicit InstrumentLane (InstrumentType type);

    std::function<void()> onSelect;
    std::function<void()> onGenerate;
    std::function<void (bool)> onLockChanged;
    std::function<void (bool)> onMuteChanged;
    std::function<juce::File()> requestMidiFile;

    InstrumentType getType() const { return type; }
    void setSelected (bool shouldSelect);
    void setHasContent (bool has);
    void setLocked (bool locked);
    void setMuted (bool muted);
    /** Mini-roll thumbnail notes (beats within loop). */
    void setThumbnailNotes (std::vector<std::pair<double, double>> notes, double loopBeats);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int kHeight = 44;

private:
    InstrumentType type;
    juce::Label title;
    juce::TextButton generateBtn { "↻" };
    juce::TextButton lockBtn { "🔒" };
    juce::TextButton muteBtn { "M" };
    FileDragButton dragBtn { "⇄" };

    bool selected = false;
    bool hasContent = false;
    double loopBeats = 16.0;
    std::vector<std::pair<double, double>> thumbNotes; // start, length

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentLane)
};

} // namespace aimidi
