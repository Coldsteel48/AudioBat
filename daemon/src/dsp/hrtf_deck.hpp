// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "ramkolfx/types.hpp"
#include "binaural_stage.hpp"
#include "crossfading_slot.hpp"
#include "dsp_stage.hpp"
#include "speaker_layout.hpp"

namespace ramkolfx
{

// Wraps two BinauralStages (via CrossfadingSlot) so switching the active
// HRTF source (SOFA file or synthetic model) never restarts the daemon
// and never clicks: a pending switch is built off the realtime thread,
// then Process() crossfades into it over a fixed window once published.
//
// Thread model: SwitchTo()/CollectGarbage() run on one of the daemon's
// control-client worker threads (ControlServer::HandleClient spawns a
// plain detached thread per connection - never the PipeWire main loop or
// the audio callback). Process() runs on the realtime audio thread - see
// CrossfadingSlot for how the two sides stay safely decoupled.
class HrtfDeck final : public DspStage
{
public:
    HrtfDeck(const SpeakerLayout& InLayout, HrtfSourceKind Kind, const std::string& SofaPath, float SampleRate,
             bool bInitialNearFieldEnabled);

    void Process(const float* Input, uint32 InputChannels,
                 float* Output, uint32 OutputChannels,
                 uint32 Frames) override;

    // Not realtime-safe: builds a brand-new BinauralStage synchronously
    // (SOFA parse + convolver setup - same "call once, not from
    // Process()" cost BinauralStage's own constructor already carries),
    // then publishes it for Process() to crossfade into on its next call.
    // The new stage starts in whatever near-field state was most recently
    // set via SetNearFieldEnabled, so switching HRTF source doesn't reset
    // that toggle.
    void SwitchTo(HrtfSourceKind Kind, const std::string& SofaPath);

    // Pass-throughs to whichever BinauralStage is currently live - see
    // BinauralStage's own doc comments for what each does. Callable from
    // any thread, same as their BinauralStage counterparts.
    void SetNearFieldEnabled(bool bEnabled);
    void RebuildVoiceForSpeaker(SpeakerLayout::SpeakerChannel Speaker, float AzimuthDegrees,
                                 float DistanceMeters);

    // Drops any stage(s) a completed crossfade faded out of, and forwards
    // into the live stage to do the same for its own per-voice crossfades.
    // Safe to call frequently; a no-op when there's nothing to collect.
    // Must not be called from the audio thread - the drop itself may
    // deallocate. AudioEngine piggybacks this on every HandleControlCommand
    // call (i.e. every GUI status poll), so no dedicated timer is needed.
    void CollectGarbage();

private:
    const SpeakerLayout& Layout;
    float SampleRate;
    std::atomic<bool> bNearFieldEnabled{false};

    static constexpr uint32 ChannelsPerFrame = 2; // stereo output

    CrossfadingSlot<BinauralStage> Slot;
};

} // namespace ramkolfx
