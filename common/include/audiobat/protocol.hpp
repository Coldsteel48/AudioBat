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
#include <optional>
#include <string>
#include <vector>

#include "audiobat/types.hpp"

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

enum class Opcode : uint8
{
    // Requests
    GetStatus = 0x01,
    SetSpatialMode = 0x02,
    SetSpeakerAzimuth = 0x03,
    GetDevices = 0x04,
    SetOutputDevice = 0x05,
    ResetSpeakerPositions = 0x06,
    SetSpeakerMute = 0x07,
    SetTestNoise = 0x08,
    GetHrtfCatalog = 0x09,
    SetHrtfFile = 0x0A,
    SetSpeakerDistance = 0x0B,
    SetNearFieldEnabled = 0x0C,
    // Responses
    StatusResponse = 0x81,
    ErrorResponse = 0x82,
    DeviceListResponse = 0x83,
    HrtfCatalogResponse = 0x84,
};

// The active spatialization DSP path. Off is a static-gain downmix
// (PassthroughStage); Basic is algebraic ambisonics decode
// (AmbisonicsStage); Advanced is HRTF-based binaural decode
// (BinauralStage). All three sit behind the same DspStage interface, so
// switching modes is just changing which stage AudioEngine dispatches to.
enum class SpatialMode : uint8
{
    Off = 0,
    Basic = 1,
    Advanced = 2,
};

struct MessageHeader
{
    Opcode MessageOpcode;
    uint16 PayloadLength;
};

inline constexpr size_t HeaderSize = 3;
inline constexpr size_t MaxPayloadSize = 4096;

// Number of virtual 7.1 speakers whose azimuth is controllable; mirrors
// AmbisonicsStage::SpeakerChannel's non-LFE channel order (FL,FR,FC,RL,RR,SL,SR).
inline constexpr size_t SpeakerCount = 7;

// Per-speaker distance range, in meters, shared by SpeakerLayout (daemon,
// which clamps to this range) and the GUI's position dial (which maps it
// to a pixel radius) - only meaningful when near-field mode is enabled
// (see SetNearFieldEnabled), but the value itself is always settable so a
// speaker can be positioned before switching that on. Floor is kept well
// above the ~0.0875m physical head radius used by the near-field model
// (daemon/src/dsp/synthetic_hrtf.cpp) so that model stays numerically
// well-behaved; ceiling is where the near-field/loudness-falloff effect
// becomes negligible.
inline constexpr float MinSpeakerDistanceMeters = 0.3f;
inline constexpr float MaxSpeakerDistanceMeters = 3.0f;
inline constexpr float DefaultSpeakerDistanceMeters = 1.5f;

// Distance at which near-field mode's loudness falloff and ILD correction
// are both exactly a no-op (1.0 gain) - roughly where HRTF measurements
// are typically made, and shared between AudioEngine's loudness falloff
// and near_field_filter.cpp's ILD shelf so both agree on what "no
// correction" means.
inline constexpr float ReferenceSpeakerDistanceMeters = 1.0f;

// Command decoded from a request message.
struct Command
{
    Opcode CommandOpcode = Opcode::GetStatus;
    SpatialMode ModeValue = SpatialMode::Off; // valid when CommandOpcode == SetSpatialMode
    uint8 SpeakerIndex = 0;     // valid when CommandOpcode == SetSpeakerAzimuth, SetSpeakerMute, or SetSpeakerDistance, 0..SpeakerCount-1
    float AzimuthDegrees = 0.0f;  // valid when CommandOpcode == SetSpeakerAzimuth
    bool bMuted = false;          // valid when CommandOpcode == SetSpeakerMute
    bool bTestNoiseEnabled = false; // valid when CommandOpcode == SetTestNoise
    std::string OutputDeviceName; // valid when CommandOpcode == SetOutputDevice
    uint8 HrtfIndex = 0;         // valid when CommandOpcode == SetHrtfFile
    float DistanceMeters = 0.0f;  // valid when CommandOpcode == SetSpeakerDistance
    bool bNearFieldEnabled = false; // valid when CommandOpcode == SetNearFieldEnabled
};

struct Status
{
    SpatialMode Mode = SpatialMode::Off;
    std::array<float, SpeakerCount> SpeakerAzimuthDegrees{};
    std::array<bool, SpeakerCount> SpeakerMuted{};
    bool bTestNoiseEnabled = false;
    std::string OutputDeviceName; // node.name of the currently pinned hardware output sink
    uint8 ActiveHrtfIndex = 0;  // index into the GetHrtfCatalog list Advanced mode is rendering through
    std::array<float, SpeakerCount> SpeakerDistanceMeters{};
    bool bNearFieldEnabled = false; // whether distance affects loudness (all modes) and ILD (Advanced mode)
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
std::optional<MessageHeader> TryReadHeader(const uint8* Buffer, size_t Available);

// Decodes a request payload for the given opcode. `Payload` must point at
// exactly `PayloadLength` valid bytes. Returns nullopt if the opcode isn't
// a request or the payload doesn't match what that opcode expects.
std::optional<Command> DecodeCommand(Opcode InOpcode, const uint8* Payload, uint16 PayloadLength);

// Decodes a StatusResponse payload. Used by control clients (GUI, test
// tools) to parse what the daemon sends back; the daemon only encodes
// responses, it doesn't decode them. Returns nullopt if PayloadLength
// doesn't match the expected `1 + 4*SpeakerCount` layout.
std::optional<Status> DecodeStatusResponse(const uint8* Payload, uint16 PayloadLength);

// Decodes an ErrorResponse payload into its message string.
std::string DecodeErrorResponse(const uint8* Payload, uint16 PayloadLength);

// Decodes a DeviceListResponse payload. Used by control clients (GUI, test
// tools) to parse what the daemon sends back. Returns nullopt if the
// payload is truncated or malformed.
std::optional<std::vector<AudioDeviceInfo>> DecodeDeviceListResponse(const uint8* Payload,
                                                                       uint16 PayloadLength);

// Decodes an HrtfCatalogResponse payload into the ordered list of display
// names; an entry's index into this list is what SetHrtfFile's HrtfIndex
// and Status's ActiveHrtfIndex refer to. Returns nullopt if the payload is
// truncated or malformed.
std::optional<std::vector<std::string>> DecodeHrtfCatalogResponse(const uint8* Payload,
                                                                     uint16 PayloadLength);

// Encodes a full status response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8> EncodeStatusResponse(const Status& InStatus);

// Encodes a full error response message (header + payload), ready to
// write directly to the socket.
std::vector<uint8> EncodeErrorResponse(const std::string& Message);

// Encodes a full device list response message (header + payload), ready to
// write directly to the socket. Entries that would push the payload past
// MaxPayloadSize are dropped from the end rather than truncated mid-entry.
std::vector<uint8> EncodeDeviceListResponse(const std::vector<AudioDeviceInfo>& Devices);

// Encodes a full HRTF catalog response message (header + payload), ready
// to write directly to the socket. Same truncate-whole-entries behavior
// as EncodeDeviceListResponse if the list would exceed MaxPayloadSize.
std::vector<uint8> EncodeHrtfCatalogResponse(const std::vector<std::string>& DisplayNames);

// Encodes a full request message. Used by control clients (GUI, test
// tools); the daemon only decodes requests, it doesn't encode them.
std::vector<uint8> EncodeGetStatusRequest();
std::vector<uint8> EncodeSetSpatialModeRequest(SpatialMode Mode);
std::vector<uint8> EncodeSetSpeakerAzimuthRequest(uint8 SpeakerIndex, float AzimuthDegrees);
std::vector<uint8> EncodeGetDevicesRequest();
std::vector<uint8> EncodeSetOutputDeviceRequest(const std::string& DeviceName);
std::vector<uint8> EncodeResetSpeakerPositionsRequest();
std::vector<uint8> EncodeSetSpeakerMuteRequest(uint8 SpeakerIndex, bool bMuted);
std::vector<uint8> EncodeSetTestNoiseRequest(bool bEnabled);
std::vector<uint8> EncodeGetHrtfCatalogRequest();
std::vector<uint8> EncodeSetHrtfFileRequest(uint8 HrtfIndex);
std::vector<uint8> EncodeSetSpeakerDistanceRequest(uint8 SpeakerIndex, float DistanceMeters);
std::vector<uint8> EncodeSetNearFieldEnabledRequest(bool bEnabled);

// Resolves the default control socket path: $XDG_RUNTIME_DIR/audiobat/control.sock,
// falling back to /tmp/audiobat-<uid>/control.sock if XDG_RUNTIME_DIR isn't set.
// Shared between the daemon (binds it) and control clients like the GUI
// (connect to it), so it lives here rather than in daemon-only code.
std::string DefaultControlSocketPath();

} // namespace audiobat
