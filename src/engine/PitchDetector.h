#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace aimidi
{
/** Small, JUCE-free autocorrelation pitch tracker for the "hum to chords"
    feature. Fed fixed-size blocks of mono float samples; returns 0.0 when no
    confident pitch is found (silence, noise, out of vocal range). Header-only
    so it stays unit-testable alongside MusicTheory.h / MusicInstructions.h. */
class PitchDetector
{
public:
    explicit PitchDetector (double sr) : sampleRate (sr) {}

    /** Analyze one block (mono). Returns detected frequency in Hz, or 0.0 if
        unvoiced/too quiet/ambiguous. Cheap RMS gate + autocorrelation limited
        to the human hum range (~70-1000 Hz) keeps this bounded-cost so it is
        safe to call once per block from a UI-thread audio callback. */
    float detectFrequency (const float* samples, int numSamples) const
    {
        if (numSamples < 64 || sampleRate <= 0.0)
            return 0.0f;

        // RMS gate: ignore near-silence so we don't chase noise-floor jitter.
        double rms = 0.0;
        for (int i = 0; i < numSamples; ++i)
            rms += (double) samples[i] * (double) samples[i];
        rms = std::sqrt (rms / (double) numSamples);
        if (rms < 0.004)
            return 0.0f;

        const int minLag = (int) (sampleRate / 1000.0); // ~1000 Hz upper bound
        const int maxLag = (int) (sampleRate / 70.0);   // ~70 Hz lower bound
        if (maxLag <= minLag || maxLag >= numSamples)
            return 0.0f;

        int bestLag = -1;
        double bestCorr = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double corr = 0.0, normA = 0.0, normB = 0.0;
            const int n = numSamples - lag;
            for (int i = 0; i < n; ++i)
            {
                const double a = samples[i];
                const double b = samples[i + lag];
                corr  += a * b;
                normA += a * a;
                normB += b * b;
            }
            const double denom = std::sqrt (normA * normB);
            if (denom <= 1.0e-9) continue;
            const double normalized = corr / denom;
            if (normalized > bestCorr)
            {
                bestCorr = normalized;
                bestLag  = lag;
            }
        }

        // Require a reasonably strong periodic correlation before trusting it
        // as a hummed pitch (avoids reporting garbage on breathy/noisy input).
        if (bestLag <= 0 || bestCorr < 0.55)
            return 0.0f;

        return (float) (sampleRate / (double) bestLag);
    }

    static int frequencyToMidiNote (float hz)
    {
        if (hz <= 0.0f) return -1;
        const double midi = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);
        return (int) std::lround (midi);
    }

private:
    double sampleRate;
};

} // namespace aimidi
