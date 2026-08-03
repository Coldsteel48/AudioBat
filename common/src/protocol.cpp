// AudioDock
// Copyright (C) 2026 Roman Levin (Coldsteel48)
//
// This file is part of AudioDock, dual-licensed under the GNU General
// Public License v3.0 (see LICENSE) or a separate commercial license
// (see LICENSE-COMMERCIAL.md). Contributions are accepted only under the
// terms of the Contributor License Agreement (see CLA.md).

#include "audiodock/protocol.hpp"

#include <cstring>

namespace audiodock
{

namespace
{

void AppendHeader(std::vector<uint8_t>& Out, Opcode InOpcode, uint16_t PayloadLength)
{
    Out.push_back(static_cast<uint8_t>(InOpcode));
    Out.push_back(static_cast<uint8_t>((PayloadLength >> 8) & 0xFF));
    Out.push_back(static_cast<uint8_t>(PayloadLength & 0xFF));
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
    case Opcode::SetThreeDEnabled:
        if (PayloadLength < 1)
        {
            return std::nullopt;
        }
        OutCommand.bEnabledValue = Payload[0] != 0;
        return OutCommand;
    default:
        return std::nullopt;
    }
}

std::vector<uint8_t> EncodeStatusResponse(const Status& InStatus)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::StatusResponse, 1);
    Out.push_back(InStatus.bThreeDEnabled ? 1 : 0);
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

std::vector<uint8_t> EncodeGetStatusRequest()
{
    std::vector<uint8_t> Out;
    AppendHeader(Out, Opcode::GetStatus, 0);
    return Out;
}

std::vector<uint8_t> EncodeSetThreeDEnabledRequest(bool bEnabled)
{
    std::vector<uint8_t> Out;
    Out.reserve(HeaderSize + 1);
    AppendHeader(Out, Opcode::SetThreeDEnabled, 1);
    Out.push_back(bEnabled ? 1 : 0);
    return Out;
}

} // namespace audiodock
