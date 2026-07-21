#include "DrumKitPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

DrumKitPanel::DrumKitPanel()
{
    title.setText ("Drums", juce::dontSendNotification);
    title.setFont (juce::Font (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    for (int i = 0; i < (int) DrumPiece::NumPieces; ++i)
    {
        auto& row = rows[(size_t) i];
        row.piece = (DrumPiece) i;

        row.label.setText (toString (row.piece), juce::dontSendNotification);
        row.label.setFont (juce::Font (12.0f));
        addAndMakeVisible (row.label);

        row.generateBtn.onClick = [this, i]
        { if (onGeneratePiece) onGeneratePiece ((DrumPiece) i); };
        addAndMakeVisible (row.generateBtn);

        row.lockBtn.setClickingTogglesState (true);
        row.lockBtn.onClick = [this, i]
        { if (onLockChanged) onLockChanged ((DrumPiece) i, rows[(size_t) i].lockBtn.getToggleState()); };
        addAndMakeVisible (row.lockBtn);

        row.muteBtn.setClickingTogglesState (true);
        row.muteBtn.onClick = [this, i]
        { if (onMuteChanged) onMuteChanged ((DrumPiece) i, rows[(size_t) i].muteBtn.getToggleState()); };
        addAndMakeVisible (row.muteBtn);

        row.dragBtn.onClick = [this, i] { startPieceDrag ((DrumPiece) i); };
        addAndMakeVisible (row.dragBtn);
    }

    generateAllBtn.onClick = [this] { if (onGenerateAll) onGenerateAll(); };
    addAndMakeVisible (generateAllBtn);

    dragAllBtn.onClick = [this] { startFullKitDrag(); };
    addAndMakeVisible (dragAllBtn);

    for (auto& row : rows)
        setPieceHasContent (row.piece, false);
}

void DrumKitPanel::setPieceHasContent (DrumPiece piece, bool has)
{
    auto& row = rows[(size_t) piece];
    row.hasContent = has;
    row.dragBtn.setEnabled (has);

    fullKitHasContent = false;
    for (auto& r : rows)
        if (r.hasContent) { fullKitHasContent = true; break; }
    dragAllBtn.setEnabled (fullKitHasContent);

    repaint();
}

void DrumKitPanel::startPieceDrag (DrumPiece piece)
{
    auto& row = rows[(size_t) piece];
    if (! row.hasContent || ! requestPieceMidiFile) return;
    auto file = requestPieceMidiFile (piece);
    if (! file.existsAsFile()) return;

    juce::StringArray files;
    files.add (file.getFullPathName());

    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        dnd->performExternalDragDropOfFiles (files, /*canMove*/ false, this);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

void DrumKitPanel::startFullKitDrag()
{
    if (! fullKitHasContent || ! requestFullKitMidiFile) return;
    auto file = requestFullKitMidiFile();
    if (! file.existsAsFile()) return;

    juce::StringArray files;
    files.add (file.getFullPathName());

    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        dnd->performExternalDragDropOfFiles (files, /*canMove*/ false, this);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

void DrumKitPanel::paint (juce::Graphics& g)
{
    g.setColour (CustomLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f);

    auto dot = getLocalBounds().reduced (10).removeFromTop (18).removeFromRight (14);
    g.setColour (fullKitHasContent ? CustomLookAndFeel::accent2
                                   : CustomLookAndFeel::text.withAlpha (0.2f));
    g.fillEllipse (dot.toFloat().withSizeKeepingCentre (10, 10));
}

void DrumKitPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    title.setBounds (r.removeFromTop (20));
    r.removeFromTop (4);

    const int bottomH = 26;
    const int rowH = juce::jmax (16, (r.getHeight() - bottomH - 4) / (int) rows.size());

    for (auto& row : rows)
    {
        auto rr = r.removeFromTop (rowH);
        row.label.setBounds (rr.removeFromLeft (juce::jmax (46, rr.getWidth() / 4)));
        rr.removeFromLeft (4);
        const int btnW = juce::jmax (24, rr.getWidth() / 3 - 3);
        row.generateBtn.setBounds (rr.removeFromLeft (btnW));
        rr.removeFromLeft (3);
        row.lockBtn.setBounds (rr.removeFromLeft (btnW));
        rr.removeFromLeft (3);
        row.muteBtn.setBounds (rr.removeFromLeft (btnW));
        rr.removeFromLeft (3);
        row.dragBtn.setBounds (rr);
        r.removeFromTop (2);
    }

    r.removeFromTop (2);
    auto bottom = r.removeFromTop (bottomH);
    generateAllBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
    bottom.removeFromLeft (6);
    dragAllBtn.setBounds (bottom);
}

} // namespace aimidi
