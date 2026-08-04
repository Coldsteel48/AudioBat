// AudioBat
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioBat, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audiobat/protocol.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace audiobat
{

namespace
{

void AppendHeader(std::vector<uint8_t>& Out, Opcode InOpcode, uint16_t PayloadLength)
{
    Out.push_back(static_cast<uint8_t>(InOpcode));
    Out.push_back(static_cast<uint8_t>((PayloadLength >> 8) & 0xFF));
    Out.push_back(static_cast<uint8_t>(PayloadLength & 0xFF));
}

void AppendFloatBE(std::vector<uint8_t>& Out, float Value)
{
    uint32_t Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Out.push_back(static_cast<uint8_t>((Bits >> 24) & 0xFF));
    Out.push_back(static_cast<uint8_t>((Bits >> 16) & 0xFF));
    Out.push_back(static_cast<uint8_t>((Bits >> 8) & 0xFF));
    Out.push_back(static_cast<uint8_t>(Bits & 0xFF));
}

float ReadFloatBE(const uint8_t* Payload)
{
    const uint32_t Bits = (static_cast<uint32_t>(Payload[0]) << 24) |
                          (static_cast<uint32_t>(Payload[1]) << 16) |
                          (static_cast<uint32_t>(Payload[2]) << 8) |
                          static_cast<uint32_t>(Payload[3]);
    float Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

// Appends a length-prefixed (u16 BE) string. Used for fields embedded
// alongside fixed-size data (Status, DeviceListResponse) where a raw
// whole-payload string (like ErrorResponse's) wouldn't be unambiguous.
void AppendString(std::vector<uint8_t>& Out, const std::string& Value)
{
    const uint16_t Len = static_cast<uint16_t>(std::min(Value.size(), static_cast<size_t>(0xFFFF)));
    Out.push_back(static_cast<uint8_t>((Len >> 8) & 0xFF));
    Out.push_back(static_cast<uint8_t>(Len & 0xFF));
    Out.insert(Out.end(), Value.begin(), Value.begin() + Len);
}

// Reads a length-prefixed string starting at `Offset`, advancing it past
// the string on success. Returns false (leaving OutValue untouched) if
// PayloadLength doesn't have enough bytes for the length or the string.
bool ReadString(const uint8_t* Payload, uint16_t PayloadLength, size_t& Offset, std::string& OutValue)
{
    if (Offset + 2 > PayloadLength)
    {
        return false;
    }
    const uint16_t Len = static_cast<uint16_t>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;
    if (Offset + Len > PayloadLength)
    {
        return false;
    }
    OutValue.assign(reinterpret_cast<const char*>(Payload + Offset), Len);
    Offset += Len;
    return true;
}

} // namespace

std::optional<MessageHeader> TryReadHeader(const uint8_t* Buffer, size_t Available)
{
    if (Available < HeaderSize)
    {
        return std::nullopt;
    }
    MessageHeader Header;
    Header.MessageOpcode = static_cast<Opcode>(Buffer[0]);
    Header.PayloadLength = static_cast<uint16_t>((Buffer[1] << 8) | Buffer[2]);
    return Header;
}

std::optional<Command> DecodeCommand(Opcode InOpcode, const uint8_t* Payload, uint16_t PayloadLength)
{
    Command OutCommand;
    OutCommand.CommandOpcode = InOpcode;

    switch (InOpcode)
    {
    case Opcode::GetStatus:
        return OutCommand;
    case Opcode::SetSpatialMode:
        if (PayloadLength < 1 || Payload[0] > static_cast<uint8_t>(SpatialMode::Advanced))
        {
            return std::nullopt;
        }
        OutCommand.ModeValue = static_cast<SpatialMode>(Payload[0]);
        return OutCommand;
    case Opcode::SetSpeakerAzimuth:
        if (PayloadLength < 5 || Payload[0] >= SpeakerCount)
        {
            return std::nullopt;
        }
        OutCommand.SpeakerIndex = Payload[0];
        OutCommand.AzimuthDegrees = ReadFloatBE(Payload + 1);
        return OutCommand;
    case Opcode::GetDevices:
        return OutCommand;
    case Opcode::SetOutputDevice:
        // Raw whole-payload string, same convention as ErrorResponse: the
        // message envelope already carries the length, so no extra prefix
        // is needed here.
        OutCommand.OutputDeviceName = std::string(reinterpret_cast<const char*>(Payload), PayloadLength);
        return OutCommand;
    case Opcode::ResetSpeakerPositions:
        return OutCommand;
    case Opcode::SetSpeakerMute:
        if (PayloadLength < 2 || Payload[0] >= SpeakerCount)
        {
            return std::nullopt;
        }
        OutCommand.SpeakerIndex = Payload[0];
        OutCommand.bMuted = Payload[1] != 0;
        return OutCommand;
    case Opcode::SetTestNoise:
        if (PayloadLength < 1)
        {
            return std::nullopt;
        }
        OutCommand.bTestNoiseEnabled = Payload[0] != 0;
        return OutCommand;
    case Opcode::GetHrtfCatalog:
        return OutCommand;
    case Opcode::SetHrtfFile:
        if (PayloadLength < 1)
        {
            return std::nullopt;
        }
        OutCommand.HrtfIndex = Payload[0];
        return OutCommand;
    default:
        return std::nullopt;
    }
}

std::optional<Status> DecodeStatusResponse(const uint8_t* Payload, uint16_t PayloadLength)
{
    // Layout: [mode:1][azimuths: 4*SpeakerCount][muted: SpeakerCount][noise:1][activeHrtfIndex:1]
    //         [device: length-prefixed string]
    const uint16_t FixedSize = static_cast<uint16_t>(1 + 4 * SpeakerCount + SpeakerCount + 1 + 1);
    if (PayloadLength < FixedSize || Payload[0] > static_cast<uint8_t>(SpatialMode::Advanced))
    {
        return std::nullopt;
    }
    Status OutStatus;
    OutStatus.Mode = static_cast<SpatialMode>(Payload[0]);
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        OutStatus.SpeakerAzimuthDegrees[i] = ReadFloatBE(Payload + 1 + 4 * i);
    }
    const size_t MutedOffset = 1 + 4 * SpeakerCount;
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        OutStatus.SpeakerMuted[i] = Payload[MutedOffset + i] != 0;
    }
    OutStatus.bTestNoiseEnabled = Payload[MutedOffset + SpeakerCount] != 0;
    OutStatus.ActiveHrtfIndex = Payload[MutedOffset + SpeakerCount + 1];

    size_t Offset = FixedSize;
    if (!ReadString(Payload, PayloadLength, Offset, OutStatus.OutputDeviceName))
    {
        return std::nullopt;
    }
    return OutStatus;
}

std::string DecodeErrorResponse(const uint8_t* Payload, uint16_t PayloadLength)
{
    return std::string(reinterpret_cast<const char*>(Payload), PayloadLength);
}

std::optional<std::vector<AudioDeviceInfo>> DecodeDeviceListResponse(const uint8_t* Payload,
                                                                       uint16_t PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16_t Count = static_cast<uint16_t>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<AudioDeviceInfo> Devices;
    Devices.reserve(Count);
    for (uint16_t i = 0; i < Count; ++i)
    {
        AudioDeviceInfo Info;
        if (!ReadString(Payload, PayloadLength, Offset, Info.Name) ||
            !ReadString(Payload, PayloadLength, Offset, Info.Description))
        {
            return std::nullopt;
        }
        Devices.push_back(std::move(Info));
    }
    return Devices;
}

std::optional<std::vector<std::string>> DecodeHrtfCatalogResponse(const uint8_t* Payload,
                                                                    uint16_t PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16_t Count = static_cast<uint16_t>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<std::string> DisplayNames;
    DisplayNames.reserve(Count);
    for (uint16_t i = 0; i < Count; ++i)
    {
        std::string Name;
        if (!ReadString(Payload, PayloadLength, Offset, Name))
        {
            return std::nullopt;
        }
        DisplayNames.push_back(std::move(Name));
    }
    return DisplayNames;
}

std::vector<uint8_t> EncodeStatusResponse(const Status& InStatus)
{
    std::vector<uint8_t> Payload;
    Payload.push_back(static_cast<uint8_t>(InStatus.Mode));
    for (float Azimuth : InStatus.SpeakerAzimuthDegrees)
    {
        AppendFloatBE(Payload, Azimuth);
    }
    for (bool bMuted : InStatus.SpeakerMuted)
    {
        Payload.push_back(bMuted ? 1 : 0);
    }
    Payload.push_back(InStatus.bTestNoiseEnabled ? 1 : 0);
    Payload.push_back(InStatus.ActiveHrtfIndex);
    AppendString(Payload, InStatus.OutputDeviceName);

    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::StatusResponse, static_cast<uint16_t>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8_t> EncodeErrorResponse(const std::string& Message)
{
    std::vector<uint8_t> Out;
    const size_t Len = Message.size() > MaxPayloadSize ? MaxPayloadSize : Message.size();
    Out.reserve(HeaderSize + Len);
    AppendHeader(Out, Opcode::ErrorResponse, static_cast<uint16_t>(Len));
    Out.insert(Out.end(), Message.begin(), Message.begin() + static_cast<std::ptrdiff_t>(Len));
    return Out;
}

std::vector<uint8_t> EncodeDeviceListResponse(const std::vector<AudioDeviceInfo>& Devices)
{
    std::vector<uint8_t> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16_t Count = 0;
    for (const auto& Device : Devices)
    {
        std::vector<uint8_t> Entry;
        AppendString(Entry, Device.Name);
        AppendString(Entry, Device.Description);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8_t>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8_t>(Count & 0xFF);

    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::DeviceListResponse, static_cast<uint16_t>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8_t> EncodeHrtfCatalogResponse(const std::vector<std::string>& DisplayNames)
{
    std::vector<uint8_t> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16_t Count = 0;
    for (const auto& Name : DisplayNames)
    {
        std::vector<uint8_t> Entry;
        AppendString(Entry, Name);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8_t>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8_t>(Count & 0xFF);

    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::HrtfCatalogResponse, static_cast<uint16_t>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8_t> EncodeGetStatusRequest()
{
    std::vector<uint8_t> Out;
    AppendHeader(Out, Opcode::GetStatus, 0);
    return Out;
}

std::vector<uint8_t> EncodeSetSpatialModeRequest(SpatialMode Mode)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetSpatialMode, 1);
    Out.push_back(static_cast<uint8_t>(Mode));
    return Out;
}

std::vector<uint8_t> EncodeSetSpeakerAzimuthRequest(uint8_t SpeakerIndex, float AzimuthDegrees)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 5);
    AppendHeader(Out, Opcode::SetSpeakerAzimuth, 5);
    Out.push_back(SpeakerIndex);
    AppendFloatBE(Out, AzimuthDegrees);
    return Out;
}

std::vector<uint8_t> EncodeGetDevicesRequest()
{
    std::vector<uint8_t> Out;
    AppendHeader(Out, Opcode::GetDevices, 0);
    return Out;
}

std::vector<uint8_t> EncodeSetOutputDeviceRequest(const std::string& DeviceName)
{
    std::vector<uint8_t> Out;
    const size_t Len = DeviceName.size() > MaxPayloadSize ? MaxPayloadSize : DeviceName.size();
    Out.reserve(HeaderSize + Len);
    AppendHeader(Out, Opcode::SetOutputDevice, static_cast<uint16_t>(Len));
    Out.insert(Out.end(), DeviceName.begin(), DeviceName.begin() + static_cast<std::ptrdiff_t>(Len));
    return Out;
}

std::vector<uint8_t> EncodeResetSpeakerPositionsRequest()
{
    std::vector<uint8_t> Out;
    AppendHeader(Out, Opcode::ResetSpeakerPositions, 0);
    return Out;
}

std::vector<uint8_t> EncodeSetSpeakerMuteRequest(uint8_t SpeakerIndex, bool bMuted)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 2);
    AppendHeader(Out, Opcode::SetSpeakerMute, 2);
    Out.push_back(SpeakerIndex);
    Out.push_back(bMuted ? 1 : 0);
    return Out;
}

std::vector<uint8_t> EncodeSetTestNoiseRequest(bool bEnabled)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetTestNoise, 1);
    Out.push_back(bEnabled ? 1 : 0);
    return Out;
}

std::vector<uint8_t> EncodeGetHrtfCatalogRequest()
{
    std::vector<uint8_t> Out;
    AppendHeader(Out, Opcode::GetHrtfCatalog, 0);
    return Out;
}

std::vector<uint8_t> EncodeSetHrtfFileRequest(uint8_t HrtfIndex)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetHrtfFile, 1);
    Out.push_back(HrtfIndex);
    return Out;
}

std::string DefaultControlSocketPath()
{
    if (const char* RuntimeDir = std::getenv("XDG_RUNTIME_DIR"))
    {
        std::string Dir = std::string(RuntimeDir) + "/audiobat";
        mkdir(Dir.c_str(), 0700);
        return Dir + "/control.sock";
    }
    std::string Dir = "/tmp/audiobat-" + std::to_string(getuid());
    mkdir(Dir.c_str(), 0700);
    return Dir + "/control.sock";
}

} // namespace audiobat
