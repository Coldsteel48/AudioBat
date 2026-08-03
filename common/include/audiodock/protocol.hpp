// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace audiodock
{

// Minimal binary control protocol spoken over the daemon's Unix domain
// socket. Deliberately not JSON/text: fixed 3-byte header (1-byte opcode +
// 2-byte payload length, big-endian) followed by a raw payload, no parsing
// or allocation beyond a byte buffer. This keeps the control path cheap
// enough to poll frequently from the GUI (e.g. live speaker repositioning)
// without framing/parsing overhead.
//
// Wire format:
//   [opcode: u8][length: u16 BE][payload: length bytes]
//
// The wire format is intentionally isolated to this header/.cpp pair so it
// can be swapped later (different opcode set, different framing) without
// touching daemon or GUI logic built on top of it.

enum class Opcode : uint8_t
{
    // Requests
    GetStatus = 0x01,
    SetThreeDEnabled = 0x02,
    // Responses
    StatusResponse = 0x81,
    ErrorResponse = 0x82,
};

struct MessageHeader
{
    Opcode MessageOpcode;
    uint16_t PayloadLength;
};

inline constexpr size_t HeaderSize = 3;
inline constexpr size_t MaxPayloadSize = 4096;

// Command decoded from a request message.
struct Command
{
    Opcode CommandOpcode = Opcode::GetStatus;
    bool bEnabledValue = false; // valid when CommandOpcode == SetThreeDEnabled
};

struct Status
{
    bool bThreeDEnabled = false;
};

// Attempts to read a header from the front of `Buffer`. Returns nullopt if
// fewer than HeaderSize bytes are available yet (caller should wait for
// more data on the socket).
std::optional<MessageHeader> TryReadHeader(const uint8_t* Buffer, size_t Available);

// Decodes a request payload for the given opcode. `Payload` must point at
// exactly `PayloadLength` valid bytes. Returns nullopt if the opcode isn't
// a request or the payload doesn't match what that opcode expects.
std::optional<Command> DecodeCommand(Opcode InOpcode, const uint8_t* Payload, uint16_t PayloadLength);

// Encodes a full status response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8_t> EncodeStatusResponse(const Status& InStatus);

// Encodes a full error response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8_t> EncodeErrorResponse(const std::string& Message);

// Encodes a full request message. Used by control clients (GUI, test
// tools); the daemon only decodes requests, it doesn't encode them.
std::vector<uint8_t> EncodeGetStatusRequest();
std::vector<uint8_t> EncodeSetThreeDEnabledRequest(bool bEnabled);

} // namespace audiodock
