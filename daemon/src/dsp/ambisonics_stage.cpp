// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "ambisonics_stage.hpp"

#include <cstring>

namespace audiobat
{

namespace
{

// 1/sqrt(2), the standard first-order ambisonics W-channel weight.
// Written as a literal rather than std::sqrt(2.0f): std::sqrt isn't
// portably constexpr in C++20, and the value is fixed at compile time.
constexpr float OneOverSqrtTwo = 0.70710678f;

// Virtual stereo decode speakers sit at +-45 deg, trading some front/back
// separation (carried by the field's X component) for more perceived
// left/right width versus a narrower +-30 deg stereo triangle. First
// thing worth tuning once there's real listening feedback.
//
// cos(45 deg) == sin(45 deg) == 1/sqrt(2); precomputed as literals for the
// same reason as OneOverSqrtTwo above. Since cos is even and sin is odd,
// the right channel reuses these same two constants with the sine term's
// sign flipped instead of needing separate +45/-45 constants (see
// Process() below).
constexpr float DecodeCos = OneOverSqrtTwo;
constexpr float DecodeSin = OneOverSqrtTwo;

// Plain first-order ambisonic decode to just two speakers has weak L/R
// separation: both output channels share the same W (omni) and X
// (front/back) terms, and only the Y term differs between them (with an
// opposite sign). With Y's natural amplitude, that shared content
// dominates and everything collapses toward a blurry phantom center -
// confirmed by ear: a source panned hard to one side still sounded
// mostly centered rather than clearly one-sided.
//
// WidthGain exaggerates just the L/R-differencing Y term (not the
// physical decode speaker azimuth above, which stays a separate,
// geometric concept) to push panned content further apart. 2.0x roughly
// doubles the L/R differentiation without discarding the W/X content that
// still carries useful front-weighted energy.
constexpr float WidthGain = 2.0f;

// Worst-case unnormalized decode gain no longer happens exactly at the
// decode azimuth once WidthGain != 1 (the peak shifts to
// atan(WidthGain) as the source azimuth). For a unit-amplitude source at
// azimuth theta: raw = 0.5 + OneOverSqrtTwo * (cos(theta) + WidthGain *
// sin(theta)), and (cos(theta) + WidthGain*sin(theta)) has amplitude
// sqrt(1 + WidthGain^2) regardless of theta, so:
//   MaxRaw = 0.5 + OneOverSqrtTwo * sqrt(1 + WidthGain^2)
//          = 0.5 + 0.70710678 * sqrt(5)   (WidthGain = 2.0)
//          = 0.5 + 0.70710678 * 2.2360680
//          = 2.0811388
// Normalizing by 1/MaxRaw (~0.4805061) brings that worst case to unity,
// same safety guarantee as before, just re-derived for the boosted Y term.
constexpr float DecodeNormalization = 0.4805061f;

constexpr float LfeGain = 0.5f; // matches PassthroughStage

// Raw 7.1 input channel order, matching the layout VirtualSink captures.
enum SevenOneChannel : uint32_t
{
    FL = 0,
    FR = 1,
    FC = 2,
    LFE = 3,
    RL = 4,
    RR = 5,
    SL = 6,
    SR = 7,
    SevenOneChannelCount = 8,
};

} // namespace

void AmbisonicsStage::Process(const float* Input, uint32_t InputChannels,
                               float* Output, uint32_t OutputChannels,
                               uint32_t Frames)
{
    if (InputChannels != SevenOneChannelCount || OutputChannels != 2)
    {
        // Not the shape this stage knows how to handle; output silence
        // rather than reading/writing out of bounds.
        std::memset(Output, 0, sizeof(float) * OutputChannels * Frames);
        return;
    }

    // Snapshot live speaker positions once per block, not per sample -
    // they only change from control commands, never at audio rate.
    const SpeakerLayout::Directions Dirs = Layout.SnapshotDirections();

    for (uint32_t FrameIndex = 0; FrameIndex < Frames; ++FrameIndex)
    {
        const float* InFrame = Input + FrameIndex * InputChannels;
        float* OutFrame = Output + FrameIndex * OutputChannels;

        const float Sources[SpeakerLayout::SpeakerCount] = {
            InFrame[FL], InFrame[FR], InFrame[FC], InFrame[RL], InFrame[RR], InFrame[SL], InFrame[SR],
        };

        // Encode: sum all 7 point sources into one first-order B-format
        // field (W = omnidirectional, X = front/back, Y = left/right).
        float FieldW, FieldX, FieldY;
        SpeakerLayout::Encode(Sources, Dirs, FieldW, FieldX, FieldY);

        // Decode: sample the field at two virtual speaker directions,
        // +-DecodeAzimuthDegrees. cos is even and sin is odd, so the
        // right channel reuses DecodeCos/DecodeSin with the Y term's
        // sign flipped instead of needing separate constants. WidthGain
        // only scales the Y (L/R-differencing) term - see comment above.
        const float Side = FieldY * DecodeSin * WidthGain;
        const float Left = (FieldW * OneOverSqrtTwo + FieldX * DecodeCos + Side) * DecodeNormalization;
        const float Right = (FieldW * OneOverSqrtTwo + FieldX * DecodeCos - Side) * DecodeNormalization;

        OutFrame[0] = Left + LfeGain * InFrame[LFE];
        OutFrame[1] = Right + LfeGain * InFrame[LFE];
    }
}

} // namespace audiobat
