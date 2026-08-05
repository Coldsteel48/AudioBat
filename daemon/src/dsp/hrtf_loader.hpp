// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <string>

#include "hrtf_filter.hpp"

struct MYSOFA_EASY;

namespace ramkolfx
{

// Thin wrapper around libmysofa's "easy" API. Not realtime-safe: Open()
// parses and resamples the whole SOFA file, and GetFilter() does an
// interpolated neighbor lookup - both are only ever called once, at
// BinauralStage construction time, never from DspStage::Process().
class HrtfLoader
{
public:
    ~HrtfLoader();

    // Opens a SOFA file and resamples its HRIRs to TargetSampleRate.
    // Returns false (leaving the loader unusable) on any failure: missing
    // file, unreadable/corrupt SOFA data, etc.
    bool Open(const std::string& SofaPath, float TargetSampleRate);

    bool IsOpen() const
    {
        return Easy != nullptr;
    }

    // Number of taps in each HRIR returned by GetFilter(); valid only
    // after a successful Open().
    int FilterLength() const
    {
        return FilterTapCount;
    }

    // Looks up (nearest-neighbor + interpolated) the HRIR pair for a
    // direction, matching RamkolFX's azimuth convention: 0 = front,
    // positive = left. ElevationDegrees: 0 = horizontal, positive = up.
    // Returns an empty Filter if the loader isn't open.
    HrtfFilter GetFilter(float AzimuthDegrees, float ElevationDegrees) const;

private:
    MYSOFA_EASY* Easy = nullptr;
    int FilterTapCount = 0;
    float SampleRate = 0.0f;
};

} // namespace ramkolfx
