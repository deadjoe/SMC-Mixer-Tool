#include "SettingsProtocol.h"

#include "VendorProtocol.h"

#include <map>
#include <string>

namespace
{
constexpr std::uint8_t writeCommand = 0x22;
constexpr std::uint8_t readCommand = 0x23;
constexpr std::uint8_t acknowledgementCommand = 0x00;
constexpr std::size_t chunkHeaderSize = 8; // flash type, 32-bit address, 24-bit length

// Flash type, address, and length: the start of every read, write, and chunk body.
Bytes recordHeader(std::uint32_t address, std::uint32_t length)
{
    Bytes body { layout::settingsFlash };
    VendorProtocol::appendLE(body, address, 4);
    VendorProtocol::appendLE(body, length, 3);
    return body;
}

struct Chunk
{
    std::uint32_t address = 0;
    Bytes data;
};

// A settings chunk is a 0x23 frame whose body starts with the flash type. Other 0x23 bodies
// are acknowledgements of the read request.
std::optional<Chunk> chunkIn(const VendorFrame& frame)
{
    if (frame.command != readCommand || frame.body.empty() || frame.body.front() != layout::settingsFlash)
        return std::nullopt;
    if (frame.body.size() < chunkHeaderSize)
        throw ProtocolError("Settings chunk header is too short");

    Chunk chunk;
    chunk.address = VendorProtocol::readLE(frame.body, 1, 4);
    const auto length = VendorProtocol::readLE(frame.body, 5, 3);
    if (frame.body.size() != chunkHeaderSize + length)
        throw ProtocolError("Settings chunk length mismatch at address " + std::to_string(chunk.address));
    chunk.data.assign(frame.body.begin() + static_cast<std::ptrdiff_t>(chunkHeaderSize), frame.body.end());
    return chunk;
}
}

Bytes SettingsProtocol::readPayload(std::uint32_t address, std::uint32_t length)
{
    return VendorProtocol::frame(readCommand, recordHeader(address, length));
}

Bytes SettingsProtocol::writePayload(std::uint32_t address, const Bytes& data)
{
    // The mixer acknowledges a write at once, but the bytes stay in RAM until savePayload().
    if (data.empty() || data.size() > layout::maximumWriteSize)
        throw ProtocolError("Write length is invalid");
    auto body = recordHeader(address, static_cast<std::uint32_t>(data.size()));
    body.insert(body.end(), data.begin(), data.end());
    return VendorProtocol::frame(writeCommand, body);
}

Bytes SettingsProtocol::savePayload()
{
    // A write with address 0 and no data. Midi Suite sends this after its writes.
    // The mixer then commits RAM to flash and acknowledges after about 60 ms.
    return VendorProtocol::frame(writeCommand, recordHeader(0, 0));
}

SettingsReadResult SettingsProtocol::parseResponses(const std::vector<Bytes>& messages)
{
    return parsePayloads(VendorProtocol::vendorPayloads(messages));
}

SettingsReadResult SettingsProtocol::parsePayloads(const std::vector<Bytes>& payloads)
{
    std::map<std::uint32_t, Bytes> chunks;
    int acknowledgementCount = 0;

    for (const auto& payload : payloads)
    {
        const auto frame = VendorProtocol::parseFrame(payload);
        if (!frame || frame->command != readCommand)
            continue;
        auto chunk = chunkIn(*frame);
        if (!chunk)
        {
            ++acknowledgementCount;
            continue;
        }
        const auto existing = chunks.find(chunk->address);
        if (existing != chunks.end() && existing->second != chunk->data)
            throw ProtocolError("Conflicting settings chunk at address " + std::to_string(chunk->address));
        chunks[chunk->address] = std::move(chunk->data);
    }

    SettingsReadResult result;
    std::uint32_t expectedAddress = 0;
    for (const auto& [address, data] : chunks)
    {
        if (address != expectedAddress)
            throw ProtocolError("Settings data is missing at address " + std::to_string(expectedAddress)
                                + ". The next chunk is at " + std::to_string(address));
        result.data.insert(result.data.end(), data.begin(), data.end());
        expectedAddress += static_cast<std::uint32_t>(data.size());
    }

    if (result.data.empty())
        throw ProtocolError("No settings data chunk was received");

    result.chunkCount = static_cast<int>(chunks.size());
    result.acknowledgementCount = acknowledgementCount;
    return result;
}

WriteAcknowledgement SettingsProtocol::acknowledgement(const std::vector<Bytes>& messages)
{
    WriteAcknowledgement result;
    for (const auto& payload : VendorProtocol::vendorPayloads(messages))
    {
        const auto frame = VendorProtocol::parseFrame(payload);
        if (!frame || frame->command != acknowledgementCommand)
            continue;
        if (frame->body.size() != 1)
            throw ProtocolError("Write acknowledgement has an unexpected length");
        if (frame->body.front() != 0x00)
            return { WriteAcknowledgement::State::rejected, frame->body.front() };
        result.state = WriteAcknowledgement::State::accepted;
    }
    return result;
}

std::optional<Bytes> SettingsProtocol::findChunk(const std::vector<Bytes>& messages,
                                                 std::uint32_t address,
                                                 std::uint32_t length)
{
    std::optional<Bytes> found;
    for (const auto& payload : VendorProtocol::vendorPayloads(messages))
    {
        const auto frame = VendorProtocol::parseFrame(payload);
        if (!frame)
            continue;
        auto chunk = chunkIn(*frame);
        if (!chunk || chunk->address != address || chunk->data.size() != length)
            continue;
        if (found && *found != chunk->data)
            throw ProtocolError("Conflicting settings chunk at address " + std::to_string(address));
        found = std::move(chunk->data);
    }
    return found;
}
