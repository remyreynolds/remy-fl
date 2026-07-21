#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include <array>
#include <functional>

namespace aimidi
{
/** Replaces the old monolithic "Drums" panel. Producers think in kick/snare/
    clap/hats, not one undifferentiated blob — so each piece gets its own
    Generate/Lock/Mute/Drag row, exactly like every other instrument, plus a
    "Generate All" / "Drag Full Loop" convenience pair for the whole kit. */
class DrumKitPanel : public juce::Component
{
public:
    DrumKitPanel();

    // Callbacks wired by the editor:
    std::function<void (DrumPiece)>       onGeneratePiece;
    std::function<void()>                 onGenerateAll;
    std::function<void (DrumPiece, bool)> onLockChanged;
    std::function<void (DrumPiece, bool)> onMuteChanged;
    // Returns a freshly-written temp .mid file to drag/export for one piece.
    std::function<juce::File (DrumPiece)> requestPieceMidiFile;
    // Returns a freshly-written temp .mid file for the combined kit loop.
    std::function<juce::File()>           requestFullKitMidiFile;

    void setPieceHasContent (DrumPiece piece, bool has);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct PieceRow
    {
        DrumPiece piece {};
        juce::Label      label;
        juce::TextButton generateBtn { "Gen" };
        juce::TextButton lockBtn     { "Lock" };
        juce::TextButton muteBtn     { "Mute" };
        juce::TextButton dragBtn     { "Drag" };
        bool hasContent = false;
    };

    void startPieceDrag (DrumPiece piece);
    void startFullKitDrag();

    juce::Label title;
    std::array<PieceRow, (size_t) DrumPiece::NumPieces> rows;
    juce::TextButton generateAllBtn { "Generate All" };
    juce::TextButton dragAllBtn     { "Drag Full Loop" };
    bool fullKitHasContent = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitPanel)
};

} // namespace aimidi
