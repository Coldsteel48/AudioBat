// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Abstract interface for a spatialization DSP stage sitting between the
// virtual sink's captured 7.1 audio and the stereo signal sent to real
// hardware. AudioEngine owns one instance per SpatialMode (Off/Basic/
// Advanced) and dispatches to whichever is currently selected; keeping
// this interface stable is what lets new decode strategies be added
// without touching AudioEngine's pipeline plumbing.
class DspStage
{
public:
    virtual ~DspStage() = default;

    // Processes one block of interleaved input audio into interleaved
    // output audio. Runs on the PipeWire realtime thread: implementations
    // must not allocate or block.
    virtual void Process(const float* Input, uint32 InputChannels,
                          float* Output, uint32 OutputChannels,
                          uint32 Frames) = 0;
};

// Upper bound on Frames passed to Process() in one call - mirrors how
// AudioEngine sizes its scratch/mix buffers (see MaxScratchFrames in
// audio_engine.cpp). Stages that need their own per-block scratch memory
// (e.g. BinauralStage's virtual speaker signals) size it against this
// constant once, at construction, rather than allocating inside Process().
inline constexpr uint32 MaxProcessFrames = 8192;

} // namespace ramkolfx
