// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// A single second-order IIR section (RBJ audio-eq-cookbook coefficients:
// https://www.w3.org/TR/audio-eq-cookbook/), run in Direct Form II
// Transposed - the standard choice for coefficients that change at
// control-rate while audio keeps flowing, since it stays numerically
// well-behaved across a coefficient swap without needing to reset state.
// One instance holds one channel's filter state; a stereo signal needs two
// (see GraphicEqFilter).
class Biquad
{
public:
    // Recomputes the five normalized coefficients (b0,b1,b2,a1,a2; b2/a0
    // already folded in) for one EqBand at SampleRate. Not realtime-safe in
    // spirit (several trig calls) but allocation-free - safe to call from a
    // control thread building a fresh GraphicEqFilter, not from the audio
    // thread mid-block. Leaves the running Direct-Form-II-Transposed state
    // (Z1/Z2) untouched, so retuning an already-playing band doesn't click.
    void SetCoefficients(EqFilterType FilterType, float FrequencyHz, float GainDb, float Q, float SampleRate);

    // Filters one sample. Realtime-safe: no allocation, no branching beyond
    // the arithmetic itself.
    float ProcessSample(float In)
    {
        const float Out = B0 * In + Z1;
        Z1 = B1 * In - A1 * Out + Z2;
        Z2 = B2 * In - A2 * Out;
        return Out;
    }

private:
    float B0 = 1.0f, B1 = 0.0f, B2 = 0.0f;
    float A1 = 0.0f, A2 = 0.0f;
    float Z1 = 0.0f, Z2 = 0.0f; // Direct Form II Transposed delay state
};

} // namespace ramkolfx
