// RamkolFX
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of RamkolFX, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "ramkolfx/protocol.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ramkolfx
{

namespace
{

void AppendHeader(std::vector<uint8>& Out, Opcode InOpcode, uint16 PayloadLength)
{
    Out.push_back(static_cast<uint8>(InOpcode));
    Out.push_back(static_cast<uint8>((PayloadLength >> 8) & 0xFF));
    Out.push_back(static_cast<uint8>(PayloadLength & 0xFF));
}

void AppendFloatBE(std::vector<uint8>& Out, float Value)
{
    uint32 Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Out.push_back(static_cast<uint8>((Bits >> 24) & 0xFF));
    Out.push_back(static_cast<uint8>((Bits >> 16) & 0xFF));
    Out.push_back(static_cast<uint8>((Bits >> 8) & 0xFF));
    Out.push_back(static_cast<uint8>(Bits & 0xFF));
}

float ReadFloatBE(const uint8* Payload)
{
    const uint32 Bits = (static_cast<uint32>(Payload[0]) << 24) |
                          (static_cast<uint32>(Payload[1]) << 16) |
                          (static_cast<uint32>(Payload[2]) << 8) |
                          static_cast<uint32>(Payload[3]);
    float Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

void AppendEqBand(std::vector<uint8>& Out, const EqBand& Band)
{
    Out.push_back(static_cast<uint8>(Band.FilterType));
    AppendFloatBE(Out, Band.FrequencyHz);
    AppendFloatBE(Out, Band.GainDb);
    AppendFloatBE(Out, Band.Q);
}

// Reads a fixed EqBandWireSize-byte EqBand from `Payload`. Caller must have
// already verified that many bytes are available.
EqBand ReadEqBand(const uint8* Payload)
{
    EqBand Band;
    Band.FilterType = static_cast<EqFilterType>(Payload[0]);
    Band.FrequencyHz = ReadFloatBE(Payload + 1);
    Band.GainDb = ReadFloatBE(Payload + 5);
    Band.Q = ReadFloatBE(Payload + 9);
    return Band;
}

// Appends a length-prefixed (u16 BE) string. Used for fields embedded
// alongside fixed-size data (Status, DeviceListResponse) where a raw
// whole-payload string (like ErrorResponse's) wouldn't be unambiguous.
void AppendString(std::vector<uint8>& Out, const std::string& Value)
{
    const uint16 Len = static_cast<uint16>(std::min(Value.size(), static_cast<size_t>(0xFFFF)));
    Out.push_back(static_cast<uint8>((Len >> 8) & 0xFF));
    Out.push_back(static_cast<uint8>(Len & 0xFF));
    Out.insert(Out.end(), Value.begin(), Value.begin() + Len);
}

// Reads a length-prefixed string starting at `Offset`, advancing it past
// the string on success. Returns false (leaving OutValue untouched) if
// PayloadLength doesn't have enough bytes for the length or the string.
bool ReadString(const uint8* Payload, uint16 PayloadLength, size_t& Offset, std::string& OutValue)
{
    if (Offset + 2 > PayloadLength)
    {
        return false;
    }
    const uint16 Len = static_cast<uint16>((Payload[Offset] << 8) | Payload[Offset + 1]);
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

std::optional<MessageHeader> TryReadHeader(const uint8* Buffer, size_t Available)
{
    if (Available < HeaderSize)
    {
        return std::nullopt;
    }
    MessageHeader Header;
    Header.MessageOpcode = static_cast<Opcode>(Buffer[0]);
    Header.PayloadLength = static_cast<uint16>((Buffer[1] << 8) | Buffer[2]);
    return Header;
}

std::optional<Command> DecodeCommand(Opcode InOpcode, const uint8* Payload, uint16 PayloadLength)
{
    Command OutCommand;
    OutCommand.CommandOpcode = InOpcode;

    switch (InOpcode)
    {
    case Opcode::GetStatus:
        return OutCommand;
    case Opcode::SetSpatialMode:
        if (PayloadLength < 1 || Payload[0] > static_cast<uint8>(SpatialMode::Advanced))
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
    case Opcode::SetSpeakerDistance:
        if (PayloadLength < 5 || Payload[0] >= SpeakerCount)
        {
            return std::nullopt;
        }
        OutCommand.SpeakerIndex = Payload[0];
        OutCommand.DistanceMeters = ReadFloatBE(Payload + 1);
        return OutCommand;
    case Opcode::SetNearFieldEnabled:
        if (PayloadLength < 1)
        {
            return std::nullopt;
        }
        OutCommand.bNearFieldEnabled = Payload[0] != 0;
        return OutCommand;
    case Opcode::SetHwEqBand:
    {
        const uint16 Needed = static_cast<uint16>(1 + EqBandWireSize);
        if (PayloadLength < Needed || Payload[0] >= MaxEqBands ||
            Payload[1] > static_cast<uint8>(EqFilterType::Notch))
        {
            return std::nullopt;
        }
        OutCommand.BandIndex = Payload[0];
        OutCommand.Band = ReadEqBand(Payload + 1);
        return OutCommand;
    }
    case Opcode::SetHwEqPreset:
        if (PayloadLength < 1)
        {
            return std::nullopt;
        }
        OutCommand.EqPresetIndex = Payload[0];
        return OutCommand;
    case Opcode::SaveHwEqPreset:
        OutCommand.PresetName = std::string(reinterpret_cast<const char*>(Payload), PayloadLength);
        return OutCommand;
    case Opcode::GetHwEqState:
        return OutCommand;
    case Opcode::GetContentStreams:
        return OutCommand;
    case Opcode::SetContentEqBand:
    {
        size_t Offset = 0;
        if (!ReadString(Payload, PayloadLength, Offset, OutCommand.ContentAppName))
        {
            return std::nullopt;
        }
        const size_t Needed = 1 + EqBandWireSize;
        if (Offset + Needed > PayloadLength || Payload[Offset] >= MaxEqBands ||
            Payload[Offset + 1] > static_cast<uint8>(EqFilterType::Notch))
        {
            return std::nullopt;
        }
        OutCommand.BandIndex = Payload[Offset];
        OutCommand.Band = ReadEqBand(Payload + Offset + 1);
        return OutCommand;
    }
    case Opcode::SetContentEqPreset:
    {
        size_t Offset = 0;
        if (!ReadString(Payload, PayloadLength, Offset, OutCommand.ContentAppName))
        {
            return std::nullopt;
        }
        if (Offset + 1 > PayloadLength)
        {
            return std::nullopt;
        }
        OutCommand.EqPresetIndex = Payload[Offset];
        return OutCommand;
    }
    case Opcode::SaveContentEqPreset:
    {
        size_t Offset = 0;
        if (!ReadString(Payload, PayloadLength, Offset, OutCommand.ContentAppName))
        {
            return std::nullopt;
        }
        OutCommand.PresetName =
            std::string(reinterpret_cast<const char*>(Payload + Offset), PayloadLength - Offset);
        return OutCommand;
    }
    case Opcode::GetEqPresetCatalog:
        return OutCommand;
    default:
        return std::nullopt;
    }
}

std::optional<Status> DecodeStatusResponse(const uint8* Payload, uint16 PayloadLength)
{
    // Layout: [mode:1][azimuths: 4*SpeakerCount][muted: SpeakerCount][noise:1][activeHrtfIndex:1]
    //         [distances: 4*SpeakerCount][nearFieldEnabled:1][device: length-prefixed string]
    const uint16 FixedSize =
        static_cast<uint16>(1 + 4 * SpeakerCount + SpeakerCount + 1 + 1 + 4 * SpeakerCount + 1);
    if (PayloadLength < FixedSize || Payload[0] > static_cast<uint8>(SpatialMode::Advanced))
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

    const size_t DistanceOffset = MutedOffset + SpeakerCount + 2;
    for (size_t i = 0; i < SpeakerCount; ++i)
    {
        OutStatus.SpeakerDistanceMeters[i] = ReadFloatBE(Payload + DistanceOffset + 4 * i);
    }
    OutStatus.bNearFieldEnabled = Payload[DistanceOffset + 4 * SpeakerCount] != 0;

    size_t Offset = FixedSize;
    if (!ReadString(Payload, PayloadLength, Offset, OutStatus.OutputDeviceName))
    {
        return std::nullopt;
    }
    return OutStatus;
}

std::string DecodeErrorResponse(const uint8* Payload, uint16 PayloadLength)
{
    return std::string(reinterpret_cast<const char*>(Payload), PayloadLength);
}

std::optional<std::vector<AudioDeviceInfo>> DecodeDeviceListResponse(const uint8* Payload,
                                                                       uint16 PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16 Count = static_cast<uint16>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<AudioDeviceInfo> Devices;
    Devices.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
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

std::optional<std::vector<std::string>> DecodeHrtfCatalogResponse(const uint8* Payload,
                                                                    uint16 PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16 Count = static_cast<uint16>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<std::string> DisplayNames;
    DisplayNames.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
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

std::optional<std::array<EqBand, MaxEqBands>> DecodeHwEqStateResponse(const uint8* Payload,
                                                                        uint16 PayloadLength)
{
    if (PayloadLength != static_cast<uint16>(MaxEqBands * EqBandWireSize))
    {
        return std::nullopt;
    }
    std::array<EqBand, MaxEqBands> Bands;
    for (size_t i = 0; i < MaxEqBands; ++i)
    {
        Bands[i] = ReadEqBand(Payload + i * EqBandWireSize);
    }
    return Bands;
}

std::optional<std::vector<ContentStreamInfo>> DecodeContentStreamListResponse(const uint8* Payload,
                                                                                 uint16 PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16 Count = static_cast<uint16>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<ContentStreamInfo> Streams;
    Streams.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
    {
        ContentStreamInfo Info;
        if (!ReadString(Payload, PayloadLength, Offset, Info.AppName) ||
            !ReadString(Payload, PayloadLength, Offset, Info.MediaName))
        {
            return std::nullopt;
        }
        Streams.push_back(std::move(Info));
    }
    return Streams;
}

std::optional<std::vector<std::string>> DecodeEqPresetCatalogResponse(const uint8* Payload,
                                                                         uint16 PayloadLength)
{
    size_t Offset = 0;
    if (Offset + 2 > PayloadLength)
    {
        return std::nullopt;
    }
    const uint16 Count = static_cast<uint16>((Payload[Offset] << 8) | Payload[Offset + 1]);
    Offset += 2;

    std::vector<std::string> DisplayNames;
    DisplayNames.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
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

std::vector<uint8> EncodeStatusResponse(const Status& InStatus)
{
    std::vector<uint8> Payload;
    Payload.push_back(static_cast<uint8>(InStatus.Mode));
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
    for (float Distance : InStatus.SpeakerDistanceMeters)
    {
        AppendFloatBE(Payload, Distance);
    }
    Payload.push_back(InStatus.bNearFieldEnabled ? 1 : 0);
    AppendString(Payload, InStatus.OutputDeviceName);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::StatusResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeErrorResponse(const std::string& Message)
{
    std::vector<uint8> Out;
    const size_t Len = Message.size() > MaxPayloadSize ? MaxPayloadSize : Message.size();
    Out.reserve(HeaderSize + Len);
    AppendHeader(Out, Opcode::ErrorResponse, static_cast<uint16>(Len));
    Out.insert(Out.end(), Message.begin(), Message.begin() + static_cast<std::ptrdiff_t>(Len));
    return Out;
}

std::vector<uint8> EncodeDeviceListResponse(const std::vector<AudioDeviceInfo>& Devices)
{
    std::vector<uint8> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16 Count = 0;
    for (const auto& Device : Devices)
    {
        std::vector<uint8> Entry;
        AppendString(Entry, Device.Name);
        AppendString(Entry, Device.Description);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8>(Count & 0xFF);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::DeviceListResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeHrtfCatalogResponse(const std::vector<std::string>& DisplayNames)
{
    std::vector<uint8> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16 Count = 0;
    for (const auto& Name : DisplayNames)
    {
        std::vector<uint8> Entry;
        AppendString(Entry, Name);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8>(Count & 0xFF);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::HrtfCatalogResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeHwEqStateResponse(const std::array<EqBand, MaxEqBands>& Bands)
{
    std::vector<uint8> Payload;
    Payload.reserve(MaxEqBands * EqBandWireSize);
    for (const EqBand& Band : Bands)
    {
        AppendEqBand(Payload, Band);
    }

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::HwEqStateResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeContentStreamListResponse(const std::vector<ContentStreamInfo>& Streams)
{
    std::vector<uint8> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16 Count = 0;
    for (const auto& Stream : Streams)
    {
        std::vector<uint8> Entry;
        AppendString(Entry, Stream.AppName);
        AppendString(Entry, Stream.MediaName);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8>(Count & 0xFF);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::ContentStreamListResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeEqPresetCatalogResponse(const std::vector<std::string>& DisplayNames)
{
    std::vector<uint8> Payload;
    Payload.push_back(0); // count placeholder, patched below once the final count is known
    Payload.push_back(0);

    uint16 Count = 0;
    for (const auto& Name : DisplayNames)
    {
        std::vector<uint8> Entry;
        AppendString(Entry, Name);
        if (Payload.size() + Entry.size() > MaxPayloadSize)
        {
            break; // stop before exceeding the payload cap, never mid-entry
        }
        Payload.insert(Payload.end(), Entry.begin(), Entry.end());
        ++Count;
    }
    Payload[0] = static_cast<uint8>((Count >> 8) & 0xFF);
    Payload[1] = static_cast<uint8>(Count & 0xFF);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::EqPresetCatalogResponse, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeGetStatusRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetStatus, 0);
    return Out;
}

std::vector<uint8> EncodeSetSpatialModeRequest(SpatialMode Mode)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetSpatialMode, 1);
    Out.push_back(static_cast<uint8>(Mode));
    return Out;
}

std::vector<uint8> EncodeSetSpeakerAzimuthRequest(uint8 SpeakerIndex, float AzimuthDegrees)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 5);
    AppendHeader(Out, Opcode::SetSpeakerAzimuth, 5);
    Out.push_back(SpeakerIndex);
    AppendFloatBE(Out, AzimuthDegrees);
    return Out;
}

std::vector<uint8> EncodeGetDevicesRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetDevices, 0);
    return Out;
}

std::vector<uint8> EncodeSetOutputDeviceRequest(const std::string& DeviceName)
{
    std::vector<uint8> Out;
    const size_t Len = DeviceName.size() > MaxPayloadSize ? MaxPayloadSize : DeviceName.size();
    Out.reserve(HeaderSize + Len);
    AppendHeader(Out, Opcode::SetOutputDevice, static_cast<uint16>(Len));
    Out.insert(Out.end(), DeviceName.begin(), DeviceName.begin() + static_cast<std::ptrdiff_t>(Len));
    return Out;
}

std::vector<uint8> EncodeResetSpeakerPositionsRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::ResetSpeakerPositions, 0);
    return Out;
}

std::vector<uint8> EncodeSetSpeakerMuteRequest(uint8 SpeakerIndex, bool bMuted)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 2);
    AppendHeader(Out, Opcode::SetSpeakerMute, 2);
    Out.push_back(SpeakerIndex);
    Out.push_back(bMuted ? 1 : 0);
    return Out;
}

std::vector<uint8> EncodeSetTestNoiseRequest(bool bEnabled)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetTestNoise, 1);
    Out.push_back(bEnabled ? 1 : 0);
    return Out;
}

std::vector<uint8> EncodeGetHrtfCatalogRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetHrtfCatalog, 0);
    return Out;
}

std::vector<uint8> EncodeSetHrtfFileRequest(uint8 HrtfIndex)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetHrtfFile, 1);
    Out.push_back(HrtfIndex);
    return Out;
}

std::vector<uint8> EncodeSetSpeakerDistanceRequest(uint8 SpeakerIndex, float DistanceMeters)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 5);
    AppendHeader(Out, Opcode::SetSpeakerDistance, 5);
    Out.push_back(SpeakerIndex);
    AppendFloatBE(Out, DistanceMeters);
    return Out;
}

std::vector<uint8> EncodeSetNearFieldEnabledRequest(bool bEnabled)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetNearFieldEnabled, 1);
    Out.push_back(bEnabled ? 1 : 0);
    return Out;
}

std::vector<uint8> EncodeSetHwEqBandRequest(uint8 BandIndex, const EqBand& Band)
{
    std::vector<uint8> Out;
    const size_t PayloadSize = 1 + EqBandWireSize;
    Out.reserve(HeaderSize + PayloadSize);
    AppendHeader(Out, Opcode::SetHwEqBand, static_cast<uint16>(PayloadSize));
    Out.push_back(BandIndex);
    AppendEqBand(Out, Band);
    return Out;
}

std::vector<uint8> EncodeSetHwEqPresetRequest(uint8 EqPresetIndex)
{
    std::vector<uint8> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetHwEqPreset, 1);
    Out.push_back(EqPresetIndex);
    return Out;
}

std::vector<uint8> EncodeSaveHwEqPresetRequest(const std::string& PresetName)
{
    std::vector<uint8> Out;
    const size_t Len = PresetName.size() > MaxPayloadSize ? MaxPayloadSize : PresetName.size();
    Out.reserve(HeaderSize + Len);
    AppendHeader(Out, Opcode::SaveHwEqPreset, static_cast<uint16>(Len));
    Out.insert(Out.end(), PresetName.begin(), PresetName.begin() + static_cast<std::ptrdiff_t>(Len));
    return Out;
}

std::vector<uint8> EncodeGetHwEqStateRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetHwEqState, 0);
    return Out;
}

std::vector<uint8> EncodeGetContentStreamsRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetContentStreams, 0);
    return Out;
}

std::vector<uint8> EncodeSetContentEqBandRequest(const std::string& AppName, uint8 BandIndex,
                                                  const EqBand& Band)
{
    std::vector<uint8> Payload;
    AppendString(Payload, AppName);
    Payload.push_back(BandIndex);
    AppendEqBand(Payload, Band);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::SetContentEqBand, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeSetContentEqPresetRequest(const std::string& AppName, uint8 EqPresetIndex)
{
    std::vector<uint8> Payload;
    AppendString(Payload, AppName);
    Payload.push_back(EqPresetIndex);

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::SetContentEqPreset, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeSaveContentEqPresetRequest(const std::string& AppName,
                                                     const std::string& PresetName)
{
    std::vector<uint8> Payload;
    AppendString(Payload, AppName);
    const size_t Available = Payload.size() < MaxPayloadSize ? MaxPayloadSize - Payload.size() : 0;
    const size_t Len = PresetName.size() > Available ? Available : PresetName.size();
    Payload.insert(Payload.end(), PresetName.begin(), PresetName.begin() + static_cast<std::ptrdiff_t>(Len));

    std::vector<uint8> Out;
    Out.reserve(HeaderSize + Payload.size());
    AppendHeader(Out, Opcode::SaveContentEqPreset, static_cast<uint16>(Payload.size()));
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    return Out;
}

std::vector<uint8> EncodeGetEqPresetCatalogRequest()
{
    std::vector<uint8> Out;
    AppendHeader(Out, Opcode::GetEqPresetCatalog, 0);
    return Out;
}

std::string DefaultControlSocketPath()
{
    if (const char* RuntimeDir = std::getenv("XDG_RUNTIME_DIR"))
    {
        std::string Dir = std::string(RuntimeDir) + "/ramkolfx";
        mkdir(Dir.c_str(), 0700);
        return Dir + "/control.sock";
    }
    std::string Dir = "/tmp/ramkolfx-" + std::to_string(getuid());
    mkdir(Dir.c_str(), 0700);
    return Dir + "/control.sock";
}

} // namespace ramkolfx
