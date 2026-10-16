#pragma once

#include "DeviceModel.h"

#include <cstddef>
#include <string>

class VendorProtocol
{
public:
    static Bytes encode8To7(const Bytes& input);
    static Bytes decode7To8(const Bytes& input);
    static Bytes versionQueryPayload();
    static Bytes versionQuerySysEx();
    static VersionInfo parseVersionResponse(const Bytes& message);
    static VersionInfo parseVersionPayload(const Bytes& payload);
    static std::uint8_t checksum(const Bytes& body);
    static std::string hexString(const Bytes& bytes, std::size_t maximumCount = 0);
};
