// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cstdint>

namespace audiodock
{

// Abstract interface for the spatialization DSP stage sitting between the
// virtual sink's captured 7.1 audio and the stereo signal sent to real
// hardware. The real implementation (ambisonics encode -> manipulate ->
// stereo decode) will replace PassthroughStage; keeping this interface
// stable is what lets that swap happen without touching AudioEngine.
class DspStage
{
public:
    virtual ~DspStage() = default;

    // Processes one block of interleaved input audio into interleaved
    // output audio. Runs on the PipeWire realtime thread: implementations
    // must not allocate or block.
    virtual void Process(const float* Input, uint32_t InputChannels,
                          float* Output, uint32_t OutputChannels,
                          uint32_t Frames) = 0;

    virtual void SetThreeDEnabled(bool bEnabled) = 0;
    virtual bool IsThreeDEnabled() const = 0;
};

} // namespace audiodock
