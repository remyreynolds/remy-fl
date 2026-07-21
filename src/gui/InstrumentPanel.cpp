#include "InstrumentPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

InstrumentPanel::InstrumentPanel (InstrumentType t) : type (t)
{
    title.setText (toString (t), juce::dontSendNotification);
    title.setFont (juce::Font (15.0f, juce::Font::bold));
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
    g.setColour (CustomLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f);
    auto dot = getLocalBounds().reduced (10).removeFromTop (18).removeFromRight (14);
    g.setColour (hasContent ? CustomLookAndFeel::accent2
                            : CustomLookAndFeel::text.withAlpha (0.2f));
    g.fillEllipse (dot.toFloat().withSizeKeepingCentre (10, 10));
}

void InstrumentPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);

    auto row1 = r.removeFromTop (28);
    generateBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3));
    row1.removeFromLeft (6);
    lockBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3));
    row1.removeFromLeft (6);
    muteBtn.setBounds (row1);

    r.removeFromTop (6);
    auto row2 = r.removeFromTop (28);
    dragBtn.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
    row2.removeFromLeft (6);
    exportBtn.setBounds (row2);
}

} // namespace aimidi
