// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "control_server.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace audiobat
{

ControlServer::ControlServer(std::string InSocketPath) : SocketPath(std::move(InSocketPath))
{
}

ControlServer::~ControlServer()
{
    Stop();
}

void ControlServer::SetCommandHandler(CommandHandler InHandler)
{
    Handler = std::move(InHandler);
}

bool ControlServer::Start()
{
    ListenFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ListenFd < 0)
    {
        fprintf(stderr, "[audiobatd] failed to create control socket: %s\n", strerror(errno));
        return false;
    }

    sockaddr_un Addr{};
    Addr.sun_family = AF_UNIX;
    if (SocketPath.size() >= sizeof(Addr.sun_path))
    {
        fprintf(stderr, "[audiobatd] control socket path too long: %s\n", SocketPath.c_str());
        close(ListenFd);
        ListenFd = -1;
        return false;
    }
    std::strncpy(Addr.sun_path, SocketPath.c_str(), sizeof(Addr.sun_path) - 1);

    unlink(SocketPath.c_str()); // remove a stale socket from a previous run

    if (bind(ListenFd, reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr)) < 0)
    {
        fprintf(stderr, "[audiobatd] failed to bind control socket %s: %s\n", SocketPath.c_str(),
                strerror(errno));
        close(ListenFd);
        ListenFd = -1;
        return false;
    }

    if (listen(ListenFd, 4) < 0)
    {
        fprintf(stderr, "[audiobatd] failed to listen on control socket: %s\n", strerror(errno));
        close(ListenFd);
        ListenFd = -1;
        return false;
    }

    bRunning.store(true, std::memory_order_relaxed);
    AcceptThread = std::thread(&ControlServer::AcceptLoop, this);

    fprintf(stderr, "[audiobatd] control socket listening at %s\n", SocketPath.c_str());
    return true;
}

void ControlServer::Stop()
{
    if (!bRunning.exchange(false, std::memory_order_relaxed))
    {
        return;
    }
    if (ListenFd >= 0)
    {
        shutdown(ListenFd, SHUT_RDWR);
        close(ListenFd);
        ListenFd = -1;
    }
    if (AcceptThread.joinable())
    {
        AcceptThread.join();
    }
    unlink(SocketPath.c_str());
}

void ControlServer::AcceptLoop()
{
    while (bRunning.load(std::memory_order_relaxed))
    {
        int ClientFd = accept(ListenFd, nullptr, nullptr);
        if (ClientFd < 0)
        {
            break; // listen socket closed (Stop() was called) or a real error
        }
        std::thread(&ControlServer::HandleClient, this, ClientFd).detach();
    }
}

void ControlServer::HandleClient(int ClientFd)
{
    std::vector<uint8> Buffer;
    uint8 Chunk[512];

    for (;;)
    {
        ssize_t BytesRead = recv(ClientFd, Chunk, sizeof(Chunk), 0);
        if (BytesRead <= 0)
        {
            break; // client closed or error
        }
        Buffer.insert(Buffer.end(), Chunk, Chunk + BytesRead);

        for (;;)
        {
            auto Header = TryReadHeader(Buffer.data(), Buffer.size());
            if (!Header)
            {
                break; // need more bytes for even the header
            }
            const size_t Total = HeaderSize + Header->PayloadLength;
            if (Buffer.size() < Total)
            {
                break; // need more bytes for the full payload
            }

            const uint8* Payload = Buffer.data() + HeaderSize;
            std::vector<uint8> Response;

            auto DecodedCommand = DecodeCommand(Header->MessageOpcode, Payload, Header->PayloadLength);
            if (!DecodedCommand)
            {
                Response = EncodeErrorResponse("malformed or unknown command");
            }
            else if (Handler)
            {
                Response = Handler(*DecodedCommand);
            }
            else
            {
                Response = EncodeErrorResponse("no command handler registered");
            }

            send(ClientFd, Response.data(), Response.size(), MSG_NOSIGNAL);
            Buffer.erase(Buffer.begin(), Buffer.begin() + static_cast<std::ptrdiff_t>(Total));
        }
    }

    close(ClientFd);
}

} // namespace audiobat
