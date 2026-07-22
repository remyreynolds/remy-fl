#include "InstrumentPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

InstrumentPanel::InstrumentPanel (InstrumentType t) : type (t)
{
    title.setText (toString (t), juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (13.5f, juce::Font::bold));
    addAndMakeVisible (title);

    generateBtn.onClick = [this] { if (onGenerate) onGenerate(); };
    addAndMakeVisible (generateBtn);

    lockBtn.setClickingTogglesState (true);
    lockBtn.onClick = [this] { if (onLockChanged) onLockChanged (lockBtn.getToggleState()); };
    addAndMakeVisible (lockBtn);

    muteBtn.setClickingTogglesState (true);
    muteBtn.onClick = [this] { if (onMuteChanged) onMuteChanged (muteBtn.getToggleState()); };
    addAndMakeVisible (muteBtn);

    dragBtn.onClick = [this] { startMidiDrag(); };
    addAndMakeVisible (dragBtn);

    exportBtn.onClick = [this]
    {
        if (! requestMidiFile) return;
        auto src = requestMidiFile();
        if (! src.existsAsFile()) return;
        auto chooser = std::make_shared<juce::FileChooser> (
            "Export MIDI", juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile (juce::String (toString (type)) + ".mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [src, chooser] (const juce::FileChooser& fc)
            {
                auto dst = fc.getResult();
                if (dst != juce::File()) src.copyFileTo (dst);
            });
    };
    addAndMakeVisible (exportBtn);

    setHasContent (false);
}

void InstrumentPanel::setHasContent (bool has)
{
    hasContent = has;
    dragBtn.setEnabled (has);
    exportBtn.setEnabled (has);
    repaint();
}

void InstrumentPanel::startMidiDrag()
{
    if (! hasContent || ! requestMidiFile) return;
    auto file = requestMidiFile();
    if (! file.existsAsFile()) return;

    juce::StringArray files;
    files.add (file.getFullPathName());

    // External OS drag → drops the .mid into FL Studio's playlist/piano roll.
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        dnd->performExternalDragDropOfFiles (files, /*canMove*/ false, this);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds().reduced (1));

    auto titleArea = getLocalBounds().reduced (10).removeFromTop (26);
    g.setColour (CustomLookAndFeel::divider);
    g.drawHorizontalLine (titleArea.getBottom() + 4, 10.0f, (float) getWidth() - 10.0f);

    auto dot = titleArea.removeFromRight (18);
    g.setColour (hasContent ? CustomLookAndFeel::accent2 : CustomLookAndFeel::muted.withAlpha (0.35f));
    g.fillEllipse (dot.toFloat().withSizeKeepingCentre (8, 8));
}

void InstrumentPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (10);

    auto row1 = r.removeFromTop (28);
    generateBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 4));
    row1.removeFromLeft (8);
    lockBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 4));
    row1.removeFromLeft (8);
    muteBtn.setBounds (row1);

    r.removeFromTop (8);
    auto row2 = r.removeFromTop (28);
    dragBtn.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 4));
    row2.removeFromLeft (8);
    exportBtn.setBounds (row2);
}

} // namespace aimidi
