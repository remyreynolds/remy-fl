#include "KnowledgeBase.h"
#include <BinaryData.h>
#include <algorithm>

namespace aimidi
{

KnowledgeBase::KnowledgeBase()
{
    ensureFolder();
    writeStarterDocIfEmpty();
    reload();
}

juce::File KnowledgeBase::folder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("AIMidiGen")
        .getChildFile ("knowledge");
}

void KnowledgeBase::ensureFolder() const
{
    folder().createDirectory();
}

void KnowledgeBase::writeStarterDocIfEmpty()
{
    ensureFolder();

    auto ensureDoc = [this] (const juce::String& fileName, const juce::String& body, bool force = false)
    {
        const auto f = folder().getChildFile (fileName);
        if (force || ! f.existsAsFile())
            f.replaceWithText (body);
    };

    // Prefer the shipped Groovewright master prompt from the repo when present.
    {
        const auto dest = folder().getChildFile ("master-system-prompt.md");
        int bundledMasterSize = 0;
        const auto* bundledMasterData = BinaryData::getNamedResource (
            "master_system_prompt_md", bundledMasterSize);
        const juce::String bundledMaster = bundledMasterData != nullptr
            ? juce::String (bundledMasterData, (size_t) juce::jmax (0, bundledMasterSize))
            : juce::String();
        if (bundledMaster.trim().isNotEmpty())
            dest.replaceWithText (bundledMaster);

        juce::File shipped;
        // Standalone / dev: look next to cwd and common relative roots
        const juce::File candidates[] = {
            juce::File::getCurrentWorkingDirectory().getChildFile ("brain/2-prompts/master-system-prompt.md"),
            juce::File::getCurrentWorkingDirectory().getChildFile ("brain/master-system-prompt.md"),
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getParentDirectory().getParentDirectory()
                .getParentDirectory().getChildFile ("brain/2-prompts/master-system-prompt.md"),
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getParentDirectory().getParentDirectory()
                .getParentDirectory().getChildFile ("brain/master-system-prompt.md"),
        };
        for (auto& c : candidates)
            if (c.existsAsFile()) { shipped = c; break; }

        if (! dest.existsAsFile() && shipped.existsAsFile())
            shipped.copyFileTo (dest);
        else if (! dest.existsAsFile())
            ensureDoc ("master-system-prompt.md",
R"(# MASTER SYSTEM PROMPT — Groovewright

You are Groovewright, a house MIDI agent. Groove over theory. Loop-first. Leave space.
Obey genre profiles (deep/tech/French/prog/afro/garage/piano house).
Generate authentic house MIDI; never clone copyrighted melodies.
When generating, return ONLY the plugin MIDI JSON schema (pitch names + startBeat + durationBeats).
)", true);
    }

    // Always ensure the mix guide exists (preview volumes follow this knowledge).
    ensureDoc ("preview-mix-guide.md",
R"(# Preview mix guide (brain)

Use these relative levels when writing MIDI velocities and when balancing preview.

## House / tech house
- Closed hats: LOUD and bright (often the busiest element) — velocity 110–127.
- Kick: punchy and forward — 105–120.
- Clap: strong on 2 and 4 — 100–118.
- Snare: slightly under the clap if both play — 85–100.
- Bass: solid, a bit under kick — 95–115.
- Chords / stabs: tucked under the groove — 70–95.
- Pads: quiet bed — 55–75.
- Lead / melody: sit above chords but under hats — 85–105.

## Hip-hop / rap
- 808 / bass: very forward — 110–127.
- Kick + snare: dominant — 105–125.
- Hats: present but not house-busy — 85–105.
- Chords / samples: mid level — 70–95.

## Pop
- Melody / lead: loudest pitched part — 100–120.
- Drums balanced; hats clear — 90–110.
- Chords support the lead — 75–95.

## Techno
- Kick is king — 115–127.
- Closed hats very loud and ticking — 105–127.
- Bass heavy — 100–120.
- Chords sparse and quieter — 60–85.

## Classical
- Melody and harmony lead; percussion soft if present.
- Melody 95–115, chords 80–100, drums soft 40–70.
)");

    ensureDoc ("drum-loop-theory.md",
R"(# Drum loop theory (brain)

## Roles in a house / deep house kit
- Kick: pulse of the track — usually 4-on-the-floor.
- Clap: body on 2 and 4; can layer with snare.
- Snare: backbeat or ghost notes; often quieter than clap in house.
- Closed hat: groove glue — 16th patterns, often loudest metallic layer.
- Open hat: offbeat accents (the "&" of each beat).

## Generating a loop
- 4 or 8 bars, instantly loopable.
- Start with kick + clap, then hats, then spice (open hat / perc).
- Humanize velocity slightly; keep kick solid on the grid.
- Do not place melodic pitched lines on drum MIDI channels.
- Sample packs feed Drums preview only — melody/bass/chords use theory + synth.
)");

    ensureDoc ("core-music-theory.md",
R"(# Core music theory (brain)

## Harmony that sounds good
- Minor: i–VI–III–VII, i–iv–VII–VI, i–VI–VII, i–VII–VI–VII.
- Major: I–V–vi–IV, I–vi–IV–V, ii–V–I.
- Change chords every 1–2 bars (house) or every bar (pop). Avoid random jumps.
- Keep chord tones on downbeats; add 7ths/9ths for color, not every note.

## Voice leading
- Move chord voices by small intervals between changes.
- Bass lands on chord roots (or fifth) on beat 1 of each change.
- Leave space — silence is part of the groove.

## Rhythm
- House 4/4: kick on quarters; clap/snare on 2 and 4; hats on 16ths/offbeats.
- Syncopated bass on offbeats works; lock with kick on beat 1.
- Fill the requested bars so the loop feels complete when it repeats.

## Plugin MIDI rules
- Chords = multiple notes sharing the SAME startBeat + durationBeats.
- Sample packs are for Drums preview only — never pitch drum one-shots as leads.
- Follow preview-mix-guide.md for velocities.
)");

    ensureDoc ("how-to-sound-good.md",
R"(# How to sound good (brain) — READ THIS FOR EVERY GENERATE

Goal: MIDI that grooves, sits in key, and loops cleanly.

## Non-negotiables
1. Stay in the project KEY — every pitch must fit the scale.
2. Match the requested BPM / bars / instrument.
3. Patterns must LOOP — last bar should connect naturally back to bar 1.
4. Prefer simple strong ideas over busy random notes.
5. Leave space: not every 16th needs a note.

## Arrangement roles
- Bass: mostly roots + fifths, low register, rhythmic. Rarely double the melody.
- Chords: clear voicings (3–4 notes), mid register, steady rhythm or short stabs.
- Melody / lead: singable contour, motifs that repeat/vary, rests between phrases.
- Counter melody: answers the lead; thinner and quieter; avoid unison with lead.
- Arp: patterned arpeggio of current chord tones; consistent note length.
- Pad: long held chord tones; few changes; low velocity bed.
- Drums: kick/clap foundation first; hats for groove; open hats as accents only.

## What usually sounds bad (avoid)
- Random chromatic notes outside the key.
- Everything playing full density all 4 bars with no rests.
- Bass and melody fighting in the same octave with identical rhythms.
- Chords that jump by huge leaps in every voice at once.
- Melodies that never repeat a motif (no memory for the ear).

## Quick recipe (house / tech house)
1. Chords: dark minor progression, 1–2 bar changes, mid register.
2. Bass: root on beat 1, syncopated 8ths/16ths between.
3. Hats: offbeat or 16th closed hats; open hat on offbeats sparingly.
4. Lead: short motif (2 bars), repeat with variation.
)");

    ensureDoc ("genre-tech-house.md",
R"(# Tech house / deep house (brain)

Keywords: tech house, deep house, house, club, dark, groovy

## Feel
- 122–128 BPM, 4/4, swing lightly if any.
- Dark minor / phrygian colors; sparse but driving.
- Groove first — hats and bass make the track.

## Parts
- Kick: 4-on-the-floor.
- Clap: 2 and 4, strong.
- Closed hats: offbeat or busy 16ths; loud.
- Bass: dry, root-heavy, offbeat bounce; stay under kick.
- Chords: short stabs (0.25–1 beat) or long pads under bass — not both dense.
- Lead: optional; short motifs, leave space for groove.

## Progressions
- i–VI–III–VII, i–VII–VI–VII, i–iv–VII–VI in minor.
)");

    ensureDoc ("genre-hip-hop.md",
R"(# Hip-hop / trap (brain)

Keywords: hip hop, hip-hop, trap, rap, 808, boom bap

## Feel
- 70–100 BPM (trap often half-time feel around 140–160 with sparse kicks).
- Strong pocket; leave space for vocals.

## Parts
- 808 / bass: long notes, slides between roots, very forward.
- Kick + snare: dominant; snare on 3 (trap) or 2+4 (boom bap).
- Hats: rolls and 16ths, but not house-busy.
- Chords: sparse mid pads or sampled-style stabs.
- Melody: simple, memorable, often pentatonic.
)");

    ensureDoc ("genre-pop.md",
R"(# Pop / melodic (brain)

Keywords: pop, melodic, radio, catchy, uplift

## Feel
- 100–128 BPM; clear song-like phrases.
- Melody is the star.

## Parts
- Melody: loudest pitched part; motifs of 2–4 bars with repetition.
- Chords: I–V–vi–IV or vi–IV–I–V; change every 1–2 bars.
- Bass: roots on changes, simple rhythm supporting the chords.
- Drums: balanced, clap/snare clear, hats supportive not dominant.
)");

    // Sync curated + full guides from brain/1-knowledge/guides (shared with Python brain).
    {
        const juce::File guideRoots[] = {
            juce::File::getCurrentWorkingDirectory().getChildFile ("brain/1-knowledge/guides"),
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getParentDirectory().getParentDirectory()
                .getParentDirectory().getChildFile ("brain/1-knowledge/guides"),
        };
        for (auto& root : guideRoots)
        {
            if (! root.isDirectory())
                continue;
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.md"))
            {
                const auto src = entry.getFile();
                if (src.getFileName().startsWithIgnoreCase ("00-"))
                    continue;
                // Flatten into knowledge root so retrieveForQuery sees one corpus.
                src.copyFileTo (folder().getChildFile (src.getFileName()));
            }
            const auto pdfRoot = root.getChildFile ("pdfs");
            if (pdfRoot.isDirectory())
            {
                const auto pdfDest = folder().getChildFile ("pdfs");
                pdfDest.createDirectory();
                for (const auto& entry : juce::RangedDirectoryIterator (pdfRoot, false, "*.pdf"))
                    entry.getFile().copyFileTo (pdfDest.getChildFile (entry.getFile().getFileName()));
            }
            break;
        }
    }

    // Reload happens in constructor after this.
}

void KnowledgeBase::reload()
{
    docs.clear();
    ensureFolder();

    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false,
                                                            "*.txt;*.md;*.text;*.markdown"))
    {
        const auto f = entry.getFile();
        Document d;
        d.id = f.getFileNameWithoutExtension();
        d.title = d.id;
        d.body = f.loadFileAsString();
        if (d.body.isNotEmpty())
            docs.push_back (std::move (d));
    }

    std::sort (docs.begin(), docs.end(),
               [] (const Document& a, const Document& b) { return a.title < b.title; });
}

juce::String KnowledgeBase::sanitizeFileName (const juce::String& title)
{
    auto s = title.trim().toLowerCase();
    juce::String out;
    for (int i = 0; i < s.length(); ++i)
    {
        auto c = s[i];
        if (juce::CharacterFunctions::isLetterOrDigit (c)) out << c;
        else if (c == ' ' || c == '-' || c == '_') out << '-';
    }
    while (out.contains ("--")) out = out.replace ("--", "-");
    if (out.isEmpty()) out = "doc-" + juce::String (juce::Time::currentTimeMillis());
    return out;
}

bool KnowledgeBase::importFile (const juce::File& file, juce::String* errorOut)
{
    if (! file.existsAsFile())
    {
        if (errorOut) *errorOut = "File not found.";
        return false;
    }

    const auto ext = file.getFileExtension().toLowerCase();
    if (! (ext == ".txt" || ext == ".md" || ext == ".text" || ext == ".markdown"))
    {
        if (errorOut) *errorOut = "Use a .txt or .md file.";
        return false;
    }

    const auto body = file.loadFileAsString();
    if (body.trim().isEmpty())
    {
        if (errorOut) *errorOut = "File is empty.";
        return false;
    }

    return importText (file.getFileNameWithoutExtension(), body, errorOut);
}

bool KnowledgeBase::importText (const juce::String& title, const juce::String& body,
                                juce::String* errorOut)
{
    ensureFolder();
    auto id = sanitizeFileName (title);
    auto dest = folder().getChildFile (id + ".md");
    int n = 2;
    while (dest.existsAsFile())
    {
        dest = folder().getChildFile (id + "-" + juce::String (n++) + ".md");
    }

    if (! dest.replaceWithText (body))
    {
        if (errorOut) *errorOut = "Could not write knowledge file.";
        return false;
    }

    reload();
    return true;
}

bool KnowledgeBase::upsertText (const juce::String& id, const juce::String& title,
                                const juce::String& body, juce::String* errorOut)
{
    ensureFolder();
    const auto safeId = sanitizeFileName (id.isNotEmpty() ? id : title);
    auto dest = folder().getChildFile (safeId + ".md");
    juce::String content = body;
    if (! content.startsWithIgnoreCase ("#"))
        content = "# " + title + "\n\n" + body;

    if (! dest.replaceWithText (content))
    {
        if (errorOut) *errorOut = "Could not write knowledge file.";
        return false;
    }

    reload();
    return true;
}

bool KnowledgeBase::removeDocument (const juce::String& id)
{
    auto fMd = folder().getChildFile (id + ".md");
    auto fTxt = folder().getChildFile (id + ".txt");
    bool ok = false;
    if (fMd.existsAsFile()) ok = fMd.deleteFile() || ok;
    if (fTxt.existsAsFile()) ok = fTxt.deleteFile() || ok;
    reload();
    return ok;
}

void KnowledgeBase::clearAll()
{
    for (const auto& entry : juce::RangedDirectoryIterator (folder(), false, "*"))
        if (! entry.isDirectory())
            entry.getFile().deleteFile();
    reload();
}

juce::String KnowledgeBase::statusLine() const
{
    if (docs.empty()) return "No theory docs";
    if (docs.size() == 1) return "1 theory doc";
    return juce::String (docs.size()) + " theory docs";
}

std::vector<KnowledgeBase::Chunk> KnowledgeBase::chunkDocument (const Document& doc)
{
    std::vector<Chunk> chunks;
    auto paras = juce::StringArray::fromLines (doc.body);
    juce::String current;
    auto flush = [&] ()
    {
        auto t = current.trim();
        if (t.isNotEmpty())
            chunks.push_back ({ doc.title, t });
        current.clear();
    };

    for (auto& line : paras)
    {
        if (line.trim().isEmpty())
        {
            flush();
            continue;
        }
        if (current.length() + line.length() > 900)
            flush();
        current << line << "\n";
    }
    flush();

    if (chunks.empty() && doc.body.trim().isNotEmpty())
        chunks.push_back ({ doc.title, doc.body.trim().substring (0, 1200) });

    return chunks;
}

int KnowledgeBase::scoreChunk (const Chunk& chunk, const juce::StringArray& keywords)
{
    const auto title = chunk.docTitle.toLowerCase();
    const auto body  = chunk.text.toLowerCase();
    int score = 0;
    for (auto& kw : keywords)
    {
        if (kw.length() < 3) continue;
        if (title.contains (kw)) score += 8; // prefer matching style/doc titles
        if (body.contains (kw))  score += 2;
    }
    return score;
}

KnowledgeBase::RetrievalResult KnowledgeBase::retrieveForQuery (const juce::String& query,
                                                                int maxChars) const
{
    RetrievalResult result;
    if (docs.empty())
        return result;

    auto keywords = juce::StringArray::fromTokens (query.toLowerCase(), " \t\n,.;:!?()[]{}\"'", "");
    keywords.removeEmptyStrings();

    // Style aliases help match uploaded genre docs
    const auto q = query.toLowerCase();
    auto addAlias = [&] (const char* needle, const char* alias)
    {
        if (q.contains (needle)) keywords.addIfNotAlreadyThere (alias);
    };
    addAlias ("tech house", "tech");
    addAlias ("tech house", "house");
    addAlias ("deep house", "house");
    addAlias ("melodic", "melody");
    addAlias ("progression", "chord");
    addAlias ("harmony", "chord");
    addAlias ("hip hop", "hip");
    addAlias ("hip-hop", "hip");
    addAlias ("trap", "808");
    addAlias ("sound good", "good");
    addAlias ("groove", "house");
    addAlias ("bassline", "bass");
    addAlias ("drum", "kick");
    // Always bias toward the "how to sound good" guide when generating.
    keywords.addIfNotAlreadyThere ("good");
    keywords.addIfNotAlreadyThere ("sound");
    keywords.addIfNotAlreadyThere ("loop");
    keywords.addIfNotAlreadyThere ("groove");

    struct Scored { int score; Chunk chunk; };
    std::vector<Scored> scored;

    for (auto& doc : docs)
        for (auto& chunk : chunkDocument (doc))
        {
            const int s = scoreChunk (chunk, keywords);
            if (s > 0)
                scored.push_back ({ s, chunk });
        }

    // If nothing matched keywords, fall back to a light sample from every doc
    if (scored.empty())
    {
        for (auto& doc : docs)
        {
            auto chunks = chunkDocument (doc);
            if (! chunks.empty())
                scored.push_back ({ 1, chunks.front() });
        }
    }

    std::sort (scored.begin(), scored.end(),
               [] (const Scored& a, const Scored& b) { return a.score > b.score; });

    juce::String out;
    out << "===== MASTER SYSTEM PROMPT (always obey) =====\n";

    // Always inject the master prompt first when present.
    for (auto& doc : docs)
    {
        if (doc.id.containsIgnoreCase ("master-system") || doc.title.containsIgnoreCase ("master-system"))
        {
            out << doc.body.trim() << "\n===== END MASTER =====\n\n";
            result.matchedDocs.addIfNotAlreadyThere (doc.title);
            break;
        }
    }

    out << "Relevant music-theory / style documents for this request. "
           "Identify the style, prefer the best-matching docs, and follow their rules "
           "(harmony, rhythm, voicing, genre) when answering or composing.\n\n";

    int referenceDocsAdded = 0;
    juce::StringArray includedReferenceTitles;
    for (auto& s : scored)
    {
        if (s.chunk.docTitle.containsIgnoreCase ("master-system"))
            continue; // already injected
        const bool isReference = s.chunk.docTitle.containsIgnoreCase ("reference-midi-");
        if (isReference
            && ! includedReferenceTitles.contains (s.chunk.docTitle)
            && referenceDocsAdded >= 3)
            continue;
        juce::String block;
        block << "### " << s.chunk.docTitle << "\n" << s.chunk.text.trim() << "\n\n";
        if (out.length() + block.length() > maxChars)
            break;
        out << block;
        result.matchedDocs.addIfNotAlreadyThere (s.chunk.docTitle);
        if (isReference && ! includedReferenceTitles.contains (s.chunk.docTitle))
        {
            includedReferenceTitles.add (s.chunk.docTitle);
            ++referenceDocsAdded;
        }
    }

    result.context = out.trim();
    return result;
}

juce::String KnowledgeBase::masterPromptText() const
{
    for (const auto& doc : docs)
        if (doc.id.containsIgnoreCase ("master-system")
            || doc.title.containsIgnoreCase ("master-system")
            || doc.id.containsIgnoreCase ("master_system"))
            return doc.body;

    // Fall back to the binary-embedded resource so Claude still receives the
    // complete Brain even if the knowledge folder was wiped.
    int size = 0;
    const auto* data = BinaryData::getNamedResource ("master_system_prompt_md", size);
    if (data != nullptr && size > 0)
        return juce::String (data, (size_t) size);
    return {};
}

} // namespace aimidi
