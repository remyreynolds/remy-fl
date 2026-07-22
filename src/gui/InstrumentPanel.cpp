#include "InstrumentPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

InstrumentPanel::InstrumentPanel (InstrumentType t) : type (t)
{
    title.setText (toString (t), juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (12.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (title);

    minimizeBtn.setTooltip ("Minimize / expand this instrument");
    minimizeBtn.onClick = [this]
    {
        setMinimized (! minimized);
        if (onMinimizeChanged) onMinimizeChanged();
    };
    addAndMakeVisible (minimizeBtn);

    chordLabel.setText ("—", juce::dontSendNotification);
    chordLabel.setFont (CustomLookAndFeel::font (11.0f));
    chordLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    chordLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (chordLabel);

    volLabel.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    volLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    addAndMakeVisible (volLabel);

    volSlider.setRange (0.0, 1.25, 0.01);
    volSlider.setValue (0.7, juce::dontSendNotification);
    volSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volSlider.onValueChange = [this]
    {
        if (suppressVolCallback) return;
        if (onVolumeChanged) onVolumeChanged ((float) volSlider.getValue());
    };
    addAndMakeVisible (volSlider);

    midiLabel.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    midiLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    addAndMakeVisible (midiLabel);

    midiCombo.setTextWhenNothingSelected ("AI / Generate");
    midiCombo.setTooltip ("Pick a MIDI loop from your MIDI packs, or leave AI/Generate");
    midiCombo.onChange = [this]
    {
        if (suppressMidiCallback) return;
        if (! onMidiLoopChanged) return;
        const int idx = midiCombo.getSelectedItemIndex();
        if (idx <= 0)
            onMidiLoopChanged ({});
        else if (juce::isPositiveAndBelow (idx - 1, midiIds.size()))
            onMidiLoopChanged (midiIds[idx - 1]);
    };
    addAndMakeVisible (midiCombo);

    synthLabel.setFont (CustomLookAndFeel::font (10.0f, juce::Font::bold));
    synthLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
    addAndMakeVisible (synthLabel);

    soundCombo.setTextWhenNothingSelected ("Synth style");
    soundCombo.setTooltip ("How this part sounds in preview (synth — not drum samples)");
    soundCombo.onChange = [this]
    {
        if (suppressTimbreCallback) return;
        const int id = soundCombo.getSelectedId();
        if (id > 0 && onTimbreChanged)
            onTimbreChanged (partTimbreFromIndex (id - 1));
    };
    addAndMakeVisible (soundCombo);

    generateBtn.setComponentID ("primary");
    generateBtn.onClick = [this] { if (onGenerate) onGenerate(); };
    addAndMakeVisible (generateBtn);

    varyBtn.setComponentID ("outline");
    continueBtn.setComponentID ("outline");
    lockBtn.setComponentID ("outline");
    muteBtn.setComponentID ("outline");
    exportBtn.setComponentID ("outline");
    minimizeBtn.setComponentID ("ghost");

    varyBtn.setTooltip ("AI variation of this part (keeps key / BPM / vibe)");
    varyBtn.onClick = [this] { if (onVary) onVary(); };
    addAndMakeVisible (varyBtn);

    continueBtn.setTooltip ("AI continuation — next section in the same style");
    continueBtn.onClick = [this] { if (onContinue) onContinue(); };
    addAndMakeVisible (continueBtn);

    lockBtn.setClickingTogglesState (true);
    lockBtn.onClick = [this] { if (onLockChanged) onLockChanged (lockBtn.getToggleState()); };
    addAndMakeVisible (lockBtn);

    muteBtn.setClickingTogglesState (true);
    muteBtn.onClick = [this] { if (onMuteChanged) onMuteChanged (muteBtn.getToggleState()); };
    addAndMakeVisible (muteBtn);

    dragBtn.setTooltip ("Click and drag into FL Studio");
    dragBtn.getFileToDrag = [this] () -> juce::File
    {
        if (! hasContent || ! requestMidiFile) return {};
        return requestMidiFile();
    };
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
    updateMinimizedUi();
}

void InstrumentPanel::setMinimized (bool shouldMinimize)
{
    minimized = shouldMinimize;
    minimizeBtn.setButtonText (minimized ? "+" : "−");
    updateMinimizedUi();
    resized();
}

void InstrumentPanel::updateMinimizedUi()
{
    const bool show = ! minimized;
    chordLabel.setVisible (show);
    volLabel.setVisible (show);
    volSlider.setVisible (show);
    midiLabel.setVisible (show);
    midiCombo.setVisible (show);
    synthLabel.setVisible (show);
    soundCombo.setVisible (show);
    generateBtn.setVisible (show);
    varyBtn.setVisible (show);
    continueBtn.setVisible (show);
    lockBtn.setVisible (show);
    muteBtn.setVisible (show);
    dragBtn.setVisible (show);
    exportBtn.setVisible (show);
}

void InstrumentPanel::updateActionEnabled()
{
    generateBtn.setEnabled (! aiBusy);
    varyBtn.setEnabled (hasContent && ! aiBusy);
    continueBtn.setEnabled (hasContent && ! aiBusy);
    dragBtn.setEnabled (hasContent);
    exportBtn.setEnabled (hasContent);
}

void InstrumentPanel::setAiBusy (bool busy)
{
    aiBusy = busy;
    updateActionEnabled();
}

void InstrumentPanel::setSoundOptions (const std::vector<PartTimbre>& options, PartTimbre selected)
{
    suppressTimbreCallback = true;
    soundCombo.clear (juce::dontSendNotification);

    int selectId = 0;
    for (auto t : options)
    {
        const int id = (int) t + 1;
        soundCombo.addItem (toString (t), id);
        if (t == selected)
            selectId = id;
    }

    if (selectId == 0 && ! options.empty())
        selectId = (int) options.front() + 1;

    if (selectId > 0)
        soundCombo.setSelectedId (selectId, juce::dontSendNotification);

    suppressTimbreCallback = false;
}

void InstrumentPanel::setMidiLoopOptions (const std::vector<const MidiEntry*>& options,
                                          const juce::String& selectedId)
{
    suppressMidiCallback = true;
    midiCombo.clear (juce::dontSendNotification);
    midiIds.clear();
    midiCombo.addItem ("AI / Generate", 1);

    int selectId = 1;
    int nextId = 2;
    for (auto* e : options)
    {
        if (e == nullptr) continue;
        midiCombo.addItem (e->packName + " · " + e->name, nextId);
        midiIds.add (e->id);
        if (e->id == selectedId)
            selectId = nextId;
        ++nextId;
    }

    if (options.empty())
        midiCombo.setTextWhenNothingSelected ("Add MIDI packs…");
    else
        midiCombo.setTextWhenNothingSelected (juce::String ((int) options.size()) + " loops…");

    midiCombo.setSelectedId (selectId, juce::dontSendNotification);
    suppressMidiCallback = false;
}

void InstrumentPanel::setHasContent (bool has)
{
    hasContent = has;
    updateActionEnabled();
    repaint();
}

void InstrumentPanel::setChordSummary (const juce::String& summary)
{
    chordLabel.setText (summary.isNotEmpty() ? summary : "—",
                        juce::dontSendNotification);
}

void InstrumentPanel::setVolume (float gain01)
{
    suppressVolCallback = true;
    volSlider.setValue ((double) gain01, juce::dontSendNotification);
    suppressVolCallback = false;
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto titleArea = getLocalBounds().reduced (12, 10).removeFromTop (20);
    auto dot = titleArea.removeFromRight (12);
    g.setColour (hasContent ? CustomLookAndFeel::success : CustomLookAndFeel::txt3);
    g.fillEllipse (dot.toFloat().withSizeKeepingCentre (6, 6));
}

void InstrumentPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto titleRow = r.removeFromTop (20);
    minimizeBtn.setBounds (titleRow.removeFromRight (24).reduced (1));
    titleRow.removeFromRight (4);
    title.setBounds (titleRow);

    if (minimized)
        return;

    r.removeFromTop (6);
    chordLabel.setBounds (r.removeFromTop (13));
    r.removeFromTop (4);

    auto midiRow = r.removeFromTop (28);
    midiLabel.setBounds (midiRow.removeFromLeft (36));
    midiCombo.setBounds (midiRow);

    r.removeFromTop (4);
    auto synthRow = r.removeFromTop (28);
    synthLabel.setBounds (synthRow.removeFromLeft (36));
    soundCombo.setBounds (synthRow);

    r.removeFromTop (4);
    auto volRow = r.removeFromTop (16);
    volLabel.setBounds (volRow.removeFromLeft (24));
    volSlider.setBounds (volRow);

    r.removeFromTop (8);
    auto row1 = r.removeFromTop (30);
    const int gap = 6;
    const int w3 = (row1.getWidth() - gap * 2) / 3;
    generateBtn.setBounds (row1.removeFromLeft (w3));
    row1.removeFromLeft (gap);
    varyBtn.setBounds (row1.removeFromLeft (w3));
    row1.removeFromLeft (gap);
    continueBtn.setBounds (row1);

    r.removeFromTop (6);
    auto row2 = r.removeFromTop (28);
    lockBtn.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
    row2.removeFromLeft (6);
    muteBtn.setBounds (row2);

    r.removeFromTop (6);
    auto row3 = r.removeFromTop (28);
    dragBtn.setBounds (row3.removeFromLeft (row3.getWidth() / 2 - 3));
    row3.removeFromLeft (6);
    exportBtn.setBounds (row3);
}

} // namespace aimidi
