// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <string>

namespace audiobat
{

// Guards against two audiobatd processes running at once. Without this,
// a second instance would happily create its own duplicate "AudioBat
// Virtual Sink" / "AudioBat Spatialized Output" PipeWire nodes: neither
// PipeWire nor ControlServer's socket bind (which unlinks and steals a
// stale socket path rather than detecting a live owner) enforce a single
// daemon on their own.
//
// Held via flock() on a lock file, which the kernel releases
// automatically on process exit (including a crash or SIGKILL) - no
// cleanup path needed.
class SingleInstanceLock
{
public:
    explicit SingleInstanceLock(std::string InLockPath);
    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    // Returns true if this process now holds the lock. On failure,
    // HolderPid() reports the PID that holds it, if available.
    bool TryAcquire();

    const std::string& HolderPid() const { return HolderPidText; }

private:
    std::string LockPath;
    std::string HolderPidText;
    int Fd = -1;
};

} // namespace audiobat
