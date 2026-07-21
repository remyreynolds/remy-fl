#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace aimidi
{

AIMidiGenProcessor::AIMidiGenProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        parts[(size_t) t].type = (InstrumentType) t;
}

//==============================================================================
void AIMidiGenProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    previewPpqPos = 0.0;
    previewIdx.fill (0);
}

void AIMidiGenProcessor::generatePart (InstrumentType t)
{
    auto& p = parts[(size_t) t];
    if (p.locked) return;
    p = generator.generate (t, projectParams);
    rebuildPreviewSequences();
}

void AIMidiGenProcessor::regenerateFromAI (const juce::String& prompt,
                                           std::function<void (AIClient::Response)> onDone)
{
    std::vector<bool> locked ((size_t) InstrumentType::NumTypes, false);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        locked[(size_t) t] = parts[(size_t) t].locked;

    aiClient.sendPrompt (prompt, projectParams, locked,
        [this, onDone] (AIClient::Response r)
        {
            if (r.ok)
            {
                projectParams = r.params;
                auto toGen = r.toGenerate;
                if (toGen.empty())
                    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
                        toGen.push_back ((InstrumentType) t);

                for (auto t : toGen)
                    if (! parts[(size_t) t].locked)
                        parts[(size_t) t] = generator.generate (t, projectParams);

                rebuildPreviewSequences();
            }
            if (onDone) onDone (r);
        });
}

//==============================================================================
void AIMidiGenProcessor::rebuildPreviewSequences()
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        previewSeqs[(size_t) t] = MidiGenerator::toSequence (parts[(size_t) t],
                                                             projectParams.bpm);
    previewIdx.fill (0);
    previewPpqPos = 0.0;
}

void AIMidiGenProcessor::togglePreview (bool shouldPlay)
{
    previewing.store (shouldPlay);
    if (shouldPlay) { rebuildPreviewSequences(); }
}

//==============================================================================
void AIMidiGenProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi)
{
    buffer.clear();               // we never produce audio
    if (! previewing.load()) { return; }

    const double bpm          = projectParams.bpm;
    const double ppqPerSample = bpm / (60.0 * sampleRateHz);
    const double ticksPerQ    = MidiGenerator::ticksPerQuarter;
    const int    numSamples   = buffer.getNumSamples();

    const double loopLenQ = projectParams.bars * 4.0; // quarter notes in the loop

    for (int s = 0; s < numSamples; ++s)
    {
        const double ppq  = previewPpqPos + s * ppqPerSample;
        const double tick = std::fmod (ppq, loopLenQ) * ticksPerQ;

        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        {
            if (parts[(size_t) t].muted) continue;
            auto& seq = previewSeqs[(size_t) t];
            auto& idx = previewIdx[(size_t) t];

            // wrap
            if (tick < (idx > 0 ? seq.getEventTime (idx - 1) : 0.0))
                idx = 0;

            while (idx < seq.getNumEvents()
                   && seq.getEventTime (idx) <= tick)
            {
                midi.addEvent (seq.getEventPointer (idx)->message, s);
                ++idx;
            }
        }
    }

    previewPpqPos = std::fmod (previewPpqPos + numSamples * ppqPerSample, loopLenQ);
}

//==============================================================================
juce::AudioProcessorEditor* AIMidiGenProcessor::createEditor()
{
    return new AIMidiGenEditor (*this);
}

//==============================================================================
void AIMidiGenProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* root = new juce::DynamicObject();
    auto& p = projectParams;
    root->setProperty ("root", juce::String (p.root));
    root->setProperty ("scale", juce::String (p.scale));
    root->setProperty ("genre", juce::String (p.genre));
    root->setProperty ("bpm", p.bpm);
    root->setProperty ("bars", p.bars);
    root->setProperty ("octave", p.octave);
    root->setProperty ("energy", p.energy);
    root->setProperty ("complexity", p.complexity);

    const auto json = juce::JSON::toString (juce::var (root));
    dest.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void AIMidiGenProcessor::setStateInformation (const void* data, int size)
{
    auto v = juce::JSON::parse (juce::String::createStringFromData (data, size));
    if (! v.isObject()) return;
    auto& p = projectParams;
    p.root   = v.getProperty ("root",  juce::String (p.root)).toString().toStdString();
    p.scale  = v.getProperty ("scale", juce::String (p.scale)).toString().toStdString();
    p.genre  = v.getProperty ("genre", juce::String (p.genre)).toString().toStdString();
    p.bpm    = (double) v.getProperty ("bpm", p.bpm);
    p.bars   = (int) v.getProperty ("bars", p.bars);
    p.octave = (int) v.getProperty ("octave", p.octave);
    p.energy = (float) v.getProperty ("energy", p.energy);
    p.complexity = (float) v.getProperty ("complexity", p.complexity);
}

} // namespace aimidi

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aimidi::AIMidiGenProcessor();
}
