#pragma once

#include "engine/HumToChords.h"
#include "engine/PitchDetector.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <vector>

namespace aimidi
{
/** Drives a standalone juce::AudioDeviceManager mic input (independent of the
    host's audio graph) to capture a hummed melody and turn held notes into
    HumNote segments. Runs on the device's own callback thread — NOT the
    plugin's real-time processBlock thread — so a lightweight lock around the
    captured-notes list is acceptable here (it would not be on the host's
    audio thread). */
class HumCaptureCallback : public juce::AudioIODeviceCallback
{
public:
    explicit HumCaptureCallback (double bpmIn) : bpm (bpmIn) {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                            int numInputChannels,
                                            float* const* outputChannelData,
                                            int numOutputChannels,
                                            int numSamples,
                                            const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        const double nowSec = sampleClock / (sampleRate > 0.0 ? sampleRate : 44100.0);
        sampleClock += (double) numSamples;

        int note = -1;
        if (numInputChannels > 0 && inputChannelData[0] != nullptr)
        {
            const float freq = detector.detectFrequency (inputChannelData[0], numSamples);
            if (freq > 0.0f)
                note = PitchDetector::frequencyToMidiNote (freq);
        }
        liveNote.store (note);

        if (note != activeNote)
        {
            if (activeNote >= 0)
            {
                const double lenSec = nowSec - activeStartSec;
                if (lenSec > 0.09) // debounce: ignore very short blips/noise
                {
                    HumNote hn;
                    hn.startBeats  = activeStartSec * bpm / 60.0;
                    hn.lengthBeats = juce::jmax (0.1, lenSec * bpm / 60.0);
                    hn.midiNote    = activeNote;
                    const juce::ScopedLock sl (lock);
                    notes.push_back (hn);
                }
            }
            activeNote     = note;
            activeStartSec = nowSec;
        }
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
        detector    = PitchDetector (sampleRate);
        sampleClock = 0.0;
        activeNote  = -1;
        liveNote.store (-1);
        const juce::ScopedLock sl (lock);
        notes.clear();
    }

    void audioDeviceStopped() override {}

    /** UI-thread poll: current live pitch (-1 if silent/no capture). */
    int currentLiveNote() const { return liveNote.load(); }

    /** UI-thread: snapshot of everything captured so far. */
    std::vector<HumNote> snapshotNotes() const
    {
        const juce::ScopedLock sl (lock);
        return notes;
    }

private:
    double bpm;
    PitchDetector detector { 44100.0 };
    double sampleRate  = 44100.0;
    double sampleClock = 0.0;
    int    activeNote     = -1;
    double activeStartSec = 0.0;
    std::atomic<int> liveNote { -1 };

    mutable juce::CriticalSection lock;
    std::vector<HumNote> notes;
};

} // namespace aimidi
