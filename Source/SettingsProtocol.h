#pragma once

#include "DeviceModel.h"

#include <cstdint>

class SettingsProtocol
{
public:
    static Bytes readPayload(std::uint8_t flashType = 5,
                             std::uint32_t address = 0,
                             std::uint32_t length = 6282);
    static Bytes readSysEx(std::uint8_t flashType = 5,
                           std::uint32_t address = 0,
                           std::uint32_t length = 6282);
    static Bytes writePayload(std::uint32_t address, const Bytes& data, std::uint8_t flashType = 5);
    static Bytes sysEx(const Bytes& payload);
    static Bytes writeSysEx(std::uint32_t address, const Bytes& data, std::uint8_t flashType = 5);
    static SettingsReadResult parseResponses(const std::vector<Bytes>& messages);
    static SettingsReadResult parsePayloads(const std::vector<Bytes>& payloads);
    static void parseWriteAcknowledgement(const std::vector<Bytes>& messages);
    static Bytes parseChunk(const std::vector<Bytes>& messages, std::uint32_t address, std::uint32_t length);
};
