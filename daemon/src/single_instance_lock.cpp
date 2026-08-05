// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "single_instance_lock.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <fcntl.h>
#include <sys/file.h>

namespace ramkolfx
{

SingleInstanceLock::SingleInstanceLock(std::string InLockPath) : LockPath(std::move(InLockPath))
{
}

SingleInstanceLock::~SingleInstanceLock()
{
    if (Fd >= 0)
    {
        close(Fd); // releases the flock
    }
}

bool SingleInstanceLock::TryAcquire()
{
    Fd = open(LockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (Fd < 0)
    {
        fprintf(stderr, "[ramkolfxd] failed to open lock file %s: %s\n", LockPath.c_str(), strerror(errno));
        return false;
    }

    if (flock(Fd, LOCK_EX | LOCK_NB) < 0)
    {
        // Someone else holds it. Best-effort read of the PID it left
        // behind below, purely for the error message - locking itself
        // doesn't depend on this.
        char Buffer[32] = {};
        const ssize_t BytesRead = pread(Fd, Buffer, sizeof(Buffer) - 1, 0);
        if (BytesRead > 0)
        {
            HolderPidText.assign(Buffer, static_cast<size_t>(BytesRead));
            while (!HolderPidText.empty() && std::isspace(static_cast<unsigned char>(HolderPidText.back())))
            {
                HolderPidText.pop_back();
            }
        }
        close(Fd);
        Fd = -1;
        return false;
    }

    const std::string PidText = std::to_string(getpid()) + "\n";
    if (ftruncate(Fd, 0) == 0)
    {
        [[maybe_unused]] const ssize_t Written = write(Fd, PidText.data(), PidText.size());
    }

    return true;
}

} // namespace ramkolfx
