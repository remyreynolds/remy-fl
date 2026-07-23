#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/Critic.h"
#include "engine/ChatDirector.h"
#include "engine/SongPlan.h"
#include <algorithm>
#include <cmath>

namespace aimidi
{

AIMidiGenProcessor::AIMidiGenProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        parts[(size_t) t].type = (InstrumentType) t;
    for (auto& piece : drumKit)
        piece.type = InstrumentType::Drums;

    auto map = defaultsFor (genreMode);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        partTimbres[(size_t) t] = map.defaults[(size_t) t];
    applyMixDefaults();
    syncPreviewSounds();
}

//==============================================================================
void AIMidiGenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    previewPpqPos.store (0.0);
    previewIdx.fill (0);
    drumPreviewIdx.fill (0);
    previewSynth.prepare (sampleRate, samplesPerBlock);
    syncPreviewSounds();
    syncSamplesToSynth();
}

void AIMidiGenProcessor::releaseResources()
{
    previewSynth.allNotesOff();
}

void AIMidiGenProcessor::syncPreviewSounds()
{
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        previewSynth.setPartTimbre ((InstrumentType) t, partTimbres[(size_t) t]);
    previewSynth.setDrumKitStyle (defaultsFor (genreMode).drumKit);
}

void AIMidiGenProcessor::applyMixDefaults()
{
    auto mix = mixDefaultsFor (genreMode);
    partGains = mix.partGain;
    drumGains = mix.drumGain;
}

void AIMidiGenProcessor::syncSamplesToSynth()
{
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        std::shared_ptr<const LoadedSample> audio;
        const auto& id = drumSampleIds[(size_t) dp];
        if (id.isNotEmpty())
            if (auto* e = sampleLibrary.findById (id))
                audio = sampleLibrary.ensureLoaded (*e);
        previewSynth.setDrumSample ((DrumPiece) dp, audio);
    }

    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums) continue;
        std::shared_ptr<const LoadedSample> audio;
        const auto& id = partSampleIds[(size_t) t];
        if (id.isNotEmpty())
            if (auto* e = sampleLibrary.findById (id))
                audio = sampleLibrary.ensureLoaded (*e);
        previewSynth.setPartSample ((InstrumentType) t, audio);
    }
}

int AIMidiGenProcessor::countLoadedPreviewSamples() const
{
    int n = 0;
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        const auto& id = drumSampleIds[(size_t) dp];
        if (id.isEmpty()) continue;
        if (auto* e = sampleLibrary.findById (id))
            if (sampleLibrary.ensureLoaded (*e) != nullptr)
                ++n;
    }
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums) continue;
        const auto& id = partSampleIds[(size_t) t];
        if (id.isEmpty()) continue;
        if (auto* e = sampleLibrary.findById (id))
            if (sampleLibrary.ensureLoaded (*e) != nullptr)
                ++n;
    }
    return n;
}

void AIMidiGenProcessor::setDrumSampleId (DrumPiece dp, const juce::String& id)
{
    drumSampleIds[(size_t) dp] = id;
    std::shared_ptr<const LoadedSample> audio;
    if (id.isNotEmpty())
        if (auto* e = sampleLibrary.findById (id))
            audio = sampleLibrary.ensureLoaded (*e);
    previewSynth.setDrumSample (dp, audio);
}

void AIMidiGenProcessor::setPartSampleId (InstrumentType t, const juce::String& id)
{
    if (t == InstrumentType::Drums) return;
    partSampleIds[(size_t) t] = id;
    std::shared_ptr<const LoadedSample> audio;
    if (id.isNotEmpty())
        if (auto* e = sampleLibrary.findById (id))
            audio = sampleLibrary.ensureLoaded (*e);
    previewSynth.setPartSample (t, audio);
}

bool AIMidiGenProcessor::applyMidiLoopToPart (InstrumentType t, const juce::String& midiId,
                                              juce::String* errorOut)
{
    if (t == InstrumentType::Drums)
    {
        if (errorOut) *errorOut = "Use the Drums panel / Gen for drum MIDI.";
        return false;
    }

    auto& dest = parts[(size_t) t];
    if (dest.locked)
    {
        if (errorOut) *errorOut = "Part is locked.";
        return false;
    }

    if (midiId.isEmpty())
    {
        partMidiLoopIds[(size_t) t] = {};
        return true; // keep current notes; user can Generate
    }

    auto* entry = midiLibrary.findById (midiId);
    if (entry == nullptr)
    {
        if (errorOut) *errorOut = "MIDI loop not found.";
        return false;
    }

    GeneratedPart loaded;
    if (! MidiLibrary::loadFileToPart (entry->file, t, loaded, errorOut))
        return false;

    pushUndoSnapshot();
    loaded.locked = dest.locked;
    loaded.muted = dest.muted;
    dest = std::move (loaded);
    partMidiLoopIds[(size_t) t] = midiId;

    // Stretch project bars if the loop is longer
    double maxEnd = 0.0;
    for (auto& n : dest.notes)
        maxEnd = juce::jmax (maxEnd, n.startBeats + n.lengthBeats);
    const int needBars = juce::jmax (1, (int) std::ceil (maxEnd / 4.0));
    if (needBars > projectParams.bars)
        projectParams.bars = needBars;

    rebuildPreviewSequences();
    return true;
}

int AIMidiGenProcessor::applyMidiKitBundle (const juce::String& packName,
                                            const juce::String& kitTag,
                                            juce::String* errorOut)
{
    if (kitTag.isEmpty())
    {
        if (errorOut) *errorOut = "No kit selected.";
        return 0;
    }

    auto matchesKit = [&] (const MidiEntry& e) -> bool
    {
        if (packName.isNotEmpty() && e.packName != packName)
            return false;
        return e.name.startsWithIgnoreCase (kitTag + "_")
            || e.id.containsIgnoreCase ("/" + kitTag + "_")
            || e.id.containsIgnoreCase ("/" + kitTag + "/");
    };

    const MidiEntry* bass = nullptr;
    const MidiEntry* chords = nullptr;
    const MidiEntry* lead = nullptr;
    const MidiEntry* guitar = nullptr;

    for (auto& e : midiLibrary.all())
    {
        if (! matchesKit (e)) continue;
        const auto n = e.name.toLowerCase();
        if (bass == nullptr && n.contains ("bass")) bass = &e;
        else if (chords == nullptr && n.contains ("chord")) chords = &e;
        else if (lead == nullptr && n.contains ("lead")) lead = &e;
        else if (guitar == nullptr && (n.contains ("guitar") || n.contains ("melody")))
            guitar = &e;
    }

    for (auto& e : midiLibrary.all())
    {
        if (! matchesKit (e)) continue;
        if (bass == nullptr && e.role == MidiRole::Bass) bass = &e;
        if (chords == nullptr && e.role == MidiRole::Chords) chords = &e;
        if (lead == nullptr && e.role == MidiRole::Melody) lead = &e;
        if (guitar == nullptr && e.role == MidiRole::CounterMelody) guitar = &e;
    }

    pushUndoSnapshot();
    int loaded = 0;
    juce::String err;
    auto tryLoad = [&] (InstrumentType t, const MidiEntry* e)
    {
        if (e == nullptr) return;
        auto& dest = parts[(size_t) t];
        if (dest.locked) return;
        GeneratedPart part;
        if (! MidiLibrary::loadFileToPart (e->file, t, part, &err))
            return;
        part.locked = dest.locked;
        part.muted = dest.muted;
        dest = std::move (part);
        partMidiLoopIds[(size_t) t] = e->id;
        ++loaded;
    };

    tryLoad (InstrumentType::Bass, bass);
    tryLoad (InstrumentType::Chords, chords);
    tryLoad (InstrumentType::Melody, lead);
    tryLoad (InstrumentType::CounterMelody, guitar != nullptr ? guitar : lead);
    if (guitar != nullptr)
        tryLoad (InstrumentType::Arp, guitar);

    double maxEnd = 0.0;
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums) continue;
        for (auto& n : parts[(size_t) t].notes)
            maxEnd = juce::jmax (maxEnd, n.startBeats + n.lengthBeats);
    }
    const int needBars = juce::jmax (1, (int) std::ceil (maxEnd / 4.0));
    if (needBars > projectParams.bars)
        projectParams.bars = needBars;

    for (auto* e : { bass, chords, lead, guitar })
    {
        if (e == nullptr) continue;
        const auto lower = e->name.toLowerCase();
        const int bpmIdx = lower.indexOf ("bpm");
        if (bpmIdx > 0)
        {
            int i = bpmIdx - 1;
            while (i >= 0 && (lower[i] == '_' || lower[i] == ' ')) --i;
            const int end = i;
            while (i >= 0 && juce::CharacterFunctions::isDigit (lower[i])) --i;
            const int bpm = lower.substring (i + 1, end + 1).getIntValue();
            if (bpm >= 60 && bpm <= 200)
                projectParams.bpm = (double) bpm;
        }

        auto toks = juce::StringArray::fromTokens (e->name, "_", "");
        if (toks.size() > 0)
        {
            auto keyTok = toks[toks.size() - 1];
            static const char* keys[] = { "C", "C#", "Db", "D", "D#", "Eb", "E", "F",
                                          "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B" };
            for (auto* k : keys)
                if (keyTok.equalsIgnoreCase (k))
                {
                    projectParams.root = k;
                    break;
                }
        }
        break;
    }

    rebuildPreviewSequences();
    if (loaded == 0 && errorOut)
        *errorOut = "No Bass/Chord/Lead/Guitar MIDI found for " + kitTag;
    return loaded;
}

juce::String AIMidiGenProcessor::buildMidiPackMemoryDoc (const juce::String& packName) const
{
    const auto name = packName.isNotEmpty() ? packName : juce::String ("MIDI packs");
    juce::String body;
    body << "# MIDI pack memory: " << name << "\n\n";
    body << "Local MIDI loops the user can load onto Melody / Bass / Chords / Counter / Arp.\n";
    body << "Drums still use the audio drum kit — these are pitched-instrument MIDI only.\n\n";

    body << "## Inventory\n";
    int roles[(int) MidiRole::NumRoles] {};
    for (auto& e : midiLibrary.all())
    {
        if (packName.isNotEmpty() && e.packName != packName) continue;
        roles[(int) e.role]++;
    }
    for (int r = 0; r < (int) MidiRole::NumRoles; ++r)
        if (roles[r] > 0)
            body << "- " << toString ((MidiRole) r) << ": " << roles[r] << " loops\n";

    auto kits = midiLibrary.kitBundleNames (packName);
    if (kits.size() > 0)
    {
        body << "\n## Kit bundles (load together)\n";
        for (auto& k : kits)
            body << "- " << k << " → Bass + Chords + Lead (Melody) + Guitar (Counter)\n";
    }

    body << "\n## File list\n";
    for (auto& e : midiLibrary.all())
    {
        if (packName.isNotEmpty() && e.packName != packName) continue;
        body << "- [" << toString (e.role) << "] " << e.name << "\n";
    }

    body << R"(
## Funky house usage (theory)
- Typical tempo ~118–124 BPM; many of these kits are 120 BPM.
- Bass: syncopated / groovy; leave space on downbeats for the kick.
- Chords: short stabs or rhythmic comps; lock with clap on 2 and 4.
- Lead: catchy top line; sit above chords without fighting the vocal range.
- Guitar: often a counter-melody or funk rhythm line — use Counter Melody or Arp.
- When the user asks to "use the funky house MIDI pack", load a Kit_N bundle then generate drums separately from the drum sample pack.
)";
    return body;
}

bool AIMidiGenProcessor::rememberMidiPackInBrain (const juce::String& packName)
{
    juce::String err;
    return aiClient.knowledge().upsertText ("midi-pack-memory",
                                            "MIDI pack memory",
                                            buildMidiPackMemoryDoc (packName),
                                            &err);
}

bool AIMidiGenProcessor::hasAnySampleAssignment() const
{
    for (auto& id : drumSampleIds)
        if (id.isNotEmpty()) return true;
    for (auto& id : partSampleIds)
        if (id.isNotEmpty()) return true;
    return false;
}

int AIMidiGenProcessor::autoAssignSamplesFromLibrary (const juce::String& packName)
{
    int assigned = 0;
    auto pick = [&] (SampleRole role) -> juce::String
    {
        const SampleEntry* e = packName.isNotEmpty()
                                   ? sampleLibrary.firstForRoleInPack (role, packName)
                                   : sampleLibrary.firstForRole (role);
        return e != nullptr ? e->id : juce::String();
    };

    // Drum kits only feed the Drums section — never melody/bass/chords.
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        if ((InstrumentType) t != InstrumentType::Drums)
            setPartSampleId ((InstrumentType) t, {});

    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        auto id = pick (roleForDrumPiece ((DrumPiece) dp));
        if (id.isEmpty() && (DrumPiece) dp == DrumPiece::Clap)
            id = pick (SampleRole::Snare);
        if (id.isEmpty())
            id = pick (SampleRole::Perc);
        if (id.isEmpty() && packName.isNotEmpty())
        {
            auto all = sampleLibrary.samplesForPack (packName);
            if (! all.empty() && all.front() != nullptr)
                id = all.front()->id;
        }
        if (id.isNotEmpty())
        {
            setDrumSampleId ((DrumPiece) dp, id);
            ++assigned;
        }
    }

    syncSamplesToSynth();
    return assigned;
}

juce::String AIMidiGenProcessor::buildDrumKitMemoryDoc (const juce::String& packName) const
{
    const auto name = packName.isNotEmpty() ? packName : juce::String ("Active drum kit");
    juce::String body;
    body << "# Active drum kit memory: " << name << "\n\n";
    body << "This document is the plugin's memory of the loaded drum sample pack.\n";
    body << "Use it when generating or discussing DRUMS only. Melodies, bass, and chords "
            "use synth / theory — not these one-shot drum files.\n\n";

    body << "## Kit inventory\n";
    int roles[(int) SampleRole::NumRoles] {};
    for (auto& e : sampleLibrary.all())
    {
        if (packName.isNotEmpty() && e.packName != packName)
            continue;
        roles[(int) e.role]++;
    }
    for (int r = 0; r < (int) SampleRole::NumRoles; ++r)
        if (roles[r] > 0)
            body << "- " << toString ((SampleRole) r) << ": " << roles[r] << " sounds\n";

    body << "\n## Assigned preview mapping\n";
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        const auto id = drumSampleIds[(size_t) dp];
        body << "- " << toString ((DrumPiece) dp) << " → "
             << (id.isNotEmpty() ? id : juce::String ("(synth)")) << "\n";
    }

    body << R"(
## Drum-loop theory (read this when writing drum MIDI)

### Deep house / tech house groove
- Kick: four-on-the-floor (beats 1, 2, 3, 4) or every quarter note.
- Clap / snare: on beats 2 and 4 (or clap on 2+4, snare lighter/ghost).
- Closed hats: busy 16ths; often the loudest metallic element.
- Open hat: offbeat "and" of each beat (the classic house tss).
- Keep percussion sparse — shakers/toms as ear candy, not every bar.

### Hip-hop
- Kick syncopated; snare on 2 and 4; hats 8ths or 16ths with swing.
- Leave space — don't fill every 16th.

### Writing rules
- Only generate drum MIDI for Kick / Snare / Clap / Closed Hat / Open Hat pieces.
- Do not invent melodic pitched content from drum one-shots.
- Match velocities to the preview-mix-guide (hats hot in house).
- Loop length usually 4 or 8 bars; patterns should feel loopable.
)";
    return body;
}

bool AIMidiGenProcessor::rememberDrumKitInBrain (const juce::String& packName)
{
    const auto body = buildDrumKitMemoryDoc (packName);
    juce::String err;
    return aiClient.knowledge().upsertText ("drum-kit-memory",
                                            "Active drum kit memory",
                                            body, &err);
}

void AIMidiGenProcessor::setPartGain (InstrumentType t, float gain01)
{
    partGains[(size_t) t] = juce::jlimit (0.0f, 1.25f, gain01);
}

void AIMidiGenProcessor::setDrumGain (DrumPiece dp, float gain01)
{
    drumGains[(size_t) dp] = juce::jlimit (0.0f, 1.25f, gain01);
}

void AIMidiGenProcessor::setGenreMode (GenreMode mode, bool applyDefaults)
{
    genreMode = mode;
    projectParams.genre = toString (mode);

    if (applyDefaults)
    {
        auto map = defaultsFor (mode);
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
            partTimbres[(size_t) t] = map.defaults[(size_t) t];
        applyMixDefaults();
    }

    syncPreviewSounds();
}

void AIMidiGenProcessor::setPartTimbre (InstrumentType t, PartTimbre timbre)
{
    if (t == InstrumentType::Drums) return;
    partTimbres[(size_t) t] = timbre;
    previewSynth.setPartTimbre (t, timbre);
}

bool AIMidiGenProcessor::autoDetectGenreFromText (const juce::String& text)
{
    GenreMode detected;
    if (! detectGenreFromText (text, detected))
        return false;
    if (detected == genreMode)
        return false;
    setGenreMode (detected, true);
    return true;
}

void AIMidiGenProcessor::pushUndoSnapshot()
{
    PartSnapshot snap;
    snap.params = projectParams;
    snap.parts = parts;
    snap.drumKit = drumKit;
    snap.genreMode = genreMode;
    snap.partTimbres = partTimbres;
    snap.partGains = partGains;
    snap.drumGains = drumGains;
    snap.criticSummary = criticSummary;
    undoStack.push_back (std::move (snap));
    constexpr size_t kMax = 20;
    if (undoStack.size() > kMax)
        undoStack.erase (undoStack.begin(),
                         undoStack.begin() + (ptrdiff_t) (undoStack.size() - kMax));
}

bool AIMidiGenProcessor::undoLastGeneration()
{
    if (undoStack.empty())
        return false;

    auto snap = std::move (undoStack.back());
    undoStack.pop_back();
    projectParams = snap.params;
    parts = std::move (snap.parts);
    drumKit = std::move (snap.drumKit);
    genreMode = snap.genreMode;
    partTimbres = snap.partTimbres;
    partGains = snap.partGains;
    drumGains = snap.drumGains;
    criticSummary = snap.criticSummary;
    syncPreviewSounds();
    rebuildDrumMasterPart();
    rebuildPreviewSequences();
    return true;
}

void AIMidiGenProcessor::applyMidiPattern (MidiPattern& pattern, juce::String& errorOut)
{
    errorOut.clear();
    auto lanes = pattern.lanes();
    if (lanes.empty())
    {
        errorOut = "AI MIDI pattern had no parts.";
        return;
    }

    int unlocked = 0;
    for (const auto& lane : lanes)
    {
        const auto type = instrumentTypeFromName (lane.instrument);
        if (! parts[(size_t) type].locked)
            ++unlocked;
    }
    if (unlocked == 0)
    {
        errorOut = "All target parts are locked — unlock at least one to apply AI MIDI.";
        return;
    }

    pushUndoSnapshot();

    // Adopt tempo/length only when the pattern carries sane values — never
    // let a malformed AI reply silently trash the project tempo.
    if (pattern.bpm > 0.0)
        setBpm (pattern.bpm);
    if (pattern.bars > 0)
        projectParams.bars = juce::jlimit (1, 32, pattern.bars);
    pattern.key = juce::String (formatKeyString (projectParams));

    juce::StringArray applied;
    juce::StringArray skipped;
    for (const auto& lane : lanes)
    {
        const auto type = instrumentTypeFromName (lane.instrument);
        auto& dest = parts[(size_t) type];
        if (dest.locked)
        {
            skipped.add (toString (type));
            continue;
        }

        dest = patternPartToGeneratedPart (lane, pattern.bpm, pattern.bars);
        applied.add (toString (type));

        if (type == InstrumentType::Drums)
        {
            // Split the AI drum lane into the kit by GM pitch, so each piece
            // (kick/snare/hats/perc) gets ITS notes — not everything into Kick.
            for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
            {
                auto& piece = drumKit[(size_t) dp];
                if (piece.locked)
                    continue;
                piece.notes.clear();
                piece.type = InstrumentType::Drums;
                const int gmNote = drumPieceMidiNote ((DrumPiece) dp);
                for (const auto& n : dest.notes)
                    if (n.pitch == gmNote)
                        piece.notes.push_back (n);
            }
            // Anything on an unmapped GM pitch lands in the closest piece: kick.
            {
                auto& kick = drumKit[(size_t) DrumPiece::Kick];
                if (! kick.locked)
                    for (const auto& n : dest.notes)
                    {
                        bool mapped = false;
                        for (int dp = 0; dp < (int) DrumPiece::NumPieces && ! mapped; ++dp)
                            mapped = (n.pitch == drumPieceMidiNote ((DrumPiece) dp));
                        if (! mapped)
                            kick.notes.push_back (n);
                    }
            }
            rebuildDrumMasterPart();
        }
    }

    rebuildPreviewSequences();

    // Soft warning only — generation still succeeded for unlocked parts.
    if (skipped.size() > 0 && applied.size() > 0)
        errorOut = "Applied " + applied.joinIntoString (", ")
                 + " (skipped locked: " + skipped.joinIntoString (", ") + ")";
}

void AIMidiGenProcessor::generatePart (InstrumentType t, bool recordUndo)
{
    if (t == InstrumentType::Drums)
    {
        generateDrumKit (recordUndo);
        return;
    }

    auto& p = parts[(size_t) t];
    if (p.locked) return;
    if (recordUndo) pushUndoSnapshot();
    p = generator.generate (t, projectParams);
    runCritic();
    rebuildPreviewSequences();
}

void AIMidiGenProcessor::generateAllParts (bool recordUndo)
{
    if (recordUndo) pushUndoSnapshot();
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        generatePart ((InstrumentType) t, false);
}

juce::String AIMidiGenProcessor::currentSongPlanFingerprint() const
{
    const auto& st = findStyle (projectParams.genre);
    int tones = st.chordTones;
    if (projectParams.chordComplexity > 0.75f) ++tones;
    if (projectParams.chordComplexity < 0.25f) --tones;
    tones = std::clamp (tones, 3, 6);
    return juce::String (songPlanFingerprint (buildSongPlan (projectParams, tones)));
}

bool AIMidiGenProcessor::rollSeedUntilFingerprintChanges (const juce::String& previousFp,
                                                          int maxTries)
{
    if (previousFp.isEmpty())
    {
        projectParams.seed = 1u + (unsigned int) juce::Random::getSystemRandom().nextInt (0x7fffffff);
        return true;
    }

    for (int i = 0; i < maxTries; ++i)
    {
        projectParams.seed = 1u + (unsigned int) juce::Random::getSystemRandom().nextInt (0x7fffffff);
        if (currentSongPlanFingerprint() != previousFp)
            return true;
    }
    // Last resort: bump seed until degree template index moves.
    projectParams.seed += 17u;
    return currentSongPlanFingerprint() != previousFp;
}

void AIMidiGenProcessor::recordOfflineGeneration (const juce::String& reasonLabel)
{
    lastGeneration.mode = GenerationMode::OfflineLocal;
    lastGeneration.ok = true;
    lastGeneration.detail.clear();
    lastGeneration.fingerprint = currentSongPlanFingerprint().toStdString();
    lastHarmonyFp = juce::String (lastGeneration.fingerprint);
    lastGeneration.statusLine = reasonLabel.toStdString();
    pendingLocalOffer = false;
    pendingLocalAction = PendingLocalAction::None;
    aiClient.rememberHarmonyFingerprint (lastHarmonyFp);
}

void AIMidiGenProcessor::recordClaudeSuccess (const MidiPattern& pattern,
                                              const juce::String& assistant)
{
    lastGeneration.mode = GenerationMode::ClaudeBrain;
    lastGeneration.ok = true;
    lastGeneration.detail.clear();
    lastGeneration.fingerprint = chordProgressionFingerprint (pattern).toStdString();
    if (lastGeneration.fingerprint.empty())
        lastGeneration.fingerprint = currentSongPlanFingerprint().toStdString();
    lastHarmonyFp = juce::String (lastGeneration.fingerprint);
    lastGeneration.statusLine = "Generated with Claude";
    if (assistant.isNotEmpty())
        lastGeneration.detail = assistant.toStdString();
    pendingLocalOffer = false;
    pendingLocalAction = PendingLocalAction::None;
}

void AIMidiGenProcessor::recordClaudeFailure (const juce::String& error,
                                              PendingLocalAction action,
                                              InstrumentType lane)
{
    lastGeneration.mode = GenerationMode::FailedClaude;
    lastGeneration.ok = false;
    lastGeneration.statusLine = "Claude failed";
    lastGeneration.detail = error.toStdString();
    pendingLocalOffer = true;
    pendingLocalAction = action;
    pendingLocalLane = lane;
}

juce::String AIMidiGenProcessor::buildClaudeGeneratePrompt (InstrumentType focusOrAll) const
{
    const auto key = juce::String (formatKeyString (projectParams));
    juce::String prompt;
    prompt << "Compose a house MIDI loop using the Groovewright Brain rules.\n"
           << "Genre/style: " << projectParams.genre << "\n"
           << "Key: " << key << "\n"
           << "BPM: " << (int) projectParams.bpm << "\n"
           << "Bars: " << projectParams.bars << "\n"
           << "Seed: " << (juce::int64) projectParams.seed << "\n"
           << "Energy: " << projectParams.energy
           << "  Complexity: " << projectParams.complexity
           << "  ChordComplexity: " << projectParams.chordComplexity << "\n";

    if (focusOrAll == InstrumentType::NumTypes)
    {
        prompt << "Return the FULL-LOOP parts[] schema with chords, bass, melody, and drums "
                  "(add pad/arp if it fits the style).\n";
    }
    else
    {
        prompt << "Focus instrument: " << toString (focusOrAll)
               << ". Return a single-part OR parts[] that includes this instrument.\n";
    }

    prompt << "You MUST include explicit harmony metadata:\n"
              "  \"progression\": short roman/degree summary (e.g. \"i–VI–III–VII\"),\n"
              "  \"chords\": array of chord symbols per bar (e.g. [\"Fm7\",\"Dbmaj7\",\"Eb7\",\"Cm7\"]).\n"
              "Chord lane notes must realize those chords (stacked tones sharing startBeat).\n"
              "Stay strictly in the project key unless Brain docs intentionally request borrowed harmony.\n"
              "Make this seed feel distinct from recent progressions listed in the request.";
    return prompt;
}

void AIMidiGenProcessor::generateAllPartsOffline (const juce::String& reasonLabel)
{
    generateAllParts();
    recordOfflineGeneration (reasonLabel);
}

void AIMidiGenProcessor::generatePartOffline (InstrumentType t, bool recordUndo,
                                              const juce::String& reasonLabel)
{
    generatePart (t, recordUndo);
    recordOfflineGeneration (reasonLabel);
}

void AIMidiGenProcessor::noteOfflineResult (const juce::String& reasonLabel)
{
    recordOfflineGeneration (reasonLabel);
}

void AIMidiGenProcessor::generatePreferredAll (std::function<void (GenerationReport)> onDone)
{
    const bool useClaude = ! preferOfflineGeneration && aiClient.hasApiKey();
    if (! useClaude)
    {
        const auto label = preferOfflineGeneration
            ? "Generated locally — offline mode"
            : "Generated locally — no API key";
        generateAllPartsOffline (label);
        if (onDone) onDone (lastGeneration);
        return;
    }

    aiClient.setProjectContext (buildProjectContextBrief());
    regenerateFromAI (buildClaudeGeneratePrompt (InstrumentType::NumTypes),
        [this, onDone] (AIClient::PatternResponse r)
        {
            if (! r.ok)
            {
                recordClaudeFailure (r.error.isNotEmpty() ? r.error
                                                          : juce::String ("Claude request failed."),
                                     PendingLocalAction::All);
                if (onDone) onDone (lastGeneration);
                return;
            }
            recordClaudeSuccess (r.pattern, r.assistantText);
            if (onDone) onDone (lastGeneration);
        });
}

void AIMidiGenProcessor::generatePreferredLane (InstrumentType t,
                                                std::function<void (GenerationReport)> onDone)
{
    if (t == InstrumentType::Drums)
    {
        generateDrumKit();
        recordOfflineGeneration (preferOfflineGeneration
                                     ? "Generated locally — offline mode"
                                     : (aiClient.hasApiKey()
                                            ? "Generated locally — drum kit engine"
                                            : "Generated locally — no API key"));
        if (onDone) onDone (lastGeneration);
        return;
    }

    const bool useClaude = ! preferOfflineGeneration && aiClient.hasApiKey();
    if (! useClaude)
    {
        const auto label = preferOfflineGeneration
            ? "Generated locally — offline mode"
            : "Generated locally — no API key";
        generatePartOffline (t, true, label);
        if (onDone) onDone (lastGeneration);
        return;
    }

    aiClient.setProjectContext (buildProjectContextBrief());
    regenerateFromAI (buildClaudeGeneratePrompt (t),
        [this, onDone, t] (AIClient::PatternResponse r)
        {
            if (! r.ok)
            {
                recordClaudeFailure (r.error.isNotEmpty() ? r.error
                                                          : juce::String ("Claude request failed."),
                                     PendingLocalAction::Lane, t);
                if (onDone) onDone (lastGeneration);
                return;
            }
            recordClaudeSuccess (r.pattern, r.assistantText);
            if (onDone) onDone (lastGeneration);
        });
}

void AIMidiGenProcessor::newIdeaPreferred (std::function<void (GenerationReport)> onDone)
{
    const auto previousFp = lastHarmonyFp.isNotEmpty() ? lastHarmonyFp
                                                       : currentSongPlanFingerprint();
    (void) rollSeedUntilFingerprintChanges (previousFp);

    const bool useClaude = ! preferOfflineGeneration && aiClient.hasApiKey();
    if (! useClaude)
    {
        const auto label = preferOfflineGeneration
            ? "Generated locally — offline mode"
            : "Generated locally — no API key";
        generateAllPartsOffline (label);
        // Guarantee New Idea differs from the previous local fingerprint.
        if (lastHarmonyFp == previousFp && previousFp.isNotEmpty())
        {
            (void) rollSeedUntilFingerprintChanges (previousFp);
            generateAllParts();
            recordOfflineGeneration (label);
        }
        if (onDone) onDone (lastGeneration);
        return;
    }

    aiClient.setProjectContext (buildProjectContextBrief());
    auto prompt = buildClaudeGeneratePrompt (InstrumentType::NumTypes);
    prompt << "\nThis is a NEW IDEA request. Seed changed to "
           << (juce::int64) projectParams.seed
           << ". Produce a meaningfully different harmonic fingerprint from recent results.";

    regenerateFromAI (prompt,
        [this, onDone, previousFp] (AIClient::PatternResponse r)
        {
            if (! r.ok)
            {
                recordClaudeFailure (r.error.isNotEmpty() ? r.error
                                                          : juce::String ("Claude request failed."),
                                     PendingLocalAction::NewIdea);
                if (onDone) onDone (lastGeneration);
                return;
            }
            recordClaudeSuccess (r.pattern, r.assistantText);
            if (previousFp.isNotEmpty()
                && lastHarmonyFp == previousFp)
            {
                // Claude returned the same fingerprint after internal retry —
                // surface as failure rather than pretending it's a new idea.
                recordClaudeFailure ("Claude returned the same chord progression as the previous idea.",
                                     PendingLocalAction::NewIdea);
            }
            if (onDone) onDone (lastGeneration);
        });
}

void AIMidiGenProcessor::varyChordsWithAI (std::function<void (AIClient::PatternResponse)> onDone)
{
    auto& chords = parts[(size_t) InstrumentType::Chords];
    if (chords.notes.empty())
    {
        AIClient::PatternResponse r;
        r.ok = false;
        r.error = "Generate chords first, then Vary.";
        if (onDone) juce::MessageManager::callAsync ([onDone, r] { onDone (r); });
        return;
    }
    if (chords.locked)
    {
        AIClient::PatternResponse r;
        r.ok = false;
        r.error = "Chords are locked — unlock to vary.";
        if (onDone) juce::MessageManager::callAsync ([onDone, r] { onDone (r); });
        return;
    }

    const auto ctx = serializePartAsMidiContext (chords, projectParams);
    const auto forcedKey = juce::String (formatKeyString (projectParams));
    juce::String prompt;
    prompt << "Vary Chords: create a fresh variation of this chord progression.\n"
           << "HARD LOCKS — preserve exactly:\n"
           << "  key: " << forcedKey << "\n"
           << "  genre/style: " << projectParams.genre << "\n"
           << "  BPM: " << (int) projectParams.bpm << "\n"
           << "  bars: " << projectParams.bars << "\n"
           << "Change the harmonic route (degrees, extensions, inversions, rhythm) enough to feel new.\n"
           << "Include \"progression\" and \"chords\" metadata in the JSON.\n"
           << "Set JSON instrument to \"chords\".\n\n"
           << "EXISTING CHORDS TO VARY:\n" << ctx;

    aiClient.setProjectContext (buildProjectContextBrief());
    aiClient.requestMidiPattern (prompt, forcedKey,
        [this, weakThis = juce::WeakReference<AIMidiGenProcessor> (this), onDone]
        (AIClient::PatternResponse r)
        {
            if (weakThis == nullptr)
                return; // processor destroyed while the request was in flight

            if (r.ok)
            {
                juce::String err;
                applyMidiPattern (r.pattern, err);
                if (err.isNotEmpty() && ! err.startsWithIgnoreCase ("Applied "))
                {
                    r.ok = false;
                    r.error = err;
                }
                else if (r.ok)
                {
                    runCritic();
                    rebuildPreviewSequences();
                    recordClaudeSuccess (r.pattern, r.assistantText);
                    r.assistantText = "Generated with Claude — " + r.assistantText;
                }
            }
            if (! r.ok)
                recordClaudeFailure (r.error.isNotEmpty() ? r.error
                                                          : juce::String ("Vary chords failed."),
                                     PendingLocalAction::Lane, InstrumentType::Chords);
            if (onDone) onDone (r);
        });
}

void AIMidiGenProcessor::useLocalGeneratorNow (std::function<void (GenerationReport)> onDone)
{
    const auto action = pendingLocalAction;
    const auto lane = pendingLocalLane;
    pendingLocalOffer = false;
    pendingLocalAction = PendingLocalAction::None;

    const juce::String label = "Generated locally — offline fallback chosen";
    switch (action)
    {
        case PendingLocalAction::Lane:
            generatePartOffline (lane, true, label);
            break;
        case PendingLocalAction::NewIdea:
            (void) rollSeedUntilFingerprintChanges (lastHarmonyFp);
            generateAllPartsOffline (label);
            break;
        case PendingLocalAction::All:
        case PendingLocalAction::None:
        default:
            generateAllPartsOffline (label);
            break;
    }
    if (onDone) onDone (lastGeneration);
}

void AIMidiGenProcessor::regenerateFromAI (const juce::String& prompt,
                                           std::function<void (AIClient::PatternResponse)> onDone)
{
    const auto forcedKey = juce::String (formatKeyString (projectParams));
    aiClient.setProjectContext (buildProjectContextBrief()); // ground the AI in the live project
    aiClient.requestMidiPattern (prompt, forcedKey,
        [this, weakThis = juce::WeakReference<AIMidiGenProcessor> (this), prompt, onDone]
        (AIClient::PatternResponse r)
        {
            if (weakThis == nullptr)
                return; // processor destroyed while the request was in flight

            if (r.ok)
            {
                (void) autoDetectGenreFromText (prompt + " " + juce::String (projectParams.genre));

                juce::String err;
                applyMidiPattern (r.pattern, err);
                if (err.isNotEmpty() && ! err.startsWithIgnoreCase ("Applied "))
                {
                    r.ok = false;
                    r.error = err;
                }
                else if (err.isNotEmpty())
                {
                    r.assistantText = (r.assistantText.isNotEmpty() ? r.assistantText + "\n" : juce::String())
                                    + err;
                }
            }

            if (r.ok)
            {
                // Cross-part critic: the AI path must not dump unchecked
                // MIDI into the roll.
                runCritic();
                rebuildPreviewSequences();
            }

            if (onDone) onDone (r);
        });
}

void AIMidiGenProcessor::transformPartWithAI (InstrumentType t,
                                              const juce::String& mode,
                                              std::function<void (AIClient::PatternResponse)> onDone)
{
    const auto& src = parts[(size_t) t];
    if (src.notes.empty())
    {
        AIClient::PatternResponse r;
        r.ok = false;
        r.error = juce::String ("Generate ") + toString (t) + " first, then Vary / Continue.";
        if (onDone) juce::MessageManager::callAsync ([onDone, r] { onDone (r); });
        return;
    }

    if (src.locked)
    {
        AIClient::PatternResponse r;
        r.ok = false;
        r.error = juce::String (toString (t)) + " is locked — unlock to transform.";
        if (onDone) juce::MessageManager::callAsync ([onDone, r] { onDone (r); });
        return;
    }

    const auto ctx = serializePartAsMidiContext (src, projectParams);
    const auto forcedKey = juce::String (formatKeyString (projectParams));
    aiClient.requestMidiTransform (mode, toString (t), ctx, forcedKey,
        [this, weakThis = juce::WeakReference<AIMidiGenProcessor> (this), onDone, t]
        (AIClient::PatternResponse r)
        {
            if (weakThis == nullptr)
                return; // processor destroyed while the request was in flight

            if (r.ok)
            {
                juce::String err;
                applyMidiPattern (r.pattern, err);
                if (err.isNotEmpty() && ! err.startsWithIgnoreCase ("Applied "))
                {
                    r.ok = false;
                    r.error = err;
                }
                else if (err.isNotEmpty())
                {
                    r.assistantText = (r.assistantText.isNotEmpty() ? r.assistantText + "\n" : juce::String())
                                    + err;
                }

                if (r.ok)
                {
                    runCritic();
                    rebuildPreviewSequences();
                    recordClaudeSuccess (r.pattern, r.assistantText);
                }
            }
            if (! r.ok)
                recordClaudeFailure (r.error.isNotEmpty() ? r.error
                                                          : juce::String ("Transform failed."),
                                     PendingLocalAction::Lane, t);
            if (onDone) onDone (r);
        });
}

juce::String AIMidiGenProcessor::midiContextForPart (InstrumentType t) const
{
    return serializePartAsMidiContext (parts[(size_t) t], projectParams);
}

juce::File AIMidiGenProcessor::exportAllPartsMidiFile() const
{
    std::vector<const GeneratedPart*> list;
    for (int i = 0; i < (int) InstrumentType::NumTypes; ++i)
    {
        const auto& p = parts[(size_t) i];
        if (! p.notes.empty())
            list.push_back (&p);
    }
    return MidiGenerator::writeTempMultiTrackMidiFile (list, projectParams, "AIMidiGen_All");
}

void AIMidiGenProcessor::handleChatTurn (const juce::String& prompt,
                                         std::function<void (AIClient::TurnResponse)> onDone)
{
    // Layer 1 — instant local commands: no network, no LLM, ~0 ms.
    {
        AIClient::TurnResponse local;
        if (tryLocalChatCommand (prompt, local))
        {
            if (onDone) onDone (std::move (local));
            return;
        }
    }

    // Layer 2 — real conversation: ground the AI in the live project state.
    aiClient.setProjectContext (buildProjectContextBrief());

    const auto forcedKey = juce::String (formatKeyString (projectParams));
    aiClient.handleUserTurn (prompt, forcedKey,
        [this, weakThis = juce::WeakReference<AIMidiGenProcessor> (this), prompt, onDone]
        (AIClient::TurnResponse r)
        {
            if (weakThis == nullptr)
                return; // processor destroyed while the request was in flight

            if (r.ok && r.generatedMidi)
            {
                (void) autoDetectGenreFromText (prompt);

                juce::String err;
                applyMidiPattern (r.pattern, err);
                if (err.isNotEmpty() && ! err.startsWithIgnoreCase ("Applied "))
                {
                    r.ok = false;
                    r.generatedMidi = false;
                    r.error = err;
                }
                else if (err.isNotEmpty())
                {
                    r.assistantText = (r.assistantText.isNotEmpty() ? r.assistantText + "\n" : juce::String())
                                    + err;
                }

                if (r.ok)
                {
                    runCritic();
                    rebuildPreviewSequences();
                }
            }

            if (onDone) onDone (std::move (r));
        });
}

//==============================================================================
bool AIMidiGenProcessor::tryLocalChatCommand (const juce::String& text,
                                              AIClient::TurnResponse& out)
{
    const auto intent = parseChatIntent (text.toStdString());
    using CA = ChatAction;

    if (intent.action == CA::None || intent.action == CA::Conversation)
        return false; // real conversation -> AI

    out.ok = true;

    if (intent.action == CA::Help)
    {
        out.assistantText = juce::String::fromUTF8 (chatDirectorHelpText());
        return true;
    }

    if (intent.action == CA::Undo)
    {
        if (undoLastGeneration())
        {
            out.generatedMidi = true; // tells the editor to refresh the roll
            out.assistantText = "Undone - restored the previous take.";
        }
        else
            out.assistantText = "Nothing to undo yet.";
        return true;
    }

    juce::StringArray done;

    // One undo snapshot BEFORE any mutation, so "undo" reverts the param
    // changes (style/key/tempo/dials) as well as the regenerated notes.
    pushUndoSnapshot();

    // ---- Parameter changes first, so any generation below uses them ----
    if (! intent.genre.empty())
    {
        (void) autoDetectGenreFromText (juce::String (intent.genre)); // timbres + mix
        projectParams.genre = intent.genre;      // keep the specific style name
        const auto& st = findStyle (intent.genre);
        projectParams.swing = st.swing;
        projectParams.scale = intent.scale.empty() ? st.scale : intent.scale;
        if (intent.bpm <= 0.0 && intent.bpmDelta == 0.0)
            setBpm (st.bpm);
        done.add ("style -> " + juce::String (intent.genre));
    }
    if (intent.bpm > 0.0)
    {
        setBpm (intent.bpm);
        done.add ("tempo -> " + juce::String ((int) projectParams.bpm) + " BPM");
    }
    if (intent.bpmDelta != 0.0)
    {
        setBpm (projectParams.bpm + intent.bpmDelta);
        done.add ("tempo -> " + juce::String ((int) projectParams.bpm) + " BPM");
    }
    if (! intent.root.empty())  projectParams.root  = intent.root;
    if (! intent.scale.empty()) projectParams.scale = intent.scale;
    if (! intent.root.empty() || (! intent.scale.empty() && intent.genre.empty()))
        done.add ("key -> " + juce::String (formatKeyString (projectParams)));
    if (intent.bars > 0)
    {
        projectParams.bars = intent.bars;
        done.add (juce::String (intent.bars) + " bars");
    }

    auto nudge = [&done] (float& v, float delta, const char* name)
    {
        if (delta == 0.0f) return;
        v = juce::jlimit (0.0f, 1.0f, v + delta);
        done.add (juce::String (name) + (delta > 0.0f ? " up" : " down"));
    };
    nudge (projectParams.energy,      intent.energyDelta,   "energy");
    nudge (projectParams.noteDensity, intent.densityDelta,  "density");
    nudge (projectParams.swing,       intent.swingDelta,    "swing");
    nudge (projectParams.humanize,    intent.humanizeDelta, "humanize");

    // ---- Then the generation action ----
    auto freshSeed = [this]
    {
        projectParams.seed =
            1u + (unsigned int) juce::Random::getSystemRandom().nextInt (0x7fffffff);
    };

    switch (intent.action)
    {
        case CA::NewIdea:
        case CA::GenerateAll:
            freshSeed();
            generateAllParts (false); // snapshot already taken above
            out.generatedMidi = true;
            done.add (intent.action == CA::NewIdea ? "rolled a fresh idea"
                                                   : "regenerated all unlocked lanes");
            break;

        case CA::Generate:
        case CA::Vary:
        {
            freshSeed(); // snapshot already taken above
            juce::StringArray names;
            for (auto lane : intent.lanes)
            {
                if (lane == InstrumentType::Drums)
                {
                    if (intent.pieces.empty())
                    {
                        generateDrumKit (false);
                        names.add ("drums");
                    }
                    else
                    {
                        for (auto piece : intent.pieces)
                        {
                            if (drumKit[(size_t) piece].locked)
                            {
                                names.add (juce::String (toString (piece)).toLowerCase()
                                           + " (locked, skipped)");
                                continue;
                            }
                            generateDrumPiece (piece, false);
                            names.add (juce::String (toString (piece)).toLowerCase());
                        }
                    }
                }
                else if (parts[(size_t) lane].locked)
                {
                    names.add (juce::String (toString (lane)).toLowerCase()
                               + " (locked, skipped)");
                }
                else
                {
                    generatePart (lane, false);
                    names.add (juce::String (toString (lane)).toLowerCase());
                }
            }
            out.generatedMidi = true;
            done.add ((intent.action == CA::Vary ? "varied " : "regenerated ")
                      + names.joinIntoString (" + "));
            // If every target was locked, nothing above rebuilt the preview —
            // but tempo/key/dial changes still applied. Keep playback in sync.
            rebuildPreviewSequences();
            break;
        }

        case CA::AdjustOnly:
            if (intent.paramChangeWantsRegen())
            {
                generateAllParts (false); // snapshot already taken above
                out.generatedMidi = true;
                done.add ("regenerated unlocked lanes to match");
            }
            else
            {
                rebuildPreviewSequences(); // BPM-only: playback follows live
            }
            break;

        default:
            break;
    }

    // (The editor shows the critic summary separately after generation.)
    out.assistantText = juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa1 "))
                      + done.joinIntoString (", ") + ".";
    return true;
}

juce::String AIMidiGenProcessor::buildProjectContextBrief() const
{
    juce::String s;
    s << "Style: "  << juce::String (projectParams.genre)
      << " | Key: " << juce::String (formatKeyString (projectParams))
      << " | "      << juce::String ((int) projectParams.bpm) << " BPM"
      << " | "      << projectParams.bars << " bars\n";
    s << "Feel dials (0..1): energy " << juce::String (projectParams.energy, 2)
      << ", density "  << juce::String (projectParams.noteDensity, 2)
      << ", swing "    << juce::String (projectParams.swing, 2)
      << ", humanize " << juce::String (projectParams.humanize, 2) << "\n";

    s << "Lanes:";
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums)
            continue;
        const auto& p = parts[(size_t) t];
        s << " " << toString ((InstrumentType) t) << "="
          << (p.notes.empty() ? juce::String ("empty")
                              : juce::String ((int) p.notes.size()) + " notes");
        if (p.locked) s << " [locked]";
        if (p.muted)  s << " [muted]";
        s << ";";
    }

    s << "\nDrum kit:";
    bool anyDrums = false;
    for (int i = 0; i < (int) DrumPiece::NumPieces; ++i)
    {
        const auto& p = drumKit[(size_t) i];
        if (p.notes.empty())
            continue;
        anyDrums = true;
        s << " " << toString ((DrumPiece) i) << "=" << (int) p.notes.size() << " hits"
          << (p.locked ? " [locked]" : "") << ";";
    }
    if (! anyDrums)
        s << " empty";

    if (hasDna())
        s << "\nMIDI DNA: groove/harmony learned from a user MIDI file is active.";
    if (criticSummary.isNotEmpty())
        s << "\nLast critic pass: " << criticSummary;
    return s;
}

//==============================================================================
void AIMidiGenProcessor::generateDrumKit (bool recordUndo)
{
    if (recordUndo) pushUndoSnapshot();
    auto fresh = generator.generateDrumKit (projectParams);
    for (size_t i = 0; i < drumKit.size(); ++i)
        if (! drumKit[i].locked)
            drumKit[i] = fresh[i];

    runCritic();
    rebuildDrumMasterPart();
    rebuildPreviewSequences();
}

void AIMidiGenProcessor::generateDrumPiece (DrumPiece dp, bool recordUndo)
{
    auto& piece = drumKit[(size_t) dp];
    if (piece.locked) return;
    if (recordUndo) pushUndoSnapshot();
    piece = generator.generateDrumPiece (dp, projectParams);

    runCritic();
    rebuildDrumMasterPart();
    rebuildPreviewSequences();
}

void AIMidiGenProcessor::runCritic()
{
    // Arrangement-level review + repair (chord-tone strong beats, bass
    // register, kick/bass clashes). Locked lanes are skipped inside the
    // critic itself. Runs after EVERY generation path — manual, style
    // switch, drum-piece reroll, or AI chat — so nothing unchecked lands
    // in the roll.
    auto rep = reviewArrangement (parts, drumKit, projectParams);
    criticSummary = juce::String (rep.summary());
}

//==============================================================================
juce::String AIMidiGenProcessor::loadDna (const juce::File& midiFile)
{
    dna = MidiDna::analyzeFile (midiFile);
    generator.setDna (dna.valid ? &dna : nullptr);

    if (! dna.valid)
        return dna.describe();

    // Keep the abstract analysis in the local Brain so future Claude requests
    // can retrieve it. The original MIDI and its note sequence are not stored.
    const auto stableId = "reference-midi-" + midiFile.getFileNameWithoutExtension();
    aiClient.knowledge().upsertText (stableId,
                                     "Reference MIDI — " + midiFile.getFileNameWithoutExtension(),
                                     dna.styleProfile());

    pushUndoSnapshot(); // one undo step covers the whole DNA regenerate

    // Harmony learned from the pack becomes the project key (the drums
    // side is picked up automatically by the generator via setDna).
    if (dna.hasHarmony)
    {
        static const char* pcNames[] = { "C","C#","D","D#","E","F",
                                         "F#","G","G#","A","A#","B" };
        projectParams.root  = pcNames[dna.rootPc % 12];
        projectParams.scale = dna.scale;
    }

    // Regenerate everything that isn't locked so the DNA takes effect now.
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        if ((InstrumentType) t == InstrumentType::Drums) continue;
        if (! parts[(size_t) t].locked)
            parts[(size_t) t] = generator.generate ((InstrumentType) t, projectParams);
    }
    generateDrumKit (false); // runs the critic + rebuilds previews internally

    return dna.describe();
}

void AIMidiGenProcessor::rebuildDrumMasterPart()
{
    const juce::ScopedLock sl (processLock);
    auto& master = parts[(size_t) InstrumentType::Drums];
    master.type = InstrumentType::Drums;
    master.notes.clear();

    for (auto& piece : drumKit)
        if (! piece.muted)
            for (auto& n : piece.notes)
                master.notes.push_back (n);

    std::sort (master.notes.begin(), master.notes.end(),
               [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeats < b.startBeats; });
}

//==============================================================================
void AIMidiGenProcessor::rebuildPreviewSequences()
{
    // MESSAGE THREAD. Refresh the audio-thread mirrors first.
    liveBpm.store (projectParams.bpm);
    liveBars.store (projectParams.bars);
    const juce::ScopedLock sl (processLock);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        previewSeqs[(size_t) t] = MidiGenerator::toSequence (parts[(size_t) t],
                                                             projectParams.bpm);
    previewIdx.fill (0);

    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        drumPreviewSeqs[(size_t) dp] = MidiGenerator::toSequence (drumKit[(size_t) dp],
                                                                   projectParams.bpm);
    drumPreviewIdx.fill (0);

    previewPpqPos.store (0.0);
}

void AIMidiGenProcessor::setBpm (double bpm)
{
    projectParams.bpm = juce::jlimit (40.0, 240.0, bpm);
    liveBpm.store (projectParams.bpm); // audio-thread mirror follows immediately
}

void AIMidiGenProcessor::setHostTempoSync (bool shouldSync)
{
    hostTempoSync.store (shouldSync);
}

void AIMidiGenProcessor::setHostMidiOut (bool shouldEmit)
{
    hostMidiOut.store (shouldEmit);
}

void AIMidiGenProcessor::setPreviewAudition (bool shouldAudition)
{
    previewAudition.store (shouldAudition);
}

bool AIMidiGenProcessor::getHostTransport (bool& isPlaying, double& ppqPosition, double& bpm) const
{
    isPlaying = false;
    ppqPosition = 0.0;
    bpm = projectParams.bpm;

    auto* ph = getPlayHead();
    if (ph == nullptr)
        return false;

    if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm())
            bpm = *b;
        if (auto ppq = pos->getPpqPosition())
            ppqPosition = *ppq;
        isPlaying = pos->getIsPlaying();
        return true;
    }
    return false;
}

void AIMidiGenProcessor::pullHostTempoIfNeeded()
{
    // AUDIO THREAD. Never writes projectParams — updates the atomic mirror and
    // asks the message thread to adopt it (handleAsyncUpdate).
    if (! hostTempoSync.load())
        return;

    bool playing = false;
    double ppq = 0.0, bpm = liveBpm.load();
    if (getHostTransport (playing, ppq, bpm) && bpm > 0.0)
    {
        const double clamped = juce::jlimit (40.0, 240.0, bpm);
        if (std::abs (clamped - liveBpm.load()) > 0.01)
        {
            liveBpm.store (clamped);
            triggerAsyncUpdate(); // message thread copies into projectParams
        }
    }
}

void AIMidiGenProcessor::handleAsyncUpdate()
{
    // MESSAGE THREAD. Adopt host tempo + service audio-thread rebuild requests.
    if (hostTempoSync.load())
        projectParams.bpm = liveBpm.load();
    if (seqRebuildRequested.exchange (false))
        rebuildPreviewSequences();
}

void AIMidiGenProcessor::togglePreview (bool shouldPlay)
{
    previewing.store (shouldPlay);
    if (shouldPlay)
    {
        syncSamplesToSynth();
        rebuildPreviewSequences();
    }
    else
        previewSynth.allNotesOff();
}

void AIMidiGenProcessor::collectMidiForPpqWindow (juce::MidiBuffer& dest, int numSamples,
                                                  double startPpqInLoop, double ppqPerSample)
{
    // AUDIO THREAD. Try-lock: if the message thread is mid-rebuild, skip this
    // block instead of blocking the audio callback (priority inversion).
    const juce::ScopedTryLock sl (processLock);
    if (! sl.isLocked())
        return;
    const double ticksPerQ = MidiGenerator::ticksPerQuarter;
    const double loopLenQ  = liveBars.load() * 4.0;

    for (int s = 0; s < numSamples; ++s)
    {
        const double ppq  = startPpqInLoop + s * ppqPerSample;
        const double tick = std::fmod (ppq, loopLenQ) * ticksPerQ;

        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        {
            if ((InstrumentType) t == InstrumentType::Drums) continue;
            if (parts[(size_t) t].muted) continue;
            auto& seq = previewSeqs[(size_t) t];
            auto& idx = previewIdx[(size_t) t];

            if (tick < (idx > 0 ? seq.getEventTime (idx - 1) : 0.0))
                idx = 0;

            const float gain = partGains[(size_t) t];
            while (idx < seq.getNumEvents()
                   && seq.getEventTime (idx) <= tick)
            {
                auto msg = seq.getEventPointer (idx)->message;
                if (msg.isNoteOn())
                {
                    const int vel = juce::jlimit (1, 127,
                        (int) std::round ((float) msg.getVelocity() * gain));
                    msg = juce::MidiMessage::noteOn (msg.getChannel(),
                                                     msg.getNoteNumber(),
                                                     (juce::uint8) vel);
                }
                dest.addEvent (msg, s);
                ++idx;
            }
        }

        for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        {
            if (drumKit[(size_t) dp].muted) continue;
            auto& seq = drumPreviewSeqs[(size_t) dp];
            auto& idx = drumPreviewIdx[(size_t) dp];

            if (tick < (idx > 0 ? seq.getEventTime (idx - 1) : 0.0))
                idx = 0;

            const float gain = drumGains[(size_t) dp];
            while (idx < seq.getNumEvents()
                   && seq.getEventTime (idx) <= tick)
            {
                auto msg = seq.getEventPointer (idx)->message;
                if (msg.isNoteOn())
                {
                    const int vel = juce::jlimit (1, 127,
                        (int) std::round ((float) msg.getVelocity() * gain));
                    msg = juce::MidiMessage::noteOn (msg.getChannel(),
                                                     msg.getNoteNumber(),
                                                     (juce::uint8) vel);
                }
                dest.addEvent (msg, s);
                ++idx;
            }
        }
    }
}

void AIMidiGenProcessor::collectPreviewMidi (juce::MidiBuffer& dest, int numSamples)
{
    // AUDIO THREAD — read only the atomic mirrors, never projectParams.
    const double bpm          = liveBpm.load();
    const double ppqPerSample = bpm / (60.0 * sampleRateHz);
    const double loopLenQ     = liveBars.load() * 4.0;
    double pos = previewPpqPos.load();

    collectMidiForPpqWindow (dest, numSamples, pos, ppqPerSample);
    previewPpqPos.store (std::fmod (pos + numSamples * ppqPerSample, loopLenQ));
}

//==============================================================================
void AIMidiGenProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear();

    pullHostTempoIfNeeded();

    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = projectParams.bpm;
    const bool haveHost = getHostTransport (hostPlaying, hostPpq, hostBpm);

    const bool internalPreview = previewing.load();
    const bool emitHostMidi = hostMidiOut.load() && haveHost && hostPlaying;

    if (! internalPreview && ! emitHostMidi)
        return;

    // Keep sequences warm for host MIDI out without requiring Preview first.
    // NEVER rebuild on the audio thread — request it from the message thread.
    if (emitHostMidi)
    {
        bool anySeq = false;
        {
            const juce::ScopedTryLock stl (processLock);
            if (! stl.isLocked())
                return; // message thread is rebuilding right now — skip block
            for (auto& s : previewSeqs)     if (s.getNumEvents() > 0) { anySeq = true; break; }
            if (! anySeq)
                for (auto& s : drumPreviewSeqs) if (s.getNumEvents() > 0) { anySeq = true; break; }
        }
        if (! anySeq)
        {
            seqRebuildRequested.store (true);
            triggerAsyncUpdate();
            return; // sequences will be ready within a block or two
        }
    }

    juce::MidiBuffer previewMidi;
    const double loopLenQ = liveBars.load() * 4.0;

    if (emitHostMidi)
    {
        const double startInLoop = std::fmod (juce::jmax (0.0, hostPpq), loopLenQ);
        const double prev = previewPpqPos.load();
        // Loop wrap or host seek — resync event cursors
        if (startInLoop + 0.05 < prev || std::abs (startInLoop - prev) > 1.0)
        {
            previewIdx.fill (0);
            drumPreviewIdx.fill (0);
        }
        previewPpqPos.store (startInLoop);
        collectPreviewMidi (previewMidi, buffer.getNumSamples());
    }
    else
    {
        collectPreviewMidi (previewMidi, buffer.getNumSamples());
    }

    if (internalPreview && previewAudition.load())
        previewSynth.render (buffer, previewMidi);

    midi.addEvents (previewMidi, 0, buffer.getNumSamples(), 0);
}

//==============================================================================
juce::AudioProcessorEditor* AIMidiGenProcessor::createEditor()
{
    return new AIMidiGenEditor (*this);
}

//==============================================================================
namespace
{
// Notes serialize as a flat number array [start,len,pitch,vel, ...] — compact
// and version-stable.
juce::var notesToVar (const std::vector<NoteEvent>& notes)
{
    juce::Array<juce::var> a;
    a.ensureStorageAllocated ((int) notes.size() * 4);
    for (const auto& n : notes)
    {
        a.add (n.startBeats);
        a.add (n.lengthBeats);
        a.add (n.pitch);
        a.add ((double) n.velocity);
    }
    return a;
}

void varToNotes (const juce::var& v, std::vector<NoteEvent>& out)
{
    out.clear();
    if (auto* a = v.getArray())
    {
        out.reserve ((size_t) (a->size() / 4));
        for (int i = 0; i + 3 < a->size(); i += 4)
        {
            NoteEvent n;
            n.startBeats  = (double) a->getUnchecked (i);
            n.lengthBeats = (double) a->getUnchecked (i + 1);
            n.pitch       = (int)    a->getUnchecked (i + 2);
            n.velocity    = (float) (double) a->getUnchecked (i + 3);
            out.push_back (n);
        }
    }
}
} // namespace

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
    root->setProperty ("swing", p.swing);
    root->setProperty ("humanize", p.humanize);
    root->setProperty ("noteDensity", p.noteDensity);
    root->setProperty ("rhythmComplexity", p.rhythmComplexity);
    root->setProperty ("chordComplexity", p.chordComplexity);
    root->setProperty ("seed", (juce::int64) p.seed);
    root->setProperty ("hostTempoSync", hostTempoSync.load());
    root->setProperty ("hostMidiOut", hostMidiOut.load());
    root->setProperty ("previewAudition", previewAudition.load());
    root->setProperty ("criticSummary", criticSummary);
    root->setProperty ("genreMode", (int) genreMode);

    // ---- The actual music: every lane's notes + lock/mute flags ----
    juce::Array<juce::var> lanes;
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
    {
        const auto& part = parts[(size_t) t];
        auto* o = new juce::DynamicObject();
        o->setProperty ("locked", part.locked);
        o->setProperty ("muted",  part.muted);
        o->setProperty ("notes",  notesToVar (part.notes));
        lanes.add (juce::var (o));
    }
    root->setProperty ("lanes", lanes);

    juce::Array<juce::var> kit;
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
    {
        const auto& piece = drumKit[(size_t) dp];
        auto* o = new juce::DynamicObject();
        o->setProperty ("locked", piece.locked);
        o->setProperty ("muted",  piece.muted);
        o->setProperty ("notes",  notesToVar (piece.notes));
        kit.add (juce::var (o));
    }
    root->setProperty ("drumKitParts", kit);

    juce::Array<juce::var> timbres;
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        timbres.add ((int) partTimbres[(size_t) t]);
    root->setProperty ("partTimbres", timbres);

    juce::Array<juce::var> pGains, dGains;
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        pGains.add (partGains[(size_t) t]);
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        dGains.add (drumGains[(size_t) dp]);
    root->setProperty ("partGains", pGains);
    root->setProperty ("drumGains", dGains);

    juce::Array<juce::var> dSamples, pSamples;
    for (int dp = 0; dp < (int) DrumPiece::NumPieces; ++dp)
        dSamples.add (drumSampleIds[(size_t) dp]);
    for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
        pSamples.add (partSampleIds[(size_t) t]);
    root->setProperty ("drumSampleIds", dSamples);
    root->setProperty ("partSampleIds", pSamples);

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
    p.swing            = (float) v.getProperty ("swing", p.swing);
    p.humanize         = (float) v.getProperty ("humanize", p.humanize);
    p.noteDensity      = (float) v.getProperty ("noteDensity", p.noteDensity);
    p.rhythmComplexity = (float) v.getProperty ("rhythmComplexity", p.rhythmComplexity);
    p.chordComplexity  = (float) v.getProperty ("chordComplexity", p.chordComplexity);
    p.seed = (unsigned int) (juce::int64) v.getProperty ("seed", (juce::int64) p.seed);
    hostTempoSync.store ((bool) v.getProperty ("hostTempoSync", hostTempoSync.load()));
    hostMidiOut.store ((bool) v.getProperty ("hostMidiOut", hostMidiOut.load()));
    previewAudition.store ((bool) v.getProperty ("previewAudition", previewAudition.load()));
    criticSummary = v.getProperty ("criticSummary", criticSummary).toString();

    if (v.hasProperty ("genreMode"))
        genreMode = genreModeFromIndex ((int) v.getProperty ("genreMode", 0));
    else
    {
        GenreMode detected;
        if (detectGenreFromText (juce::String (p.genre), detected))
            genreMode = detected;
    }

    if (auto* arr = v.getProperty ("partTimbres", {}).getArray())
    {
        for (int t = 0; t < (int) InstrumentType::NumTypes && t < arr->size(); ++t)
            partTimbres[(size_t) t] = partTimbreFromIndex ((int) arr->getUnchecked (t));
    }
    else
    {
        auto map = defaultsFor (genreMode);
        for (int t = 0; t < (int) InstrumentType::NumTypes; ++t)
            partTimbres[(size_t) t] = map.defaults[(size_t) t];
    }

    if (auto* arr = v.getProperty ("partGains", {}).getArray())
    {
        for (int t = 0; t < (int) InstrumentType::NumTypes && t < arr->size(); ++t)
            partGains[(size_t) t] = juce::jlimit (0.0f, 1.25f, (float) arr->getUnchecked (t));
    }
    else
    {
        applyMixDefaults();
    }

    if (auto* arr = v.getProperty ("drumGains", {}).getArray())
    {
        for (int dp = 0; dp < (int) DrumPiece::NumPieces && dp < arr->size(); ++dp)
            drumGains[(size_t) dp] = juce::jlimit (0.0f, 1.25f, (float) arr->getUnchecked (dp));
    }

    if (auto* arr = v.getProperty ("drumSampleIds", {}).getArray())
    {
        for (int dp = 0; dp < (int) DrumPiece::NumPieces && dp < arr->size(); ++dp)
            drumSampleIds[(size_t) dp] = arr->getUnchecked (dp).toString();
    }
    if (auto* arr = v.getProperty ("partSampleIds", {}).getArray())
    {
        for (int t = 0; t < (int) InstrumentType::NumTypes && t < arr->size(); ++t)
            partSampleIds[(size_t) t] = arr->getUnchecked (t).toString();
    }

    // ---- Restore the actual music (notes + lock/mute per lane/piece) ----
    if (auto* arr = v.getProperty ("lanes", {}).getArray())
    {
        for (int t = 0; t < (int) InstrumentType::NumTypes && t < arr->size(); ++t)
        {
            auto& part = parts[(size_t) t];
            const auto o = arr->getUnchecked (t);
            part.locked = (bool) o.getProperty ("locked", false);
            part.muted  = (bool) o.getProperty ("muted", false);
            varToNotes (o.getProperty ("notes", {}), part.notes);
        }
    }
    if (auto* arr = v.getProperty ("drumKitParts", {}).getArray())
    {
        for (int dp = 0; dp < (int) DrumPiece::NumPieces && dp < arr->size(); ++dp)
        {
            auto& piece = drumKit[(size_t) dp];
            const auto o = arr->getUnchecked (dp);
            piece.locked = (bool) o.getProperty ("locked", false);
            piece.muted  = (bool) o.getProperty ("muted", false);
            varToNotes (o.getProperty ("notes", {}), piece.notes);
        }
    }

    syncPreviewSounds();
    syncSamplesToSynth();
    rebuildDrumMasterPart();
    rebuildPreviewSequences();
}

} // namespace aimidi

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aimidi::AIMidiGenProcessor();
}
