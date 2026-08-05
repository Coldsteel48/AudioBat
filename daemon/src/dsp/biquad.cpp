// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "biquad.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ramkolfx
{

void Biquad::SetCoefficients(EqFilterType FilterType, float FrequencyHz, float GainDb, float Q,
                              float SampleRate)
{
    // Keep w0 safely inside (0, pi) and alpha finite regardless of what a
    // GUI slider or a malformed-but-in-range wire value sends.
    const float Frequency = std::clamp(FrequencyHz, 1.0f, SampleRate * 0.49f);
    const float ClampedQ = std::max(Q, 0.05f);

    const float W0 = 2.0f * std::numbers::pi_v<float> * Frequency / SampleRate;
    const float CosW0 = std::cos(W0);
    const float SinW0 = std::sin(W0);
    const float Alpha = SinW0 / (2.0f * ClampedQ);
    const float A = std::pow(10.0f, GainDb / 40.0f); // amplitude, not power - peaking/shelf only

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (FilterType)
    {
    case EqFilterType::Peaking:
        b0 = 1.0f + Alpha * A;
        b1 = -2.0f * CosW0;
        b2 = 1.0f - Alpha * A;
        a0 = 1.0f + Alpha / A;
        a1 = -2.0f * CosW0;
        a2 = 1.0f - Alpha / A;
        break;
    case EqFilterType::LowShelf:
    {
        const float SqrtA2Alpha = 2.0f * std::sqrt(A) * Alpha;
        b0 = A * ((A + 1.0f) - (A - 1.0f) * CosW0 + SqrtA2Alpha);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * CosW0);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * CosW0 - SqrtA2Alpha);
        a0 = (A + 1.0f) + (A - 1.0f) * CosW0 + SqrtA2Alpha;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * CosW0);
        a2 = (A + 1.0f) + (A - 1.0f) * CosW0 - SqrtA2Alpha;
        break;
    }
    case EqFilterType::HighShelf:
    {
        const float SqrtA2Alpha = 2.0f * std::sqrt(A) * Alpha;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * CosW0 + SqrtA2Alpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * CosW0);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * CosW0 - SqrtA2Alpha);
        a0 = (A + 1.0f) - (A - 1.0f) * CosW0 + SqrtA2Alpha;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * CosW0);
        a2 = (A + 1.0f) - (A - 1.0f) * CosW0 - SqrtA2Alpha;
        break;
    }
    case EqFilterType::LowPass:
        b0 = (1.0f - CosW0) / 2.0f;
        b1 = 1.0f - CosW0;
        b2 = (1.0f - CosW0) / 2.0f;
        a0 = 1.0f + Alpha;
        a1 = -2.0f * CosW0;
        a2 = 1.0f - Alpha;
        break;
    case EqFilterType::HighPass:
        b0 = (1.0f + CosW0) / 2.0f;
        b1 = -(1.0f + CosW0);
        b2 = (1.0f + CosW0) / 2.0f;
        a0 = 1.0f + Alpha;
        a1 = -2.0f * CosW0;
        a2 = 1.0f - Alpha;
        break;
    case EqFilterType::Notch:
        b0 = 1.0f;
        b1 = -2.0f * CosW0;
        b2 = 1.0f;
        a0 = 1.0f + Alpha;
        a1 = -2.0f * CosW0;
        a2 = 1.0f - Alpha;
        break;
    }

    B0 = b0 / a0;
    B1 = b1 / a0;
    B2 = b2 / a0;
    A1 = a1 / a0;
    A2 = a2 / a0;
}

} // namespace ramkolfx
