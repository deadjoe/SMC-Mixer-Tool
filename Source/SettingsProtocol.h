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
    static SettingsReadResult parseResponses(const std::vector<Bytes>& messages);
    static SettingsReadResult parsePayloads(const std::vector<Bytes>& payloads);
};
