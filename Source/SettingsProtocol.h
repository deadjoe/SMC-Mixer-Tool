#pragma once

#include "DeviceModel.h"
#include "SettingsLayout.h"

#include <cstdint>
#include <optional>
#include <vector>

// What the mixer said about a write or save: nothing yet, yes, or no with a status byte.
struct WriteAcknowledgement
{
    enum class State
    {
        missing,
        accepted,
        rejected
    };

    State state = State::missing;
    std::uint8_t status = 0;
};

// Vendor payloads for the settings image. USB-C wraps each one with VendorProtocol::sysEx().
// Bluetooth sends the payload bytes unchanged.
class SettingsProtocol
{
public:
    static Bytes readPayload(std::uint32_t address = 0, std::uint32_t length = layout::snapshotSize);
    static Bytes writePayload(std::uint32_t address, const Bytes& data);
    static Bytes savePayload();

    // Assembles a full read from every reply. Throws when a chunk is missing, conflicts, or is malformed.
    static SettingsReadResult parseResponses(const std::vector<Bytes>& messages);
    static SettingsReadResult parsePayloads(const std::vector<Bytes>& payloads);

    // The reply to a write or save. Throws ProtocolError only for a malformed acknowledgement.
    static WriteAcknowledgement acknowledgement(const std::vector<Bytes>& messages);
    // The data of the chunk at exactly this address and length, or nothing if it has not arrived.
    // Throws ProtocolError when two replies disagree or a reply is malformed.
    static std::optional<Bytes> findChunk(const std::vector<Bytes>& messages, std::uint32_t address, std::uint32_t length);
};
