// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "partitioned_convolver.hpp"

#include <algorithm>

namespace audiobat
{

namespace
{
kiss_fft_cpx ComplexMul(const kiss_fft_cpx& A, const kiss_fft_cpx& B)
{
    return kiss_fft_cpx{A.r * B.r - A.i * B.i, A.r * B.i + A.i * B.r};
}
} // namespace

PartitionedConvolver::PartitionedConvolver() = default;

PartitionedConvolver::~PartitionedConvolver()
{
    kiss_fftr_free(ForwardPlan);
    kiss_fftr_free(InversePlan);
}

void PartitionedConvolver::Load(const float* Taps, uint32_t TapCount)
{
    // FftSize must cover the filter length plus one block of new samples
    // (N >= M + L - 1) for overlap-save's valid output region to fully
    // cover the L newest samples each step; kiss_fftr_next_fast_size_real
    // rounds up to a size KissFFT's real-FFT can factor efficiently.
    FftSize = static_cast<uint32_t>(
        kiss_fftr_next_fast_size_real(static_cast<int>(TapCount + BlockSamples - 1)));

    kiss_fftr_free(ForwardPlan);
    kiss_fftr_free(InversePlan);
    ForwardPlan = kiss_fftr_alloc(static_cast<int>(FftSize), 0, nullptr, nullptr);
    InversePlan = kiss_fftr_alloc(static_cast<int>(FftSize), 1, nullptr, nullptr);

    const uint32_t FreqBins = FftSize / 2 + 1;
    FilterFreq.assign(FreqBins, kiss_fft_cpx{0.0f, 0.0f});
    FreqScratch.assign(FreqBins, kiss_fft_cpx{0.0f, 0.0f});
    TimeScratch.assign(FftSize, 0.0f);
    Window.assign(FftSize, 0.0f);
    PendingInput.assign(BlockSamples, 0.0f);
    PendingOutput.assign(BlockSamples, 0.0f);
    PendingInputCount = 0;
    PendingOutputStart = 0;
    PendingOutputCount = 0;

    // Filter taps go at the start of a zero-padded, length-FftSize buffer
    // - standard overlap-save layout. Taps beyond FftSize are dropped
    // (can't happen given how FftSize was sized above, but guards against
    // a caller passing something unexpected).
    std::vector<float> PaddedFilter(FftSize, 0.0f);
    const uint32_t CopyCount = std::min(TapCount, FftSize);
    std::copy(Taps, Taps + CopyCount, PaddedFilter.begin());
    kiss_fftr(ForwardPlan, PaddedFilter.data(), FilterFreq.data());
}

void PartitionedConvolver::RunOneBlock()
{
    // Slide the analysis window: drop the oldest BlockSamples, append the
    // BlockSamples samples just accumulated in PendingInput.
    std::copy(Window.begin() + BlockSamples, Window.end(), Window.begin());
    std::copy(PendingInput.begin(), PendingInput.begin() + BlockSamples,
               Window.begin() + (FftSize - BlockSamples));

    kiss_fftr(ForwardPlan, Window.data(), FreqScratch.data());
    for (size_t Bin = 0; Bin < FreqScratch.size(); ++Bin)
    {
        FreqScratch[Bin] = ComplexMul(FreqScratch[Bin], FilterFreq[Bin]);
    }
    kiss_fftri(InversePlan, FreqScratch.data(), TimeScratch.data());

    // KissFFT's inverse transform is unnormalized (scales output by
    // FftSize); the valid overlap-save region is the last BlockSamples
    // samples of the circular result.
    const float Normalization = 1.0f / static_cast<float>(FftSize);
    for (uint32_t i = 0; i < BlockSamples; ++i)
    {
        PendingOutput[i] = TimeScratch[FftSize - BlockSamples + i] * Normalization;
    }
    PendingOutputStart = 0;
    PendingOutputCount = BlockSamples;
    PendingInputCount = 0;
}

void PartitionedConvolver::ProcessAccumulate(const float* Input, float* Output, uint32_t Frames)
{
    uint32_t InputConsumed = 0;
    uint32_t OutputProduced = 0;

    while (OutputProduced < Frames)
    {
        while (PendingOutputCount > 0 && OutputProduced < Frames)
        {
            Output[OutputProduced] += PendingOutput[PendingOutputStart];
            ++PendingOutputStart;
            --PendingOutputCount;
            ++OutputProduced;
        }
        if (OutputProduced >= Frames)
        {
            break;
        }

        while (PendingInputCount < BlockSamples && InputConsumed < Frames)
        {
            PendingInput[PendingInputCount] = Input[InputConsumed];
            ++PendingInputCount;
            ++InputConsumed;
        }
        if (PendingInputCount < BlockSamples)
        {
            // Ran out of input before completing another block and there's
            // no queued output left either - nothing more to produce this
            // call; the remaining Output samples simply keep whatever
            // other convolvers/callers already accumulated into them.
            break;
        }
        RunOneBlock();
    }
}

} // namespace audiobat
