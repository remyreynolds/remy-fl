#include "PreviewSynth.h"
#include <cmath>
#include <cstdint>

namespace aimidi
{

namespace
{
float nextNoise (std::uint32_t& state)
{
    // Integer LCG (rand48-family constants). The old float version overflowed
    // float precision instantly and hit UB casting out-of-range floats to int.
    state = state * 1103515245u + 12345u;
    return (float) ((state >> 16) & 0x7fffu) / 16384.0f - 1.0f;
}

float polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return (float) (t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }
    return 0.0f;
}

float naiveSaw (double phase, double dt)
{
    const double t = phase / juce::MathConstants<double>::twoPi;
    return (float) (2.0 * t - 1.0) - polyBlep (t, dt);
}

float naiveSquare (double phase, double dt)
{
    const double t = phase / juce::MathConstants<double>::twoPi;
    float s = phase < juce::MathConstants<double>::pi ? 1.0f : -1.0f;
    s += polyBlep (t, dt);
    s -= polyBlep (std::fmod (t + 0.5, 1.0), dt);
    return s;
}
} // namespace

PreviewSynth::PreviewSynth()
{
    auto map = defaultsFor (GenreMode::House);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        partTimbres[(size_t) t].store ((int) map.defaults[(size_t) t]);
    drumStyle.store ((int) map.drumKit);

    auto pitchedSound = juce::SynthesiserSound::Ptr (new SharedSound());
    auto drumSound    = juce::SynthesiserSound::Ptr (new SharedSound());
    sound = pitchedSound;

    const InstrumentType pitchedTypes[kPitchedParts] = {
        InstrumentType::Melody,
        InstrumentType::Chords,
        InstrumentType::Bass,
        InstrumentType::CounterMelody,
        InstrumentType::Arp,
        InstrumentType::Pad
    };

    for (int i = 0; i < kPitchedParts; ++i)
    {
        pitched[(size_t) i].addSound (pitchedSound);
        // Generous polyphony: chord/pad lanes hold long overlapping voicings and
        // voice-stealing hard-cuts click (steal path has no release tail).
        const int voices = (pitchedTypes[i] == InstrumentType::Chords
                            || pitchedTypes[i] == InstrumentType::Pad) ? 24 : 12;
        for (int v = 0; v < voices; ++v)
            pitched[(size_t) i].addVoice (new PitchedVoice (*this, pitchedTypes[i]));
    }

    drums.addSound (drumSound);
    for (int i = 0; i < 16; ++i)
        drums.addVoice (new DrumVoice (*this));
}

void PreviewSynth::prepare (double sampleRate, int /*samplesPerBlock*/)
{
    for (auto& s : pitched)
        s.setCurrentPlaybackSampleRate (sampleRate);
    drums.setCurrentPlaybackSampleRate (sampleRate);
    duckSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    duckEnv = 0.0f;
}

void PreviewSynth::setPartTimbre (InstrumentType type, PartTimbre timbre)
{
    if (type == InstrumentType::Drums) return;
    partTimbres[(size_t) type].store ((int) timbre);
}

void PreviewSynth::setDrumKitStyle (DrumKitStyle style)
{
    drumStyle.store ((int) style);
}

void PreviewSynth::applyGenreDefaults (GenreMode genre)
{
    auto map = defaultsFor (genre);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        partTimbres[(size_t) t].store ((int) map.defaults[(size_t) t]);
    drumStyle.store ((int) map.drumKit);
}

void PreviewSynth::setDrumSample (DrumPiece piece, std::shared_ptr<const LoadedSample> sample)
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    drumSamples[(size_t) piece] = std::move (sample);
}

void PreviewSynth::setPartSample (InstrumentType type, std::shared_ptr<const LoadedSample> sample)
{
    if (type == InstrumentType::Drums) return;
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    partSamples[(size_t) type] = std::move (sample);
}

DrumPiece PreviewSynth::drumPieceForMidiNote (int midiNote)
{
    switch (midiNote)
    {
        case 35: case 36: return DrumPiece::Kick;
        case 37: return DrumPiece::Rim;        // GM side stick
        case 38: case 40: return DrumPiece::Snare;
        case 39: return DrumPiece::Clap;
        case 42: case 44: return DrumPiece::ClosedHat;
        case 46: return DrumPiece::OpenHat;
        case 51: case 59: return DrumPiece::Ride;
        case 69: case 70: case 82: return DrumPiece::Shaker; // cabasa/maracas/shaker
        case 62: case 63: return DrumPiece::CongaHi;
        case 64: return DrumPiece::CongaLo;
        default:
            if (midiNote <= 40) return DrumPiece::Kick;
            if (midiNote <= 45) return DrumPiece::Snare;
            return DrumPiece::ClosedHat;
    }
}

PartTimbre PreviewSynth::getPartTimbre (InstrumentType type) const
{
    return partTimbreFromIndex (partTimbres[(size_t) type].load());
}

void PreviewSynth::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals; // decaying envelopes/filters otherwise
                                         // hit denormal range and spike the CPU
    std::array<juce::MidiBuffer, kPitchedParts> partMidi;
    juce::MidiBuffer kit;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int ch = msg.getChannel();
        if (ch == 10)
        {
            kit.addEvent (msg, metadata.samplePosition);
            continue;
        }
        if (ch >= 1 && ch <= kPitchedParts)
            partMidi[(size_t) (ch - 1)].addEvent (msg, metadata.samplePosition);
    }

    for (int i = 0; i < kPitchedParts; ++i)
        pitched[(size_t) i].renderNextBlock (buffer, partMidi[(size_t) i],
                                             0, buffer.getNumSamples());

    // Sidechain pump: at this point the buffer holds ONLY the pitched mix,
    // so duck it under every kick hit before the drums are added on top —
    // the classic house "pump" that makes the preview sit like a record.
    {
        const int numSamples = buffer.getNumSamples();

        // Kick note-on positions inside this block.
        int kickPos[32];
        int numKicks = 0;
        for (const auto metadata : kit)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn()
                && drumPieceForMidiNote (msg.getNoteNumber()) == DrumPiece::Kick
                && numKicks < 32)
                kickPos[numKicks++] = metadata.samplePosition;
        }

        const float depth   = 0.55f; // max gain reduction under the kick
        const float recover = (float) std::exp (-1.0 / (duckSampleRate * 0.110)); // ~110ms release
        // ~3ms attack ramp: a one-sample jump to full duck is a 55% gain step
        // and clicks audibly on every kick.
        const float attack  = 1.0f - (float) std::exp (-1.0 / (duckSampleRate * 0.003));
        float env = duckEnv;
        float target = 0.0f;
        int nextKick = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            while (nextKick < numKicks && kickPos[nextKick] <= i)
            {
                target = 1.0f;
                ++nextKick;
            }
            if (target > env)
            {
                env += (target - env) * attack;
                if (env > 0.999f) { env = 1.0f; target = 0.0f; } // reached — release phase
            }
            const float g = 1.0f - depth * env;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[i] *= g;
            env *= recover;
        }
        duckEnv = env;
    }

    drums.renderNextBlock (buffer, kit, 0, buffer.getNumSamples());

    const float gain = 0.55f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = std::tanh (d[i] * gain);
    }
}

void PreviewSynth::allNotesOff()
{
    for (auto& s : pitched)
        s.allNotesOff (0, true);
    drums.allNotesOff (0, true);
}

//==============================================================================
float PreviewSynth::PitchedVoice::svfProcess (float in, float cutoffHz)
{
    // Chamberlin state-variable filter (low-pass output). Stable below ~sr/6.
    const float fc = juce::jlimit (30.0f, (float) (sampleRate * 0.16), cutoffHz);
    const float f  = 2.0f * std::sin (juce::MathConstants<float>::pi * fc / (float) sampleRate);
    const float hp = in - svfLp - fltDamp * svfBp;
    svfBp += f * hp;
    svfLp += f * svfBp;
    return svfLp;
}

void PreviewSynth::PitchedVoice::startNote (int midiNote, float velocity,
                                            juce::SynthesiserSound*, int)
{
    sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    level = juce::jlimit (0.05f, 1.0f, velocity);
    phase = phase2 = phase3 = 0.0;
    env = 0.0f;
    releasing = false;
    age = 0;
    svfLp = svfBp = 0.0f;
    fltEnv = 1.0f;
    // Spread unison start phases deterministically so voices never phase-align.
    for (int u = 0; u < kUnison; ++u)
        uniPhase[(size_t) u] = juce::MathConstants<double>::twoPi
                             * std::fmod (0.137 * (u + 1) * (midiNote % 12 + 1), 1.0);
    useSample = false;
    samplePos = 0.0;
    sample.reset();

    {
        const juce::SpinLock::ScopedLockType sl (owner.sampleLock);
        sample = owner.partSamples[(size_t) part];
    }
    // One-pole attack coefficient from a time constant, so envelopes sound
    // the same at 44.1k / 48k / 96k instead of speeding up with sample rate.
    auto atkCoef = [this] (double seconds)
    { return (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0, sampleRate) * seconds))); };

    if (sample != nullptr && sample->buffer.getNumSamples() > 0)
    {
        useSample = true;
        const double pitchRatio = std::pow (2.0, (midiNote - kSampleRootMidi) / 12.0);
        sampleInc = pitchRatio * (sample->sampleRate / juce::jmax (1.0, sampleRate));
        attack = atkCoef (0.00008);
        envDecay = (float) std::exp (-1.0 / (sampleRate * 1.4));
        return;
    }

    timbre = owner.getPartTimbre (part);

    freq  = juce::MidiMessage::getMidiNoteInHertz (midiNote);
    freq2 = freq * 2.003;

    switch (timbre)
    {
        case PartTimbre::WarmPad:
        case PartTimbre::Strings:
            attack = atkCoef (0.0015); // slow pad swell
            {
                const double decaySec = juce::jmap ((double) midiNote, 24.0, 96.0, 4.5, 1.8);
                envDecay = (float) std::exp (-1.0 / (sampleRate * decaySec));
            }
            break;
        case PartTimbre::Pluck:
            attack = atkCoef (0.00005);
            envDecay = (float) std::exp (-1.0 / (sampleRate * 0.35));
            break;
        case PartTimbre::Sub808:
        case PartTimbre::HouseBass:
        case PartTimbre::AcousticBass:
            attack = atkCoef (0.00011);
            envDecay = (float) std::exp (-1.0 / (sampleRate * (timbre == PartTimbre::Sub808 ? 0.9 : 0.55)));
            break;
        case PartTimbre::HousePiano:
            attack = atkCoef (0.00009);
            envDecay = (float) std::exp (-1.0 / (sampleRate * juce::jmap ((double) midiNote, 24.0, 96.0, 1.4, 0.35)));
            break;
        case PartTimbre::ClassicPiano:
            attack = atkCoef (0.00018);
            envDecay = (float) std::exp (-1.0 / (sampleRate * juce::jmap ((double) midiNote, 24.0, 96.0, 3.2, 0.7)));
            break;
        case PartTimbre::ChordSynth:
        case PartTimbre::BrightLead:
            attack = atkCoef (0.0001);
            envDecay = (float) std::exp (-1.0 / (sampleRate * 1.1));
            break;
        case PartTimbre::SuperSaw:
            // Serum-style stab/lead: fast attack, musical decay, filter env
            // opens with velocity and sweeps shut for that "juicy" front.
            attack = atkCoef (0.00006);
            envDecay  = (float) std::exp (-1.0 / (sampleRate * 1.6));
            fltDecay  = (float) std::exp (-1.0 / (sampleRate * 0.22));
            fltBaseHz = 420.0f;
            fltAmtHz  = 1600.0f + 4200.0f * level;  // velocity = brightness
            fltDamp   = 0.55f;                       // audible resonance edge
            break;
        case PartTimbre::FilterBass:
            // Classic house bass: punchy filter sweep, tight amp decay.
            freq2 = freq * 0.5; // sub square one octave DOWN
            attack = atkCoef (0.00003);
            envDecay  = (float) std::exp (-1.0 / (sampleRate * 0.38));
            fltDecay  = (float) std::exp (-1.0 / (sampleRate * 0.11));
            fltBaseHz = 90.0f;
            fltAmtHz  = 900.0f + 1900.0f * level;
            fltDamp   = 0.60f;
            break;
        case PartTimbre::OrganBass:
            // M1-style organ bass: instant on, sustained while held.
            attack = atkCoef (0.000025); // near-instant organ key-on
            envDecay = (float) std::exp (-1.0 / (sampleRate * 2.5));
            break;
        case PartTimbre::GrowlBass:
            // Serum-style wobble bass: fast on, sustained while held, filter
            // is LFO-wobbled in renderNextBlock (fltEnv reused as LFO phase).
            attack = atkCoef (0.00004);
            envDecay = (float) std::exp (-1.0 / (sampleRate * 2.2));
            fltBaseHz = 220.0f;
            fltAmtHz  = 1400.0f + 2600.0f * level;
            fltDamp   = 0.42f; // heavier resonance = growlier
            fltEnv    = 0.0f;  // repurposed as LFO phase (radians) for this timbre
            break;
        case PartTimbre::AiryPad:
            // Wide unison pad: slow swell, long tail, airy top via detune spread.
            attack = atkCoef (0.006);
            {
                const double decaySec = juce::jmap ((double) midiNote, 24.0, 96.0, 6.0, 3.0);
                envDecay = (float) std::exp (-1.0 / (sampleRate * decaySec));
            }
            fltBaseHz = 1200.0f;
            fltAmtHz  = 3200.0f;
            fltDamp   = 0.95f; // gentle, no resonance edge — smooth top end
            break;
        case PartTimbre::SoftPiano:
        default:
            attack = atkCoef (0.0002);
            envDecay = (float) std::exp (-1.0 / (sampleRate * juce::jmap ((double) midiNote, 24.0, 96.0, 2.6, 0.55)));
            break;
    }
}

void PreviewSynth::PitchedVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        releasing = true;
        const double releaseSec = (timbre == PartTimbre::WarmPad || timbre == PartTimbre::Strings
                                    || timbre == PartTimbre::AiryPad)
                                      ? 0.55 : 0.18;
        envDecay = (float) std::exp (-1.0 / (sampleRate * releaseSec));
    }
    else
    {
        clearCurrentNote();
        env = 0.0f;
    }
}

void PreviewSynth::PitchedVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                                  int startSample, int numSamples)
{
    if (getCurrentlyPlayingNote() < 0) return;

    auto* left  = outputBuffer.getWritePointer (0, startSample);
    auto* right = outputBuffer.getNumChannels() > 1
                      ? outputBuffer.getWritePointer (1, startSample) : nullptr;

    if (useSample && sample != nullptr)
    {
        const auto& buf = sample->buffer;
        const int n = buf.getNumSamples();
        const int chs = buf.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            if (! releasing && env < 1.0f)
                env += (1.0f - env) * attack;
            else
                env *= envDecay;

            if (samplePos >= n - 1)
            {
                clearCurrentNote();
                break;
            }

            const int i0 = (int) samplePos;
            const float frac = (float) (samplePos - (double) i0);
            const float a = buf.getSample (0, i0);
            const float b = buf.getSample (0, juce::jmin (i0 + 1, n - 1));
            float s = (a + (b - a) * frac) * env * level * 0.85f;
            if (chs > 1)
            {
                const float aR = buf.getSample (1, i0);
                const float bR = buf.getSample (1, juce::jmin (i0 + 1, n - 1));
                const float sR = (aR + (bR - aR) * frac) * env * level * 0.85f;
                left[i] += s;
                if (right != nullptr) right[i] += sR;
            }
            else
            {
                left[i] += s;
                if (right != nullptr) right[i] += s;
            }

            samplePos += sampleInc;
            ++age;
            if (env < 0.001f && releasing)
            {
                clearCurrentNote();
                break;
            }
        }
        return;
    }

    const double w = juce::MathConstants<double>::twoPi / sampleRate;
    const double dt = freq / sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! releasing && env < 1.0f)
            env += (1.0f - env) * attack;
        else
            env *= envDecay;

        const float t = (float) age / (float) sampleRate;
        float sample = 0.0f;

        switch (timbre)
        {
            case PartTimbre::SuperSaw:
            {
                // 7 detuned polyBLEP saws (center + 3 pairs, up to ±18 cents),
                // summed then squeezed through the resonant LP swept by fltEnv.
                static constexpr double detune[kUnison] =
                    { 1.0, 0.9946, 1.0054, 0.9895, 1.0106, 0.9827, 1.0176 };
                static constexpr float uniGain[kUnison] =
                    { 0.30f, 0.22f, 0.22f, 0.16f, 0.16f, 0.11f, 0.11f };

                float mix = 0.0f;
                for (int u = 0; u < kUnison; ++u)
                {
                    const double fu = freq * detune[u];
                    mix += naiveSaw (uniPhase[(size_t) u], fu / sampleRate) * uniGain[u];
                    uniPhase[(size_t) u] += fu * w;
                    if (uniPhase[(size_t) u] > juce::MathConstants<double>::twoPi)
                        uniPhase[(size_t) u] -= juce::MathConstants<double>::twoPi;
                }
                fltEnv *= fltDecay;
                const float filtered = svfProcess (mix, fltBaseHz + fltAmtHz * fltEnv);
                sample = filtered * env * level * 0.34f;
                break;
            }
            case PartTimbre::FilterBass:
            {
                // Saw + square sub an octave down, through the punchy sweep.
                const float saw = naiveSaw (phase, dt);
                const float sub = naiveSquare (phase2, dt * 0.5) * 0.45f;
                fltEnv *= fltDecay;
                const float filtered = svfProcess (saw + sub, fltBaseHz + fltAmtHz * fltEnv);
                sample = filtered * env * level * 0.50f;
                break;
            }
            case PartTimbre::OrganBass:
            {
                // M1 organ bass: strong fundamental + octave + a twelfth,
                // tiny attack click. The chuggy pumping comes from the notes.
                const float h1 = (float) std::sin (phase);
                const float h2 = (float) std::sin (phase2) * 0.42f;
                const float h3 = (float) std::sin (phase3) * 0.20f;
                const float click = naiveSaw (phase2, dt * 2.0) * std::exp (-t * 60.0f) * 0.18f;
                sample = (h1 + h2 + h3 + click) * env * level * 0.38f;
                break;
            }
            case PartTimbre::GrowlBass:
            {
                // Sub + saw through a filter wobbled by a slow LFO — the
                // classic Serum growl-bass movement (LFO phase kept in fltEnv).
                const float sub = (float) std::sin (phase) * 0.6f;
                const float saw = naiveSaw (phase2, dt * 1.003);
                fltEnv += (float) (2.0 * juce::MathConstants<double>::pi * 5.2 / sampleRate);
                if (fltEnv > juce::MathConstants<float>::twoPi)
                    fltEnv -= juce::MathConstants<float>::twoPi;
                const float wobble = 0.5f + 0.5f * std::sin (fltEnv);
                const float filtered = svfProcess (sub + saw, fltBaseHz + fltAmtHz * wobble);
                sample = filtered * env * level * 0.55f;
                break;
            }
            case PartTimbre::AiryPad:
            {
                // Wide 7-voice unison through a gentle low-pass — same engine
                // as SuperSaw but wider detune, slower envelope, softer filter.
                static constexpr double detune[7] =
                    { 1.0, 0.986, 1.014, 0.972, 1.028, 0.958, 1.042 };
                static constexpr float uniGain[7] =
                    { 0.26f, 0.20f, 0.20f, 0.15f, 0.15f, 0.10f, 0.10f };

                float mix = 0.0f;
                for (int u = 0; u < kUnison; ++u)
                {
                    const double fu = freq * detune[u];
                    mix += naiveSaw (uniPhase[(size_t) u], fu / sampleRate) * uniGain[u];
                    uniPhase[(size_t) u] += fu * w;
                    if (uniPhase[(size_t) u] > juce::MathConstants<double>::twoPi)
                        uniPhase[(size_t) u] -= juce::MathConstants<double>::twoPi;
                }
                const float filtered = svfProcess (mix, fltBaseHz);
                sample = filtered * env * level * 0.30f;
                break;
            }
            case PartTimbre::ChordSynth:
            {
                const float s1 = naiveSaw (phase, dt);
                const float s2 = naiveSaw (phase2, dt * 2.003) * 0.35f;
                sample = (s1 + s2) * env * level * 0.22f;
                break;
            }
            case PartTimbre::BrightLead:
            {
                const float sq = naiveSquare (phase, dt) * 0.55f;
                const float sw = naiveSaw (phase2, dt * 2.01) * 0.35f;
                sample = (sq + sw) * env * level * 0.20f;
                break;
            }
            case PartTimbre::WarmPad:
            {
                const float a = (float) std::sin (phase);
                const float b = (float) std::sin (phase2) * 0.55f;
                const float c = (float) std::sin (phase3) * 0.35f;
                sample = (a + b + c) * env * level * 0.18f;
                break;
            }
            case PartTimbre::Strings:
            {
                const float a = (float) std::sin (phase);
                const float b = (float) std::sin (phase * 1.003 + 0.2) * 0.7f;
                const float c = naiveSaw (phase2, dt * 2.0) * 0.12f * std::exp (-t * 2.0f);
                sample = (a + b + c) * env * level * 0.17f;
                break;
            }
            case PartTimbre::Pluck:
            {
                const float tone = (float) std::sin (phase);
                const float click = naiveSaw (phase2, dt * 3.0) * std::exp (-t * 28.0f) * 0.4f;
                sample = (tone + click) * env * level * 0.28f;
                break;
            }
            case PartTimbre::Sub808:
            {
                const double drop = freq * std::exp (-(double) age / (sampleRate * 0.12));
                phase += juce::MathConstants<double>::twoPi * drop / sampleRate;
                sample = (float) std::sin (phase) * env * level * 0.55f;
                // skip normal phase advance below
                left[i] += sample;
                if (right != nullptr) right[i] += sample;
                ++age;
                if (env < 0.001f && (releasing || age > (int) (sampleRate * 5)))
                {
                    clearCurrentNote();
                    return;
                }
                continue;
            }
            case PartTimbre::HouseBass:
            {
                const float fund = (float) std::sin (phase);
                const float click = naiveSaw (phase2, dt * 2.0) * std::exp (-t * 40.0f) * 0.25f;
                sample = (fund * 0.85f + click) * env * level * 0.40f;
                break;
            }
            case PartTimbre::AcousticBass:
            {
                const float fund = (float) std::sin (phase);
                const float h2 = (float) std::sin (phase2) * std::exp (-t * 8.0f) * 0.35f;
                sample = (fund + h2) * env * level * 0.32f;
                break;
            }
            case PartTimbre::HousePiano:
            {
                const float h1 = (float) std::sin (phase);
                const float h2 = (float) std::sin (phase2) * std::exp (-t * 8.0f) * 0.55f;
                const float h3 = (float) std::sin (phase3) * std::exp (-t * 14.0f) * 0.28f;
                const float bite = naiveSaw (phase, dt) * std::exp (-t * 22.0f) * 0.12f;
                sample = (h1 + h2 + h3 + bite) * env * level * 0.28f;
                break;
            }
            case PartTimbre::ClassicPiano:
            {
                const float h1 = (float) std::sin (phase);
                const float h2 = (float) std::sin (phase2) * std::exp (-t * 4.0f) * 0.40f;
                const float h3 = (float) std::sin (phase3) * std::exp (-t * 9.0f) * 0.18f;
                const float h4 = (float) std::sin (phase * 4.01) * std::exp (-t * 14.0f) * 0.08f;
                sample = (h1 + h2 + h3 + h4) * env * level * 0.30f;
                break;
            }
            case PartTimbre::SoftPiano:
            default:
            {
                const float h1 = (float) std::sin (phase);
                const float h2 = (float) std::sin (phase2) * std::exp (-t * 6.0f) * 0.45f;
                const float h3 = (float) std::sin (phase3) * std::exp (-t * 11.0f) * 0.22f;
                const float h4 = (float) std::sin (phase * 4.01) * std::exp (-t * 16.0f) * 0.10f;
                sample = (h1 + h2 + h3 + h4) * env * level * 0.30f;
                break;
            }
        }

        left[i] += sample;
        if (right != nullptr) right[i] += sample;

        phase  += freq  * w;
        phase2 += freq2 * w;
        phase3 += freq * 3.01 * w;
        if (phase  > juce::MathConstants<double>::twoPi) phase  -= juce::MathConstants<double>::twoPi;
        if (phase2 > juce::MathConstants<double>::twoPi) phase2 -= juce::MathConstants<double>::twoPi;
        if (phase3 > juce::MathConstants<double>::twoPi) phase3 -= juce::MathConstants<double>::twoPi;

        ++age;
        if (env < 0.001f && (releasing || age > (int) (sampleRate * 5)))
        {
            clearCurrentNote();
            break;
        }
    }
}

//==============================================================================
void PreviewSynth::DrumVoice::startNote (int midiNote, float velocity,
                                         juce::SynthesiserSound*, int)
{
    sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    level = juce::jlimit (0.05f, 1.0f, velocity);
    phase = 0.0;
    phaseB = phaseC = 0.0;
    env = 1.0f;
    age = 0;
    noise = 1u + (std::uint32_t) age; // any nonzero seed works
    kit = (DrumKitStyle) owner.getDrumKitStyle();
    kickBoom = 0.0f;
    useSample = false;
    samplePos = 0.0;
    sample.reset();

    const auto piece = PreviewSynth::drumPieceForMidiNote (midiNote);
    {
        const juce::SpinLock::ScopedLockType sl (owner.sampleLock);
        sample = owner.drumSamples[(size_t) piece];
    }
    if (sample != nullptr && sample->buffer.getNumSamples() > 0)
    {
        useSample = true;
        sampleInc = sample->sampleRate / juce::jmax (1.0, sampleRate);
        envDecay = 1.0f; // sample plays through; level only
        return;
    }

    switch (midiNote)
    {
        case 36: kind = Kind::Kick;  break;
        case 37:                      // rim / side stick — short snare tick
        case 38: kind = Kind::Snare; break;
        case 39: kind = Kind::Clap;  break;
        case 42: kind = Kind::Hat;   break;
        case 63:                      // congas — tonal snare-ish hits
        case 64: kind = Kind::Snare; break;
        case 46:
        case 51:                      // ride
        case 70:                      // shaker
        default: kind = Kind::Hat;   break;
    }

    // Kit-specific envelopes / pitch
    switch (kit)
    {
        case DrumKitStyle::HipHop:
            if (kind == Kind::Kick)
            {
                freq = 90.0;
                kickBoom = 1.0f;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.45));
            }
            else if (kind == Kind::Snare)
            {
                freq = 220.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.11));
            }
            else if (kind == Kind::Clap)
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.10));
            }
            else
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * (midiNote == 46 ? 0.18 : 0.035)));
            }
            break;

        case DrumKitStyle::Techno:
            if (kind == Kind::Kick)
            {
                freq = 170.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.14));
            }
            else if (kind == Kind::Snare)
            {
                freq = 200.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.09));
            }
            else if (kind == Kind::Clap)
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.08));
            }
            else
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * (midiNote == 46 ? 0.12 : 0.028)));
            }
            break;

        case DrumKitStyle::Classical:
            if (kind == Kind::Kick)
            {
                freq = 80.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.55));
                level *= 0.55f;
            }
            else if (kind == Kind::Snare)
            {
                freq = 140.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.20));
                level *= 0.45f;
            }
            else if (kind == Kind::Clap)
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.16));
                level *= 0.35f;
            }
            else
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * (midiNote == 46 ? 0.25 : 0.06)));
                level *= 0.30f;
            }
            break;

        case DrumKitStyle::Pop:
            if (kind == Kind::Kick)
            {
                freq = 130.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.22));
            }
            else if (kind == Kind::Snare)
            {
                freq = 190.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.13));
            }
            else if (kind == Kind::Clap)
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.11));
            }
            else
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * (midiNote == 46 ? 0.20 : 0.04)));
            }
            break;

        case DrumKitStyle::House:
        default:
            if (kind == Kind::Kick)
            {
                freq = 155.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.16));
            }
            else if (kind == Kind::Snare)
            {
                freq = 180.0;
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.12));
            }
            else if (kind == Kind::Clap)
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * 0.13));
                level *= 1.15f; // clap-forward house
            }
            else
            {
                envDecay = (float) std::exp (-1.0 / (sampleRate * (midiNote == 46 ? 0.20 : 0.04)));
            }
            break;
    }
}

void PreviewSynth::DrumVoice::stopNote (float, bool)
{
    // One-shots finish via envelope; ignore note-offs.
}

void PreviewSynth::DrumVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                               int startSample, int numSamples)
{
    if (getCurrentlyPlayingNote() < 0) return;

    auto* left  = outputBuffer.getWritePointer (0, startSample);
    auto* right = outputBuffer.getNumChannels() > 1
                      ? outputBuffer.getWritePointer (1, startSample) : nullptr;

    if (useSample && sample != nullptr)
    {
        const auto& buf = sample->buffer;
        const int n = buf.getNumSamples();
        const int chs = buf.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            if (samplePos >= n - 1)
            {
                clearCurrentNote();
                break;
            }

            const int i0 = (int) samplePos;
            const float frac = (float) (samplePos - (double) i0);
            const float a = buf.getSample (0, i0);
            const float b = buf.getSample (0, juce::jmin (i0 + 1, n - 1));
            float sL = (a + (b - a) * frac) * level;
            float sR = sL;
            if (chs > 1)
            {
                const float aR = buf.getSample (1, i0);
                const float bR = buf.getSample (1, juce::jmin (i0 + 1, n - 1));
                sR = (aR + (bR - aR) * frac) * level;
            }
            left[i] += sL * 1.15f;
            if (right != nullptr) right[i] += sR * 1.15f;
            samplePos += sampleInc;
            ++age;
        }
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        env *= envDecay;
        float sampleOut = 0.0f;

        if (kind == Kind::Kick)
        {
            const double decayTime = kickBoom > 0.5f ? 0.08 : 0.035;
            const double instantFreq = freq * std::exp (-(double) age / (sampleRate * decayTime));
            phase += juce::MathConstants<double>::twoPi * instantFreq / sampleRate;
            const float body = (float) std::sin (phase) * env * level * (kickBoom > 0.5f ? 1.05f : 0.90f);
            const float click = nextNoise (noise) * std::exp (-(float) age / (float) (sampleRate * 0.004))
                                * level * 0.15f;
            sampleOut = body + click;
        }
        else if (kind == Kind::Snare)
        {
            phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
            const float toneAmt = (kit == DrumKitStyle::HipHop) ? 0.25f : 0.35f;
            const float noiseAmt = 1.0f - toneAmt;
            sampleOut = ((float) std::sin (phase) * toneAmt + nextNoise (noise) * noiseAmt)
                     * env * level * 0.55f;
        }
        else if (kind == Kind::Clap)
        {
            const float burst = (age < (int) (sampleRate * 0.012)
                                 || (age > (int) (sampleRate * 0.018) && age < (int) (sampleRate * 0.028))
                                 || (age > (int) (sampleRate * 0.034) && age < (int) (sampleRate * 0.048)))
                                    ? 1.0f : 0.35f;
            sampleOut = nextNoise (noise) * env * burst * level * 0.50f;
        }
        else
        {
            if (kit == DrumKitStyle::Techno || kit == DrumKitStyle::House)
            {
                // 909-style metallic hat: two inharmonic square partials
                // ring-ish against filtered noise instead of plain white hiss.
                phaseB += juce::MathConstants<double>::twoPi * 3223.0 / sampleRate;
                phaseC += juce::MathConstants<double>::twoPi * 5837.0 / sampleRate;
                if (phaseB > juce::MathConstants<double>::twoPi) phaseB -= juce::MathConstants<double>::twoPi;
                if (phaseC > juce::MathConstants<double>::twoPi) phaseC -= juce::MathConstants<double>::twoPi;
                const float m1 = phaseB < juce::MathConstants<double>::pi ? 1.0f : -1.0f;
                const float m2 = phaseC < juce::MathConstants<double>::pi ? 1.0f : -1.0f;
                const float metal = (m1 * 0.5f + m2 * 0.35f) * 0.45f;
                sampleOut = (metal + nextNoise (noise) * 0.65f) * env * level * 0.34f;
            }
            else
            {
                sampleOut = nextNoise (noise) * env * level * 0.24f;
            }
        }

        left[i] += sampleOut;
        if (right != nullptr) right[i] += sampleOut;

        ++age;
        if (env < 0.001f)
        {
            clearCurrentNote();
            break;
        }
    }
}

} // namespace aimidi
