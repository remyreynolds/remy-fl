#include "DrumKitPanel.h"
#include "CustomLookAndFeel.h"

namespace aimidi
{

DrumKitPanel::DrumKitPanel()
{
    title.setText ("Drums", juce::dontSendNotification);
    title.setFont (CustomLookAndFeel::font (12.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, CustomLookAndFeel::txt1);
    addAndMakeVisible (title);

    minimizeBtn.setTooltip ("Minimize / expand drums");
    minimizeBtn.onClick = [this]
    {
        setMinimized (! minimized);
        if (onMinimizeChanged) onMinimizeChanged();
    };
    addAndMakeVisible (minimizeBtn);

    for (int i = 0; i < (int) DrumPiece::NumPieces; ++i)
    {
        auto& row = rows[(size_t) i];
        row.piece = (DrumPiece) i;

        row.label.setText (toString (row.piece), juce::dontSendNotification);
        row.label.setFont (CustomLookAndFeel::font (11.0f, juce::Font::bold));
        row.label.setColour (juce::Label::textColourId, CustomLookAndFeel::txt2);
        addAndMakeVisible (row.label);

        row.volSlider.setRange (0.0, 1.25, 0.01);
        row.volSlider.setValue (0.85, juce::dontSendNotification);
        row.volSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.volSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        row.volSlider.setTooltip ("Preview volume — genre mix sets defaults (hats loud in house)");
        row.volSlider.onValueChange = [this, i]
        {
            auto& r = rows[(size_t) i];
            if (r.suppressVol) return;
            if (onVolumeChanged) onVolumeChanged ((DrumPiece) i, (float) r.volSlider.getValue());
        };
        addAndMakeVisible (row.volSlider);

        row.sampleCombo.setTextWhenNothingSelected ("Pack sound…");
        row.sampleCombo.setTooltip ("Pick a WAV from your imported pack for this drum");
        row.sampleCombo.onChange = [this, i]
        {
            auto& r = rows[(size_t) i];
            if (r.suppressSample) return;
            if (! onSampleChanged) return;
            // Index 0 = Synth; 1..N map into sampleIds (more reliable than item IDs).
            const int idx = r.sampleCombo.getSelectedItemIndex();
            if (idx <= 0)
                onSampleChanged ((DrumPiece) i, {});
            else if (juce::isPositiveAndBelow (idx - 1, r.sampleIds.size()))
                onSampleChanged ((DrumPiece) i, r.sampleIds[idx - 1]);
        };
        addAndMakeVisible (row.sampleCombo);

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

        row.dragBtn.setTooltip ("Click and drag into FL Studio");
        row.dragBtn.getFileToDrag = [this, i] () -> juce::File
        {
            auto& r = rows[(size_t) i];
            if (! r.hasContent || ! requestPieceMidiFile) return {};
            return requestPieceMidiFile ((DrumPiece) i);
        };
        addAndMakeVisible (row.dragBtn);
    }

    generateAllBtn.setComponentID ("primary");
    generateAllBtn.onClick = [this] { if (onGenerateAll) onGenerateAll(); };
    addAndMakeVisible (generateAllBtn);

    dragAllBtn.setTooltip ("Click and drag the full drum loop into FL Studio");
    dragAllBtn.getFileToDrag = [this] () -> juce::File
    {
        if (! fullKitHasContent || ! requestFullKitMidiFile) return {};
        return requestFullKitMidiFile();
    };
    addAndMakeVisible (dragAllBtn);

    for (auto& row : rows)
        setPieceHasContent (row.piece, false);

    updateMinimizedUi();
}

void DrumKitPanel::setMinimized (bool shouldMinimize)
{
    minimized = shouldMinimize;
    minimizeBtn.setButtonText (minimized ? "+" : "−");
    updateMinimizedUi();
    resized();
}

void DrumKitPanel::updateMinimizedUi()
{
    const bool show = ! minimized;
    for (auto& row : rows)
    {
        row.label.setVisible (show);
        row.volSlider.setVisible (show);
        row.sampleCombo.setVisible (show);
        row.generateBtn.setVisible (show);
        row.lockBtn.setVisible (show);
        row.muteBtn.setVisible (show);
        row.dragBtn.setVisible (show);
    }
    generateAllBtn.setVisible (show);
    dragAllBtn.setVisible (show);
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

void DrumKitPanel::setPieceVolume (DrumPiece piece, float gain01)
{
    auto& row = rows[(size_t) piece];
    row.suppressVol = true;
    row.volSlider.setValue ((double) gain01, juce::dontSendNotification);
    row.suppressVol = false;
}

void DrumKitPanel::setSampleOptions (DrumPiece piece,
                                     const std::vector<const SampleEntry*>& options,
                                     const juce::String& selectedId)
{
    auto& row = rows[(size_t) piece];
    row.suppressSample = true;
    row.sampleCombo.clear (juce::dontSendNotification);
    row.sampleIds.clear();
    row.sampleCombo.addItem ("(built-in synth)", 1);

    int selectId = 1;
    int nextId = 2;
    for (auto* e : options)
    {
        if (e == nullptr) continue;
        // Short label — pack is chosen in the header Pack menu
        const auto label = juce::String (toString (e->role)) + " · " + e->name;
        row.sampleCombo.addItem (label, nextId);
        row.sampleIds.add (e->id);
        if (e->id == selectedId)
            selectId = nextId;
        ++nextId;
    }

    if (options.empty())
        row.sampleCombo.setTextWhenNothingSelected ("No pack sounds");
    else
        row.sampleCombo.setTextWhenNothingSelected (juce::String ((int) options.size()) + " sounds…");

    row.sampleCombo.setSelectedId (selectId, juce::dontSendNotification);
    row.suppressSample = false;
}

void DrumKitPanel::paint (juce::Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds());

    auto titleArea = getLocalBounds().reduced (12, 10).removeFromTop (20);
    auto dot = titleArea.removeFromRight (12);
    g.setColour (fullKitHasContent ? CustomLookAndFeel::success : CustomLookAndFeel::txt3);
    g.fillEllipse (dot.toFloat().withSizeKeepingCentre (6, 6));
}

void DrumKitPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto titleRow = r.removeFromTop (22);
    minimizeBtn.setBounds (titleRow.removeFromRight (26).reduced (1));
    title.setBounds (titleRow);

    if (minimized)
        return;

    r.removeFromTop (8);

    const int bottomH = 28;
    const int rowH = juce::jmax (18, (r.getHeight() - bottomH - 6) / (int) rows.size());

    for (auto& row : rows)
    {
        auto rr = r.removeFromTop (rowH);
        row.label.setBounds (rr.removeFromLeft (42));
        rr.removeFromLeft (2);
        row.volSlider.setBounds (rr.removeFromLeft (28));
        rr.removeFromLeft (2);
        // Give the sample picker most of the row — this is what users need to see
        const int btnW = juce::jmax (28, juce::jmin (44, (rr.getWidth() - 8) / 8));
        auto buttons = rr.removeFromRight (btnW * 4 + 6);
        row.sampleCombo.setBounds (rr);
        row.generateBtn.setBounds (buttons.removeFromLeft (btnW));
        buttons.removeFromLeft (2);
        row.lockBtn.setBounds (buttons.removeFromLeft (btnW));
        buttons.removeFromLeft (2);
        row.muteBtn.setBounds (buttons.removeFromLeft (btnW));
        buttons.removeFromLeft (2);
        row.dragBtn.setBounds (buttons);
        r.removeFromTop (2);
    }

    r.removeFromTop (4);
    auto bottom = r.removeFromTop (bottomH);
    generateAllBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 4));
    bottom.removeFromLeft (8);
    dragAllBtn.setBounds (bottom);
}

} // namespace aimidi
