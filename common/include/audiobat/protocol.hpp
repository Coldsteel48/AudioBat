// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace audiobat
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
    SetSpatialMode = 0x02,
    SetSpeakerAzimuth = 0x03,
    GetDevices = 0x04,
    SetOutputDevice = 0x05,
    ResetSpeakerPositions = 0x06,
    // Responses
    StatusResponse = 0x81,
    ErrorResponse = 0x82,
    DeviceListResponse = 0x83,
};

// The active spatialization DSP path. Off is a static-gain downmix
// (PassthroughStage); Basic is algebraic ambisonics decode
// (AmbisonicsStage); Advanced is HRTF-based binaural decode
// (BinauralStage). All three sit behind the same DspStage interface, so
// switching modes is just changing which stage AudioEngine dispatches to.
enum class SpatialMode : uint8_t
{
    Off = 0,
    Basic = 1,
    Advanced = 2,
};

struct MessageHeader
{
    Opcode MessageOpcode;
    uint16_t PayloadLength;
};

inline constexpr size_t HeaderSize = 3;
inline constexpr size_t MaxPayloadSize = 4096;

// Number of virtual 7.1 speakers whose azimuth is controllable; mirrors
// AmbisonicsStage::SpeakerChannel's non-LFE channel order (FL,FR,FC,RL,RR,SL,SR).
inline constexpr size_t SpeakerCount = 7;

// Command decoded from a request message.
struct Command
{
    Opcode CommandOpcode = Opcode::GetStatus;
    SpatialMode ModeValue = SpatialMode::Off; // valid when CommandOpcode == SetSpatialMode
    uint8_t SpeakerIndex = 0;     // valid when CommandOpcode == SetSpeakerAzimuth, 0..SpeakerCount-1
    float AzimuthDegrees = 0.0f;  // valid when CommandOpcode == SetSpeakerAzimuth
    std::string OutputDeviceName; // valid when CommandOpcode == SetOutputDevice
};

struct Status
{
    SpatialMode Mode = SpatialMode::Off;
    std::array<float, SpeakerCount> SpeakerAzimuthDegrees{};
    std::string OutputDeviceName; // node.name of the currently pinned hardware output sink
};

// A real hardware playback sink the daemon can pin HardwareOutput to.
// Name is the PipeWire node.name (stable identifier, used to select the
// device); Description is node.description (human-readable, shown in UI).
struct AudioDeviceInfo
{
    std::string Name;
    std::string Description;
};

// Attempts to read a header from the front of `Buffer`. Returns nullopt if
// fewer than HeaderSize bytes are available yet (caller should wait for
// more data on the socket).
std::optional<MessageHeader> TryReadHeader(const uint8_t* Buffer, size_t Available);

// Decodes a request payload for the given opcode. `Payload` must point at
// exactly `PayloadLength` valid bytes. Returns nullopt if the opcode isn't
// a request or the payload doesn't match what that opcode expects.
std::optional<Command> DecodeCommand(Opcode InOpcode, const uint8_t* Payload, uint16_t PayloadLength);

// Decodes a StatusResponse payload. Used by control clients (GUI, test
// tools) to parse what the daemon sends back; the daemon only encodes
// responses, it doesn't decode them. Returns nullopt if PayloadLength
// doesn't match the expected `1 + 4*SpeakerCount` layout.
std::optional<Status> DecodeStatusResponse(const uint8_t* Payload, uint16_t PayloadLength);

// Decodes an ErrorResponse payload into its message string.
std::string DecodeErrorResponse(const uint8_t* Payload, uint16_t PayloadLength);

// Decodes a DeviceListResponse payload. Used by control clients (GUI, test
// tools) to parse what the daemon sends back. Returns nullopt if the
// payload is truncated or malformed.
std::optional<std::vector<AudioDeviceInfo>> DecodeDeviceListResponse(const uint8_t* Payload,
                                                                       uint16_t PayloadLength);

// Encodes a full status response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8_t> EncodeStatusResponse(const Status& InStatus);

// Encodes a full error response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8_t> EncodeErrorResponse(const std::string& Message);

// Encodes a full device list response message (header + payload), ready to
// write directly to the socket. Entries that would push the payload past
// MaxPayloadSize are dropped from the end rather than truncated mid-entry.
std::vector<uint8_t> EncodeDeviceListResponse(const std::vector<AudioDeviceInfo>& Devices);

// Encodes a full request message. Used by control clients (GUI, test
// tools); the daemon only decodes requests, it doesn't encode them.
std::vector<uint8_t> EncodeGetStatusRequest();
std::vector<uint8_t> EncodeSetSpatialModeRequest(SpatialMode Mode);
std::vector<uint8_t> EncodeSetSpeakerAzimuthRequest(uint8_t SpeakerIndex, float AzimuthDegrees);
std::vector<uint8_t> EncodeGetDevicesRequest();
std::vector<uint8_t> EncodeSetOutputDeviceRequest(const std::string& DeviceName);
std::vector<uint8_t> EncodeResetSpeakerPositionsRequest();

// Resolves the default control socket path: $XDG_RUNTIME_DIR/audiobat/control.sock,
// falling back to /tmp/audiobat-<uid>/control.sock if XDG_RUNTIME_DIR isn't set.
// Shared between the daemon (binds it) and control clients like the GUI
// (connect to it), so it lives here rather than in daemon-only code.
std::string DefaultControlSocketPath();

} // namespace audiobat
