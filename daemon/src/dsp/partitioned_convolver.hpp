// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <vector>

#include <kiss_fftr.h>

#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Single-partition overlap-save FFT convolver (KissFFT-backed). Used by
// BinauralStage to convolve each virtual loudspeaker feed against an HRIR.
// Measured HRTF filters are a few hundred taps - short enough that one FFT
// block per BlockSamples of new input is plenty, without the added
// bookkeeping of a full multi-partition frequency-domain delay line.
//
// Processes a fixed BlockSamples new input samples per internal FFT step
// (chosen small to keep added latency low, ~2-3 ms at 48 kHz) regardless
// of the caller's block size: ProcessAccumulate() queues input/output
// across calls via pre-allocated ring buffers, so it's safe to call from
// the realtime thread with any Frames count once Load() has run.
class PartitionedConvolver
{
public:
    PartitionedConvolver();
    ~PartitionedConvolver();

    PartitionedConvolver(const PartitionedConvolver&) = delete;
    PartitionedConvolver& operator=(const PartitionedConvolver&) = delete;

    // One-time setup: sizes the FFT from the filter length, transforms the
    // (zero-padded) filter into the frequency domain, and allocates every
    // buffer ProcessAccumulate() touches. Not realtime-safe - call once,
    // before the stage owning this convolver starts processing audio.
    void Load(const float* Taps, uint32 TapCount);

    // Realtime-safe once Load() has run: convolves Frames of mono Input
    // against the loaded filter and ADDS the result into Output (so
    // callers can sum several convolvers' contributions without a
    // separate mix pass - Output is not cleared first). No allocation.
    void ProcessAccumulate(const float* Input, float* Output, uint32 Frames);

private:
    // New input samples consumed per internal FFT block. Fixed rather
    // than derived from the filter length: keeps added latency small and
    // predictable regardless of which SOFA file is loaded.
    static constexpr uint32 BlockSamples = 128;

    void RunOneBlock();

    uint32 FftSize = 0;

    kiss_fftr_cfg ForwardPlan = nullptr;
    kiss_fftr_cfg InversePlan = nullptr;

    std::vector<kiss_fft_cpx> FilterFreq; // FftSize/2+1

    // Sliding time-domain analysis window: the last FftSize input samples.
    std::vector<float> Window;

    std::vector<kiss_fft_cpx> FreqScratch; // FftSize/2+1
    std::vector<float> TimeScratch;        // FftSize

    // Input accumulated since the last full block; drained by RunOneBlock.
    std::vector<float> PendingInput;
    uint32 PendingInputCount = 0;

    // Valid convolution output produced by RunOneBlock, not yet handed to
    // a caller.
    std::vector<float> PendingOutput;
    uint32 PendingOutputStart = 0;
    uint32 PendingOutputCount = 0;
};

} // namespace ramkolfx
