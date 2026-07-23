#pragma once

#include <string>

namespace aimidi
{
/** How the last MIDI generation was produced — never blur AI with offline. */
enum class GenerationMode
{
    ClaudeBrain,  // Claude + bundled Brain prompt / knowledge
    OfflineLocal, // deterministic MidiGenerator + SongPlan
    FailedClaude  // Claude attempted; error shown; no silent local substitute
};

struct GenerationReport
{
    GenerationMode mode = GenerationMode::OfflineLocal;
    bool ok = false;
    std::string statusLine;   // user-facing one-liner
    std::string detail;       // exact API error or empty
    std::string fingerprint;  // chord / song-plan fingerprint when available
};

inline const char* generationModeLabel (GenerationMode m)
{
    switch (m)
    {
        case GenerationMode::ClaudeBrain:  return "Generated with Claude";
        case GenerationMode::OfflineLocal: return "Generated locally";
        case GenerationMode::FailedClaude: return "Claude failed";
    }
    return "Unknown";
}
} // namespace aimidi
