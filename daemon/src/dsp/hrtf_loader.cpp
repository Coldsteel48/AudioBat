// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "hrtf_loader.hpp"

#include <cstdio>

#include <mysofa.h>

namespace audiobat
{

HrtfLoader::~HrtfLoader()
{
    if (Easy)
    {
        mysofa_close(Easy);
    }
}

bool HrtfLoader::Open(const std::string& SofaPath, float TargetSampleRate)
{
    if (Easy)
    {
        mysofa_close(Easy);
        Easy = nullptr;
    }

    int Err = MYSOFA_OK;
    Easy = mysofa_open(SofaPath.c_str(), TargetSampleRate, &FilterTapCount, &Err);
    if (!Easy || Err != MYSOFA_OK)
    {
        fprintf(stderr, "[audiobatd] failed to load HRTF SOFA file '%s' (mysofa error %d)\n",
                SofaPath.c_str(), Err);
        Easy = nullptr;
        FilterTapCount = 0;
        return false;
    }
    SampleRate = TargetSampleRate;
    return true;
}

HrtfLoader::Filter HrtfLoader::GetFilter(float AzimuthDegrees, float ElevationDegrees) const
{
    Filter Out;
    if (!Easy)
    {
        return Out;
    }

    // mysofa_s2c uses the same azimuth convention AudioBat does: 0 =
    // front, positive = left (SOFA's +X = front, +Y = left coordinate
    // system). Radius is arbitrary for a far-field HRTF lookup.
    float Coordinate[3] = {AzimuthDegrees, ElevationDegrees, 1.0f};
    mysofa_s2c(Coordinate);

    Out.Left.resize(static_cast<size_t>(FilterTapCount));
    Out.Right.resize(static_cast<size_t>(FilterTapCount));

    float DelayLeftSeconds = 0.0f;
    float DelayRightSeconds = 0.0f;
    mysofa_getfilter_float(Easy, Coordinate[0], Coordinate[1], Coordinate[2], Out.Left.data(),
                            Out.Right.data(), &DelayLeftSeconds, &DelayRightSeconds);

    // mysofa_getfilter_float reports delay in seconds, not samples (see
    // libmysofa's easy.c: the *_short variant is the one that multiplies
    // by the sampling rate, this one doesn't) - convert here so callers
    // only ever deal in samples.
    Out.DelayLeftSamples = DelayLeftSeconds * SampleRate;
    Out.DelayRightSamples = DelayRightSeconds * SampleRate;
    return Out;
}

} // namespace audiobat
