// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "audiodock/protocol.hpp"

namespace audiodock
{

// Unix domain socket server for the daemon's control protocol (see
// audiodock/protocol.hpp for the wire format). Accepts multiple concurrent
// clients (GUI, CLI tools), each on its own thread.
class ControlServer
{
public:
    using CommandHandler = std::function<Status(const Command&)>;

    explicit ControlServer(std::string InSocketPath);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    void SetCommandHandler(CommandHandler InHandler);

    // Binds the socket and spawns the accept-loop thread. Returns false on
    // failure.
    bool Start();

    // Stops accepting new connections and joins the accept thread.
    void Stop();

private:
    void AcceptLoop();
    void HandleClient(int ClientFd);

    std::string SocketPath;
    int ListenFd = -1;
    std::thread AcceptThread;
    std::atomic<bool> bRunning{false};
    CommandHandler Handler;
};

// Resolves the default control socket path: $XDG_RUNTIME_DIR/audiodock/control.sock,
// falling back to /tmp/audiodock-<uid>/control.sock if XDG_RUNTIME_DIR isn't set.
std::string DefaultControlSocketPath();

} // namespace audiodock
