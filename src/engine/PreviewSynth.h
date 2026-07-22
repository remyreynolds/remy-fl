#pragma once

#include "PreviewSounds.h"
#include "SampleLibrary.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

namespace aimidi
{
/** Built-in preview instrument for Standalone listening.
    Procedural timbres by default; assigned pack samples override when set. */
class PreviewSynth
{
public:
    PreviewSynth();

    void prepare (double sampleRate, int samplesPerBlock);
    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void allNotesOff();

    void setPartTimbre (InstrumentType type, PartTimbre timbre);
    void setDrumKitStyle (DrumKitStyle style);
    void applyGenreDefaults (GenreMode genre);

    /** Assign a loaded sample to a drum piece (nullptr = use synth). */
    void setDrumSample (DrumPiece piece, std::shared_ptr<const LoadedSample> sample);
    /** Assign a pitched sample (nullptr = use synth). Assumed root = C3 (MIDI 60). */
    void setPartSample (InstrumentType type, std::shared_ptr<const LoadedSample> sample);

    PartTimbre getPartTimbre (InstrumentType type) const;
    DrumKitStyle getDrumKitStyle() const
    {
        return (DrumKitStyle) juce::jlimit (0, (int) DrumKitStyle::NumStyles - 1, drumStyle.load());
    }

    static DrumPiece drumPieceForMidiNote (int midiNote);

private:
    class SharedSound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class PitchedVoice : public juce::SynthesiserVoice
    {
    public:
        PitchedVoice (PreviewSynth& o, InstrumentType t) : owner (o), part (t) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        { return dynamic_cast<SharedSound*> (s) != nullptr; }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    private:
        PreviewSynth& owner;
        InstrumentType part;
        PartTimbre timbre = PartTimbre::SoftPiano;
        double sampleRate = 44100.0;
        double phase = 0.0, phase2 = 0.0, phase3 = 0.0;
        double freq = 440.0, freq2 = 440.0;
        float level = 0.0f, env = 0.0f, envDecay = 0.0f, attack = 0.1f;
        bool releasing = false;
        int age = 0;

        // Serum-style unison + resonant state-variable low-pass filter
        static constexpr int kUnison = 7;
        std::array<double, kUnison> uniPhase {};
        float svfLp = 0.0f, svfBp = 0.0f;          // filter state
        float fltEnv = 0.0f;                        // filter envelope 0..1
        float fltDecay = 0.0f;                      // per-sample env multiplier
        float fltBaseHz = 800.0f, fltAmtHz = 0.0f;  // cutoff = base + env*amt
        float fltDamp = 1.0f;                       // damping (lower = more resonance)
        float svfProcess (float in, float cutoffHz);

        // Sample playback
        std::shared_ptr<const LoadedSample> sample;
        bool useSample = false;
        double samplePos = 0.0;
        double sampleInc = 1.0;
        static constexpr int kSampleRootMidi = 60; // C3
    };

    class DrumVoice : public juce::SynthesiserVoice
    {
    public:
        explicit DrumVoice (PreviewSynth& o) : owner (o) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        { return dynamic_cast<SharedSound*> (s) != nullptr; }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float, bool) override;
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    private:
        enum class Kind { Kick, Snare, Clap, Hat };
        PreviewSynth& owner;
        Kind kind = Kind::Kick;
        DrumKitStyle kit = DrumKitStyle::House;
        double sampleRate = 44100.0;
        double phase = 0.0, freq = 100.0;
        double phaseB = 0.0, phaseC = 0.0; // metallic hat partials (909-style)
        float level = 0.0f, env = 0.0f, envDecay = 0.0f, noise = 1.0f;
        float kickBoom = 0.0f;
        int age = 0;

        std::shared_ptr<const LoadedSample> sample;
        bool useSample = false;
        double samplePos = 0.0;
        double sampleInc = 1.0;
    };

    static constexpr int kPitchedParts = 6;

    std::array<juce::Synthesiser, kPitchedParts> pitched;
    juce::Synthesiser drums;
    juce::SynthesiserSound::Ptr sound;

    std::array<std::atomic<int>, (size_t) InstrumentType::NumTypes> partTimbres {};
    std::atomic<int> drumStyle { (int) DrumKitStyle::House };

    // Shared across voices — swapped atomically via shared_ptr copies under lock-free reads
    std::array<std::shared_ptr<const LoadedSample>, (size_t) DrumPiece::NumPieces> drumSamples;
    std::array<std::shared_ptr<const LoadedSample>, (size_t) InstrumentType::NumTypes> partSamples;
    juce::SpinLock sampleLock;
};

} // namespace aimidi
