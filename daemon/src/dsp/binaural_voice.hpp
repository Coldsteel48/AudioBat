// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "audiobat/types.hpp"
#include "binaural_stage.hpp" // HrtfSourceKind
#include "crossfading_slot.hpp"
#include "hrtf_loader.hpp"
#include "partitioned_convolver.hpp"

namespace audiobat
{

// One direction+distance's loaded Left/Right convolver pair - the "live"
// object BinauralVoice's CrossfadingSlot swaps between on rebuild. Not
// meant to be constructed directly by anything but BinauralVoice.
class BinauralVoiceFilter
{
public:
    explicit BinauralVoiceFilter(const HrtfFilter& Filter);

    // Realtime-safe. MonoInput/StereoOutput are Frames samples of 1 and 2
    // channels respectively (interleaved for the latter).
    void Process(const float* MonoInput, float* StereoOutput, uint32 Frames);

private:
    PartitionedConvolver LeftConvolver;
    PartitionedConvolver RightConvolver;

    // Per-call scratch for the convolvers' separate mono outputs, sized
    // once against MaxProcessFrames so Process() never allocates - same
    // pattern as BinauralStage's own MixLeft/MixRight.
    std::vector<float> ScratchLeft;
    std::vector<float> ScratchRight;
};

// Renders one 7.1 source channel straight to binaural stereo: far-field
// HRTF (measured or synthetic) for its current azimuth, cascaded with the
// near-field proximity correction for its current distance (see
// near_field_filter.hpp), normalized to a consistent level across HRTF
// sources (see hrtf_gain_normalization.hpp). This is what BinauralStage's
// near-field-enabled signal path uses instead of the original shared-
// ambisonic-field/8-virtual-speaker decode - see
// docs/near-field-distance-plan.md for why per-source direct convolution
// is required to give distance real per-ear meaning.
//
// Thread model: same as CrossfadingSlot (which this wraps) - Rebuild()/
// CollectGarbage() run on a control thread, Process() on the audio
// thread.
class BinauralVoice
{
public:
    // Kind/Source/NormalizationGain describe the HRTF source (Source is
    // the already-open loader to query when Kind is SofaFile - owned by
    // the enclosing BinauralStage, must outlive this voice; ignored,
    // may be null, when Kind is SyntheticSphericalHead).
    BinauralVoice(HrtfSourceKind Kind, const HrtfLoader* Source, float NormalizationGain,
                  float AzimuthDegrees, float DistanceMeters, float SampleRate);

    // Not realtime-safe: looks up/computes the filter for the new
    // azimuth/distance and publishes it for Process() to crossfade into.
    // Kind/Source/NormalizationGain are re-passed rather than cached
    // internally so a voice never silently uses stale HRTF-source state.
    void Rebuild(HrtfSourceKind Kind, const HrtfLoader* Source, float NormalizationGain,
                 float AzimuthDegrees, float DistanceMeters);

    // Realtime-safe.
    void Process(const float* MonoInput, float* StereoOutput, uint32 Frames);

    // Not realtime-safe (may deallocate). See CrossfadingSlot::CollectGarbage.
    void CollectGarbage();

private:
    float SampleRate;

    static constexpr uint32 ChannelsPerFrame = 2; // stereo output

    CrossfadingSlot<BinauralVoiceFilter> Slot;
};

} // namespace audiobat
