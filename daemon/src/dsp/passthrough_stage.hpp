// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>

#include "dsp_stage.hpp"

namespace audiodock
{

// Placeholder DSP stage: static-gain downmix from 7.1 (FL, FR, FC, LFE, RL,
// RR, SL, SR) to stereo. No spatialization - this is where ambisonics
// encode/decode will go. The "3D enabled" flag is wired end-to-end through
// the control protocol already, but doesn't change processing yet since
// there's no 3D stage to toggle.
class PassthroughStage final : public DspStage
{
public:
    void Process(const float* Input, uint32_t InputChannels,
                 float* Output, uint32_t OutputChannels,
                 uint32_t Frames) override;

    void SetThreeDEnabled(bool bEnabled) override
    {
        bThreeDEnabled.store(bEnabled, std::memory_order_relaxed);
    }

    bool IsThreeDEnabled() const override
    {
        return bThreeDEnabled.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> bThreeDEnabled{false};
};

} // namespace audiodock
