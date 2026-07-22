#include "PreviewSounds.h"

namespace aimidi
{

namespace
{
void setPart (GenreTimbreMap& m, InstrumentType t, PartTimbre def,
              std::initializer_list<PartTimbre> opts)
{
    m.defaults[(size_t) t] = def;
    m.variants[(size_t) t].assign (opts);
}
} // namespace

GenreTimbreMap defaultsFor (GenreMode genre)
{
    GenreTimbreMap m;

    switch (genre)
    {
        case GenreMode::House:
            m.drumKit = DrumKitStyle::House;
            setPart (m, InstrumentType::Chords, PartTimbre::SuperSaw,
                     { PartTimbre::SuperSaw, PartTimbre::HousePiano, PartTimbre::ChordSynth, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Melody, PartTimbre::SuperSaw,
                     { PartTimbre::SuperSaw, PartTimbre::BrightLead, PartTimbre::Pluck, PartTimbre::HousePiano });
            setPart (m, InstrumentType::Bass, PartTimbre::FilterBass,
                     { PartTimbre::FilterBass, PartTimbre::OrganBass, PartTimbre::HouseBass, PartTimbre::Sub808 });
            setPart (m, InstrumentType::Pad, PartTimbre::SuperSaw,
                     { PartTimbre::SuperSaw, PartTimbre::WarmPad, PartTimbre::Strings, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::Arp, PartTimbre::Pluck,
                     { PartTimbre::Pluck, PartTimbre::SuperSaw, PartTimbre::BrightLead, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::CounterMelody, PartTimbre::Pluck,
                     { PartTimbre::Pluck, PartTimbre::SuperSaw, PartTimbre::BrightLead, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Drums, PartTimbre::SoftPiano, { PartTimbre::SoftPiano });
            break;

        case GenreMode::HipHop:
            m.drumKit = DrumKitStyle::HipHop;
            setPart (m, InstrumentType::Chords, PartTimbre::SoftPiano,
                     { PartTimbre::SoftPiano, PartTimbre::ChordSynth, PartTimbre::HousePiano });
            setPart (m, InstrumentType::Melody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::Pluck, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Bass, PartTimbre::Sub808,
                     { PartTimbre::Sub808, PartTimbre::HouseBass, PartTimbre::AcousticBass });
            setPart (m, InstrumentType::Pad, PartTimbre::WarmPad,
                     { PartTimbre::WarmPad, PartTimbre::Strings });
            setPart (m, InstrumentType::Arp, PartTimbre::Pluck,
                     { PartTimbre::Pluck, PartTimbre::BrightLead });
            setPart (m, InstrumentType::CounterMelody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Drums, PartTimbre::SoftPiano, { PartTimbre::SoftPiano });
            break;

        case GenreMode::Pop:
            m.drumKit = DrumKitStyle::Pop;
            setPart (m, InstrumentType::Chords, PartTimbre::SoftPiano,
                     { PartTimbre::SoftPiano, PartTimbre::ChordSynth, PartTimbre::HousePiano });
            setPart (m, InstrumentType::Melody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::SoftPiano, PartTimbre::Pluck });
            setPart (m, InstrumentType::Bass, PartTimbre::HouseBass,
                     { PartTimbre::HouseBass, PartTimbre::AcousticBass, PartTimbre::Sub808 });
            setPart (m, InstrumentType::Pad, PartTimbre::WarmPad,
                     { PartTimbre::WarmPad, PartTimbre::Strings, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::Arp, PartTimbre::Pluck,
                     { PartTimbre::Pluck, PartTimbre::BrightLead });
            setPart (m, InstrumentType::CounterMelody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Drums, PartTimbre::SoftPiano, { PartTimbre::SoftPiano });
            break;

        case GenreMode::Classical:
            m.drumKit = DrumKitStyle::Classical;
            setPart (m, InstrumentType::Chords, PartTimbre::ClassicPiano,
                     { PartTimbre::ClassicPiano, PartTimbre::SoftPiano, PartTimbre::Strings });
            setPart (m, InstrumentType::Melody, PartTimbre::ClassicPiano,
                     { PartTimbre::ClassicPiano, PartTimbre::Strings, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Bass, PartTimbre::AcousticBass,
                     { PartTimbre::AcousticBass, PartTimbre::SoftPiano });
            setPart (m, InstrumentType::Pad, PartTimbre::Strings,
                     { PartTimbre::Strings, PartTimbre::WarmPad });
            setPart (m, InstrumentType::Arp, PartTimbre::ClassicPiano,
                     { PartTimbre::ClassicPiano, PartTimbre::Pluck, PartTimbre::Strings });
            setPart (m, InstrumentType::CounterMelody, PartTimbre::ClassicPiano,
                     { PartTimbre::ClassicPiano, PartTimbre::Strings });
            setPart (m, InstrumentType::Drums, PartTimbre::SoftPiano, { PartTimbre::SoftPiano });
            break;

        case GenreMode::Techno:
            m.drumKit = DrumKitStyle::Techno;
            setPart (m, InstrumentType::Chords, PartTimbre::SuperSaw,
                     { PartTimbre::SuperSaw, PartTimbre::ChordSynth, PartTimbre::WarmPad, PartTimbre::HousePiano });
            setPart (m, InstrumentType::Melody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::SuperSaw, PartTimbre::Pluck, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::Bass, PartTimbre::FilterBass,
                     { PartTimbre::FilterBass, PartTimbre::HouseBass, PartTimbre::Sub808, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::Pad, PartTimbre::WarmPad,
                     { PartTimbre::WarmPad, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::Arp, PartTimbre::Pluck,
                     { PartTimbre::Pluck, PartTimbre::BrightLead, PartTimbre::ChordSynth });
            setPart (m, InstrumentType::CounterMelody, PartTimbre::BrightLead,
                     { PartTimbre::BrightLead, PartTimbre::Pluck });
            setPart (m, InstrumentType::Drums, PartTimbre::SoftPiano, { PartTimbre::SoftPiano });
            break;

        default:
            break;
    }

    return m;
}

GenreMixMap mixDefaultsFor (GenreMode genre)
{
    // Production-knowledge mix balances for in-app preview.
    // Values are 0..1 gains applied to note velocities.
    GenreMixMap m {};
    auto part = [&] (InstrumentType t, float g) { m.partGain[(size_t) t] = g; };
    auto drum = [&] (DrumPiece d, float g) { m.drumGain[(size_t) d] = g; };

    // Sensible fallbacks
    part (InstrumentType::Melody, 0.70f);
    part (InstrumentType::Chords, 0.55f);
    part (InstrumentType::Bass, 0.78f);
    part (InstrumentType::Drums, 0.85f);
    part (InstrumentType::CounterMelody, 0.55f);
    part (InstrumentType::Arp, 0.60f);
    part (InstrumentType::Pad, 0.40f);
    drum (DrumPiece::Kick, 0.90f);
    drum (DrumPiece::Snare, 0.75f);
    drum (DrumPiece::Clap, 0.80f);
    drum (DrumPiece::ClosedHat, 0.85f);
    drum (DrumPiece::OpenHat, 0.70f);

    switch (genre)
    {
        case GenreMode::House:
            // House: bright busy hats, punchy kick, clap-forward, chords tucked
            part (InstrumentType::Chords, 0.48f);
            part (InstrumentType::Bass, 0.82f);
            part (InstrumentType::Melody, 0.62f);
            part (InstrumentType::Pad, 0.35f);
            part (InstrumentType::Arp, 0.55f);
            drum (DrumPiece::Kick, 0.92f);
            drum (DrumPiece::Snare, 0.62f);
            drum (DrumPiece::Clap, 0.88f);
            drum (DrumPiece::ClosedHat, 1.00f); // hats should cut
            drum (DrumPiece::OpenHat, 0.78f);
            break;

        case GenreMode::HipHop:
            part (InstrumentType::Chords, 0.50f);
            part (InstrumentType::Bass, 0.95f); // 808 forward
            part (InstrumentType::Melody, 0.68f);
            part (InstrumentType::Pad, 0.38f);
            drum (DrumPiece::Kick, 0.95f);
            drum (DrumPiece::Snare, 0.90f);
            drum (DrumPiece::Clap, 0.70f);
            drum (DrumPiece::ClosedHat, 0.72f);
            drum (DrumPiece::OpenHat, 0.60f);
            break;

        case GenreMode::Pop:
            part (InstrumentType::Chords, 0.60f);
            part (InstrumentType::Bass, 0.80f);
            part (InstrumentType::Melody, 0.85f); // lead vocal-like
            part (InstrumentType::Pad, 0.45f);
            drum (DrumPiece::Kick, 0.88f);
            drum (DrumPiece::Snare, 0.85f);
            drum (DrumPiece::Clap, 0.75f);
            drum (DrumPiece::ClosedHat, 0.80f);
            drum (DrumPiece::OpenHat, 0.70f);
            break;

        case GenreMode::Classical:
            part (InstrumentType::Chords, 0.70f);
            part (InstrumentType::Bass, 0.65f);
            part (InstrumentType::Melody, 0.80f);
            part (InstrumentType::Pad, 0.55f);
            part (InstrumentType::Arp, 0.60f);
            drum (DrumPiece::Kick, 0.45f);
            drum (DrumPiece::Snare, 0.40f);
            drum (DrumPiece::Clap, 0.30f);
            drum (DrumPiece::ClosedHat, 0.35f);
            drum (DrumPiece::OpenHat, 0.35f);
            break;

        case GenreMode::Techno:
            part (InstrumentType::Chords, 0.42f);
            part (InstrumentType::Bass, 0.88f);
            part (InstrumentType::Melody, 0.58f);
            part (InstrumentType::Pad, 0.40f);
            part (InstrumentType::Arp, 0.70f);
            drum (DrumPiece::Kick, 1.00f);
            drum (DrumPiece::Snare, 0.55f);
            drum (DrumPiece::Clap, 0.65f);
            drum (DrumPiece::ClosedHat, 0.95f);
            drum (DrumPiece::OpenHat, 0.75f);
            break;

        default:
            break;
    }

    return m;
}

bool detectGenreFromText (const juce::String& text, GenreMode& out)
{
    const auto t = text.toLowerCase();

    struct Hit { const char* needle; GenreMode mode; int weight; };
    const Hit hits[] = {
        { "tech house", GenreMode::House, 3 },
        { "deep house", GenreMode::House, 3 },
        { "house",      GenreMode::House, 2 },
        { "hip hop",    GenreMode::HipHop, 3 },
        { "hip-hop",    GenreMode::HipHop, 3 },
        { "hiphop",     GenreMode::HipHop, 3 },
        { "trap",       GenreMode::HipHop, 2 },
        { "rap",        GenreMode::HipHop, 2 },
        { "808",        GenreMode::HipHop, 1 },
        { "classical",  GenreMode::Classical, 3 },
        { "orchestr",   GenreMode::Classical, 2 },
        { "piano sonata", GenreMode::Classical, 2 },
        { "baroque",    GenreMode::Classical, 2 },
        { "techno",     GenreMode::Techno, 3 },
        { "minimal",    GenreMode::Techno, 1 },
        { "rave",       GenreMode::Techno, 2 },
        { "pop",        GenreMode::Pop, 2 },
        { "top 40",     GenreMode::Pop, 2 },
        { "radio",      GenreMode::Pop, 1 },
    };

    int bestScore = 0;
    GenreMode best = GenreMode::House;
    for (auto& h : hits)
    {
        if (t.contains (h.needle) && h.weight > bestScore)
        {
            bestScore = h.weight;
            best = h.mode;
        }
    }

    if (bestScore <= 0)
        return false;

    out = best;
    return true;
}

InstrumentType instrumentTypeFromChannel (int channel)
{
    switch (channel)
    {
        case 1:  return InstrumentType::Melody;
        case 2:  return InstrumentType::Chords;
        case 3:  return InstrumentType::Bass;
        case 4:  return InstrumentType::CounterMelody;
        case 5:  return InstrumentType::Arp;
        case 6:  return InstrumentType::Pad;
        case 10: return InstrumentType::Drums;
        default: return InstrumentType::Melody;
    }
}

} // namespace aimidi
