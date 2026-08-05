// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <optional>
#include <vector>

#include "audiobat/protocol.hpp"
#include "audiobat/types.hpp"

namespace audiobat::gui
{

// Thin synchronous wrapper around the daemon's Unix control socket. Calls
// happen directly on the render thread: this is a local Unix socket with
// sub-millisecond RTT, so a short send/recv timeout is enough of a guard
// against a hung daemon without needing a background IO thread.
class ControlClient
{
public:
    // Attempts to (re)connect to the daemon's control socket. Returns
    // false (and leaves the client disconnected) if the daemon isn't
    // running or the socket doesn't exist yet.
    bool Connect();

    void Disconnect();

    bool IsConnected() const
    {
        return SocketFd >= 0;
    }

    // Each of these sends one request and waits for the matching
    // response. On any failure (send/recv error, timeout, malformed
    // reply, or an ErrorResponse from the daemon), the connection is
    // closed and nullopt is returned - the caller should treat this as
    // "disconnected" and let Connect() retry later.
    std::optional<Status> RequestStatus();
    std::optional<Status> SetSpatialMode(SpatialMode Mode);
    std::optional<Status> SetSpeakerAzimuth(uint8 SpeakerIndex, float AzimuthDegrees);
    std::optional<Status> SetSpeakerDistance(uint8 SpeakerIndex, float DistanceMeters);
    std::optional<Status> SetNearFieldEnabled(bool bEnabled);
    std::optional<Status> ResetSpeakerPositions();
    std::optional<Status> SetOutputDevice(const std::string& DeviceName);
    std::optional<Status> SetSpeakerMute(uint8 SpeakerIndex, bool bMuted);
    std::optional<Status> SetTestNoiseEnabled(bool bEnabled);
    std::optional<std::vector<AudioDeviceInfo>> RequestDevices();
    std::optional<std::vector<std::string>> RequestHrtfCatalog();
    std::optional<Status> SetHrtfFile(uint8 HrtfIndex);

private:
    struct RawResponse
    {
        Opcode MessageOpcode;
        std::vector<uint8> Payload;
    };

    // Sends a request and returns the response's opcode and raw payload
    // verbatim, without assuming what kind of response it is - shared by
    // SendStatusRequest() and RequestDevices(), which each decode it
    // differently.
    std::optional<RawResponse> SendRawRequest(const std::vector<uint8>& Request);

    // Sends a request and decodes a StatusResponse from the reply.
    std::optional<Status> SendStatusRequest(const std::vector<uint8>& Request);

    int SocketFd = -1;
};

} // namespace audiobat::gui
