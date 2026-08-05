// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "ramkolfx/protocol.hpp"
#include "ramkolfx/types.hpp"

namespace ramkolfx
{

// Unix domain socket server for the daemon's control protocol (see
// ramkolfx/protocol.hpp for the wire format). Accepts multiple concurrent
// clients (GUI, CLI tools), each on its own thread.
class ControlServer
{
public:
    // Returns the fully encoded response message (header + payload) ready
    // to write to the socket; the handler picks the response opcode
    // (StatusResponse, DeviceListResponse, ...) so ControlServer itself
    // stays agnostic to what any given command returns.
    using CommandHandler = std::function<std::vector<uint8>(const Command&)>;

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

} // namespace ramkolfx
