#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/MusicInstructions.h"
#include "../engine/SampleLibrary.h"
#include "FileDragButton.h"
#include <array>
#include <functional>
#include <vector>

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
    std::function<void (DrumPiece, float)> onVolumeChanged;
    std::function<void (DrumPiece, juce::String)> onSampleChanged; // empty id = synth
    // Returns a freshly-written temp .mid file to drag/export for one piece.
    std::function<juce::File (DrumPiece)> requestPieceMidiFile;
    // Returns a freshly-written temp .mid file for the combined kit loop.
    std::function<juce::File()>           requestFullKitMidiFile;

    void setPieceHasContent (DrumPiece piece, bool has);
    void setPieceVolume (DrumPiece piece, float gain01);
    void setSampleOptions (DrumPiece piece, const std::vector<const SampleEntry*>& options,
                           const juce::String& selectedId);

    bool isMinimized() const { return minimized; }
    void setMinimized (bool shouldMinimize);
    int preferredHeight() const { return minimized ? 34 : 220; }
    std::function<void()> onMinimizeChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void updateMinimizedUi();
    struct PieceRow
    {
        DrumPiece piece {};
        juce::Label      label;
        juce::Slider     volSlider;
        juce::ComboBox   sampleCombo;
        juce::TextButton generateBtn { "Gen" };
        juce::TextButton lockBtn     { "Lock" };
        juce::TextButton muteBtn     { "Mute" };
        FileDragButton   dragBtn     { "Drag" };
        bool hasContent = false;
        bool suppressVol = false;
        bool suppressSample = false;
        juce::StringArray sampleIds; // maps combo item index (after Synth) → sample id
    };

    juce::Label title;
    juce::TextButton minimizeBtn { "−" };
    std::array<PieceRow, (size_t) DrumPiece::NumPieces> rows;
    juce::TextButton generateAllBtn { "Generate All" };
    FileDragButton   dragAllBtn     { "Drag Full Loop" };
    bool fullKitHasContent = false;
    bool minimized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitPanel)
};

} // namespace aimidi
