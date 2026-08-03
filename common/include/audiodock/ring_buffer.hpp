// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace audiodock
{

// Single-producer, single-consumer lock-free ring buffer of samples. Used
// to bridge audio blocks between PipeWire stream process callbacks, whose
// block sizes on the capture and playback side aren't guaranteed to match.
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(size_t Capacity) : Buffer(Capacity + 1)
    {
    }

    // Pushes up to `Count` elements; returns how many were actually written
    // (fewer than `Count` if the buffer filled up).
    size_t Push(const T* Data, size_t Count)
    {
        size_t Written = 0;
        const size_t Capacity = Buffer.size();
        while (Written < Count)
        {
            const size_t WriteIdx = WriteIndex.load(std::memory_order_relaxed);
            const size_t ReadIdx = ReadIndex.load(std::memory_order_acquire);
            const size_t NextWriteIdx = (WriteIdx + 1) % Capacity;
            if (NextWriteIdx == ReadIdx)
            {
                break; // full
            }
            Buffer[WriteIdx] = Data[Written++];
            WriteIndex.store(NextWriteIdx, std::memory_order_release);
        }
        return Written;
    }

    // Pops up to `Count` elements into `Data`; returns how many were
    // actually read (fewer than `Count` if the buffer ran dry).
    size_t Pop(T* Data, size_t Count)
    {
        size_t Read = 0;
        const size_t Capacity = Buffer.size();
        while (Read < Count)
        {
            const size_t ReadIdx = ReadIndex.load(std::memory_order_relaxed);
            const size_t WriteIdx = WriteIndex.load(std::memory_order_acquire);
            if (ReadIdx == WriteIdx)
            {
                break; // empty
            }
            Data[Read++] = Buffer[ReadIdx];
            ReadIndex.store((ReadIdx + 1) % Capacity, std::memory_order_release);
        }
        return Read;
    }

    size_t AvailableToRead() const
    {
        const size_t WriteIdx = WriteIndex.load(std::memory_order_acquire);
        const size_t ReadIdx = ReadIndex.load(std::memory_order_acquire);
        return (WriteIdx + Buffer.size() - ReadIdx) % Buffer.size();
    }

private:
    std::vector<T> Buffer;
    std::atomic<size_t> WriteIndex{0};
    std::atomic<size_t> ReadIndex{0};
};

} // namespace audiodock
