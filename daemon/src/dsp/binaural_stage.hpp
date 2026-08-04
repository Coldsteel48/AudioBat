// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "ambisonics_stage.hpp"
#include "dsp_stage.hpp"
#include "hrtf_loader.hpp"
#include "partitioned_convolver.hpp"
#include "speaker_layout.hpp"

namespace audiobat
{

class BinauralVoice;

// Which kind of HRTF source BinauralStage renders its virtual speakers
// through - see data/hrtf/README.md for what's bundled under each.
enum class HrtfSourceKind : uint8_t
{
    SofaFile,              // measured HRIR data, loaded via HrtfLoader/libmysofa
    SyntheticSphericalHead, // procedural rigid-sphere model, no data file at all
};

// "Advanced" spatial mode: HRTF-based binaural decode. Encodes 7.1 input
// to the same first-order B-format field AmbisonicsStage uses (via the
// shared SpeakerLayout), decodes that field to a fixed 8-point virtual
// loudspeaker array, then convolves each virtual speaker's signal with
// that direction's HRIR - either measured (SOFA file, via HrtfLoader) or
// procedurally computed (see synthetic_hrtf.hpp) - and sums the results
// into stereo output. This is what actually preserves front/back
// distinction that a 2-speaker algebraic decode can't.
//
// If a SofaFile source fails to load, falls back to delegating to the
// same algebraic decode AmbisonicsStage does, so Advanced mode never
// produces silence just because HRTF data is unavailable. The synthetic
// source has no such failure mode - it's pure computation.
//
// Near-field distance mode (additive - see docs/near-field-distance-plan.md):
// alongside the signal path described above ("the original path"),
// BinauralStage also always builds a second, independent signal path that
// renders each of the 7 sources through its own direct HRTF+near-field
// convolution instead of the shared-field/8-virtual-speaker decode -
// necessary because per-speaker distance needs a per-ear *filter*, and
// filters can't be applied after multiple sources are already summed into
// one shared field. Process() picks between the two based on
// SetNearFieldEnabled's most recent value, crossfading (~50ms, same
// mechanism as HrtfDeck/BinauralVoice) rather than switching instantly so
// toggling mid-playback doesn't click. With near-field never enabled, the
// original path is exactly what runs, unmodified from before this second
// path existed - RenderOriginalPath's body is an unchanged extraction of
// what Process() used to do directly.
class BinauralStage final : public DspStage
{
public:
    // SofaPath: path to a SOFA-format HRTF file, ignored when Kind is
    // SyntheticSphericalHead. SampleRate: the pipeline's fixed sample
    // rate (HRIRs are resampled to this rate on load). bInitialNearFieldEnabled:
    // near-field mode's toggle state to start in - matters because a
    // fresh BinauralStage can be constructed mid-session (HrtfDeck
    // swapping in a new HRTF source), and should preserve whatever the
    // toggle was already set to rather than resetting it to off.
    BinauralStage(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath,
                  float SampleRate, bool bInitialNearFieldEnabled);

    // Declared (not defaulted inline) and defined in binaural_stage.cpp:
    // Voices below is an array of unique_ptr<BinauralVoice>, and
    // BinauralVoice is only forward-declared here, so the implicit
    // destructor's array-destruction logic needs BinauralVoice's complete
    // type - only available where binaural_voice.hpp is actually included.
    // Without this, any other translation unit that destroys a
    // BinauralStage (e.g. hrtf_deck.cpp, via shared_ptr) fails to compile.
    ~BinauralStage() override;

    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

    static constexpr uint32_t VirtualSpeakerCount = 8;

    // Callable from any thread. Just publishes the new toggle state for
    // Process() to crossfade toward on its next call - see class comment.
    void SetNearFieldEnabled(bool bEnabled);

    // Not realtime-safe: looks up/computes the filter for Speaker's new
    // azimuth/distance and publishes it for Process() to crossfade into,
    // same as HrtfDeck::SwitchTo(). A no-op if this HRTF source failed to
    // load (SofaFile kind only) - nothing valid to build a voice from.
    // Called from AudioEngine's control-command handler whenever a
    // speaker's azimuth or distance changes, regardless of whether
    // near-field mode is currently on, so a voice is always up to date by
    // the time the toggle switches to it.
    void RebuildVoiceForSpeaker(SpeakerLayout::SpeakerChannel Speaker, float AzimuthDegrees,
                                 float DistanceMeters);

    // Not realtime-safe (may deallocate). Drops whatever each voice's last
    // completed crossfade faded out of - see CrossfadingSlot::CollectGarbage.
    void CollectVoiceGarbage();

private:
    const SpeakerLayout& Layout;
    bool bHrtfLoaded = false;

    // Reused as the "HRTF unavailable" fallback path, same pattern
    // AmbisonicsStage previously used for its own "3D off" fallback; also
    // what the near-field path falls back to when !bHrtfLoaded, for the
    // same reason.
    AmbisonicsStage FallbackStage;

    HrtfLoader Hrtf;
    std::array<PartitionedConvolver, VirtualSpeakerCount> LeftConvolvers;
    std::array<PartitionedConvolver, VirtualSpeakerCount> RightConvolvers;

    // Fixed virtual speaker decode directions (0 deg = front, positive =
    // left), independent of SpeakerLayout's live-repositionable encode
    // azimuths - these are where BinauralStage renders the sound field
    // to before convolving, not where the 7.1 sources sit.
    std::array<float, VirtualSpeakerCount> DecodeCos{};
    std::array<float, VirtualSpeakerCount> DecodeSin{};

    // Per-block scratch, sized once against MaxProcessFrames so Process()
    // never allocates. VirtualSpeakerSignal[k] holds virtual speaker k's
    // mono decode for the current block; MixLeft/MixRight accumulate each
    // speaker's convolved contribution before final interleaving.
    std::array<std::vector<float>, VirtualSpeakerCount> VirtualSpeakerSignal;
    std::vector<float> MixLeft;
    std::vector<float> MixRight;

    // Renders exactly what Process() used to do directly, before the
    // near-field path existed - an unmodified extraction, not a
    // reimplementation. Input/Output are already known to be 8/2-channel
    // interleaved and Frames already clamped by the time Process() calls
    // this.
    void RenderOriginalPath(const float* Input, float* Output, uint32_t Frames);

    // --- Near-field path (additive) ---

    void RenderNearFieldPath(const float* Input, float* Output, uint32_t Frames);

    HrtfSourceKind SourceKind;

    // Computed once at construction from SourceKind/Hrtf - see
    // hrtf_gain_normalization.hpp. Always 1.0 for SyntheticSphericalHead.
    float NearFieldNormalizationGain = 1.0f;

    // One per non-LFE 7.1 channel; null (and RebuildVoiceForSpeaker/
    // RenderNearFieldPath skip accordingly) only when SourceKind is
    // SofaFile and it failed to load - same condition FallbackStage
    // covers for the original path.
    std::array<std::unique_ptr<BinauralVoice>, SpeakerLayout::SpeakerCount> Voices;

    // Per-block scratch, sized once against MaxProcessFrames.
    // SourceScratch[i] holds speaker i's de-interleaved mono input;
    // VoiceScratch holds one voice's interleaved stereo output before
    // being summed into the near-field path's result.
    std::array<std::vector<float>, SpeakerLayout::SpeakerCount> SourceScratch;
    std::vector<float> VoiceScratch;

    // Toggle-transition crossfade state - RT-thread-owned except
    // bNearFieldRequested, which SetNearFieldEnabled publishes from any
    // thread.
    std::atomic<bool> bNearFieldRequested;
    bool bNearFieldActive;
    uint32_t ToggleFadeFramesRemaining = 0;
    const uint32_t ToggleCrossfadeFrames; // ~50ms of frames at construction SampleRate

    // Holds each path's output during a toggle transition, so they can be
    // blended - sized once against MaxProcessFrames.
    std::vector<float> OriginalPathScratch;
    std::vector<float> NearFieldPathScratch;
};

} // namespace audiobat
