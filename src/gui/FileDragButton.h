#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace aimidi
{
/** TextButton that starts an OS file drag during mouseDrag (not onClick).
    On macOS, performExternalDragDropOfFiles needs an active mouse-down event —
    calling it from a button click (mouse-up) silently fails. */
class FileDragButton : public juce::TextButton
{
public:
    using FileProvider = std::function<juce::File()>;

    explicit FileDragButton (const juce::String& text = "Drag")
        : juce::TextButton (text) {}

    FileProvider getFileToDrag;

    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDrag (e);

        if (dragStarted || ! isEnabled() || getFileToDrag == nullptr)
            return;

        if (e.getDistanceFromDragStart() < 6)
            return;

        const auto file = getFileToDrag();
        if (! file.existsAsFile())
            return;

        dragStarted = true;

        juce::StringArray files;
        files.add (file.getFullPathName());

        // Top-level component gives macOS a reliable NSView for the drag session.
        auto* source = getTopLevelComponent();
        if (source == nullptr) source = this;

        juce::DragAndDropContainer::performExternalDragDropOfFiles (
            files, /*canMoveFiles*/ false, source);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        juce::TextButton::mouseUp (e);
    }

private:
    bool dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileDragButton)
};

} // namespace aimidi
