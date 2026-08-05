// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include "ramkolfx/types.hpp"
#include "dsp_stage.hpp"

namespace ramkolfx
{

// "Off" spatial mode: static-gain downmix from 7.1 (FL, FR, FC, LFE, RL,
// RR, SL, SR) to stereo. No spatialization at all.
class PassthroughStage final : public DspStage
{
public:
    void Process(const float* Input, uint32 InputChannels,
                 float* Output, uint32 OutputChannels,
                 uint32 Frames) override;
};

} // namespace ramkolfx
