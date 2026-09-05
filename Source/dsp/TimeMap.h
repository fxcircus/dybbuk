#pragma once

#include <cmath>

#include "ChipConstants.h"

// The musical side of Time: note divisions for Sync, and the conversions
// between a division, a delay in seconds, and the 0..1 knob position.
//
// Free and synced Time differ only in the number that ends up here. Both go
// through the same 20 ms log-domain smoother in ChipClock, so changing a sync
// division smears the pitch exactly like turning the knob does, which is the
// hardware behaviour the plan asks for.
//
// No JUCE dependency: Parameters.cpp (readouts), the processor (per block)
// and EngineTest all share this one source of truth.
namespace timemap
{
    struct Division
    {
        const char* name;
        double beats;
        bool isBars;
    };

    inline constexpr int kDivisionCount = 14;

    // Ascending duration. "2 bars" is deliberately absent: it exceeds the
    // 3.67 s ceiling at any tempo under 131 BPM, so it would mostly display
    // a value the engine cannot deliver.
    inline constexpr Division kDivisions[kDivisionCount] = {
        { "1/32", 0.125, false },      { "1/16T", 1.0 / 6.0, false }, { "1/16", 0.25, false },
        { "1/8T", 1.0 / 3.0, false },  { "1/16D", 0.375, false },     { "1/8", 0.5, false },
        { "1/4T", 2.0 / 3.0, false },  { "1/8D", 0.75, false },       { "1/4", 1.0, false },
        { "1/2T", 4.0 / 3.0, false },  { "1/4D", 1.5, false },        { "1/2", 2.0, false },
        { "1/2D", 3.0, false },        { "1 bar", 1.0, true }
    };

    // Sync keeps the Time knob's 0..1 range and quantises it to 14 detents.
    inline int divisionIndexForTime01 (float t) noexcept
    {
        const int i = (int) std::lround ((double) t * (kDivisionCount - 1));
        return i < 0 ? 0 : (i >= kDivisionCount ? kDivisionCount - 1 : i);
    }

    struct SyncedDelay
    {
        double seconds;
        bool clamped; // the division asked for longer than the memory can hold
    };

    inline SyncedDelay syncedDelaySeconds (int index, double bpm, double beatsPerBar) noexcept
    {
        const Division& d = kDivisions[index < 0 ? 0 : (index >= kDivisionCount ? kDivisionCount - 1 : index)];
        const double beats = d.isBars ? d.beats * beatsPerBar : d.beats;
        const double raw = beats * 60.0 / (bpm > 1.0 ? bpm : 120.0);
        const double lo = (double) pt::kDelayMinSec, hi = (double) pt::kDelayMaxSec;
        const double s = raw < lo ? lo : (raw > hi ? hi : raw);
        return { s, s < raw - 1.0e-9 || s > raw + 1.0e-9 };
    }
} // namespace timemap
