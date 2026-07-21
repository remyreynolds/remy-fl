#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/MidiGenerator.h"
#include "ai/AIClient.h"
#include <array>

namespace aimidi
{
/** The plugin core. Owns the project state (params + generated parts),
    the generation engine, the AI client, and a simple MIDI preview player
    that streams the current parts out of the plugin's MIDI output so the
    user's own instrument (Serum, Omnisphere, etc.) makes the sound. */
class AIMidiGenProcessor : public juce::AudioProcessor
{
public:
    AIMidiGenProcessor();
    ~AIMidiGenProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AI MIDI Gen"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // ---- Project API used by the editor ----
    MusicParams& params() { return projectParams; }
    GeneratedPart& part (InstrumentType t) { return parts[(size_t) t]; }

    void generatePart (InstrumentType t);
    void regenerateFromAI (const juce::String& prompt,
                           std::function<void (AIClient::Response)> onDone);

    void togglePreview (bool shouldPlay);
    bool isPreviewing() const { return previewing.load(); }

    AIClient& ai() { return aiClient; }

private:
    MusicParams projectParams;
    std::array<GeneratedPart, (size_t) InstrumentType::NumTypes> parts;

    MidiGenerator generator;
    AIClient      aiClient;

    // --- preview playback ---
    std::atomic<bool> previewing { false };
    double sampleRateHz = 44100.0;
    double previewPpqPos = 0.0;   // position in quarter notes
    std::array<juce::MidiMessageSequence, (size_t) InstrumentType::NumTypes> previewSeqs;
    std::array<int, (size_t) InstrumentType::NumTypes> previewIdx {};

    void rebuildPreviewSequences();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMidiGenProcessor)
};

} // namespace aimidi
