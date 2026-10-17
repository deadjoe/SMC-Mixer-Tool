#pragma once

#include "DeviceModel.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// A reply that claims to be a vendor frame but has a bad length or checksum.
class ProtocolError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// One vendor frame: 00 59, a command byte, a 24-bit body length, the body, then a checksum.
struct VendorFrame
{
    std::uint8_t command = 0;
    Bytes body;
};

class VendorProtocol
{
public:
    static constexpr std::size_t headerSize = 6;

    static Bytes encode8To7(const Bytes& input);
    static Bytes decode7To8(const Bytes& input);

    // Builds a complete payload around a body.
    static Bytes frame(std::uint8_t command, const Bytes& body);
    // Returns the frame in a payload, or nothing when the payload is not a vendor frame.
    // Throws ProtocolError when it is one but its length or checksum is wrong.
    static std::optional<VendorFrame> parseFrame(const Bytes& payload);

    // USB-C carries a payload as F0, the payload packed into 7-bit bytes, F7.
    static Bytes sysEx(const Bytes& payload);
    static std::optional<Bytes> unwrapSysEx(const Bytes& message);
    // Payloads from USB-C SysEx and from raw Bluetooth replies alike. Other messages are skipped.
    static std::vector<Bytes> vendorPayloads(const std::vector<Bytes>& messages);

    // Splits a payload into writes no longer than a Bluetooth link allows, in order. The
    // mixer reads a frame's length from its header, so the pieces arrive as one frame.
    static std::vector<Bytes> fragments(const Bytes& payload, std::size_t maximumWrite);

    static Bytes versionQueryPayload();
    static VersionInfo parseVersionResponse(const Bytes& message);
    static VersionInfo parseVersionPayload(const Bytes& payload);

    static std::uint8_t checksum(const Bytes& body);
    static std::uint32_t readLE(const Bytes& bytes, std::size_t offset, std::size_t count);
    static void appendLE(Bytes& bytes, std::uint32_t value, std::size_t count);
    static std::string hexString(const Bytes& bytes, std::size_t maximumCount = 0);
};
