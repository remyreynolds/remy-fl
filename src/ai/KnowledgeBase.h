#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace aimidi
{
/** Local music-theory / style documents Claude can reference.
    Stored under ~/Library/Application Support/AIMidiGen/knowledge/ — no backend. */
class KnowledgeBase
{
public:
    struct Document
    {
        juce::String id;      // filename stem
        juce::String title;
        juce::String body;
    };

    KnowledgeBase();

    void reload();

    /** Import a .txt / .md / .text file into the knowledge folder. */
    bool importFile (const juce::File& file, juce::String* errorOut = nullptr);

    /** Import pasted/raw text as a named document. */
    bool importText (const juce::String& title, const juce::String& body,
                     juce::String* errorOut = nullptr);

    /** Write/replace a document with a stable id (for kit memory, etc.). */
    bool upsertText (const juce::String& id, const juce::String& title,
                     const juce::String& body, juce::String* errorOut = nullptr);

    bool removeDocument (const juce::String& id);
    void clearAll();

    const std::vector<Document>& documents() const { return docs; }
    int size() const { return (int) docs.size(); }
    juce::String statusLine() const;

    struct RetrievalResult
    {
        juce::String context;           // text to inject into Claude
        juce::StringArray matchedDocs;  // doc titles that contributed
    };

    /** Pick the most relevant chunks for a style/theory query.
        forGeneration adds "how to sound good / groove / loop" bias keywords —
        only wanted on the MIDI-generation path, not plain conversation. */
    RetrievalResult retrieveForQuery (const juce::String& query,
                                      int maxChars = 14000,
                                      bool forGeneration = false) const;

    /** Convenience: context string only. */
    juce::String buildContextForQuery (const juce::String& query,
                                       int maxChars = 14000) const
    {
        return retrieveForQuery (query, maxChars).context;
    }

    /** Full master-system-prompt.md body from the knowledge corpus (or empty). */
    juce::String masterPromptText() const;

    juce::File folder() const;

private:
    struct Chunk
    {
        juce::String docTitle;
        juce::String text;
    };

    std::vector<Document> docs;

    void ensureFolder() const;
    void writeStarterDocIfEmpty();
    static juce::String sanitizeFileName (const juce::String& title);
    static std::vector<Chunk> chunkDocument (const Document& doc);
    static int scoreChunk (const Chunk& chunk, const juce::StringArray& keywords);
};

} // namespace aimidi
