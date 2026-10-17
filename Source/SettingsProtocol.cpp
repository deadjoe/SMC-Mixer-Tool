#include "SettingsProtocol.h"

#include "VendorProtocol.h"

#include <map>
#include <optional>
#include <stdexcept>

namespace
{
int read24(const Bytes& bytes, std::size_t offset)
{
    return static_cast<int>(bytes[offset])
         | (static_cast<int>(bytes[offset + 1]) << 8)
         | (static_cast<int>(bytes[offset + 2]) << 16);
}

std::uint32_t read32(const Bytes& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
         | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
         | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
         | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
}

Bytes SettingsProtocol::readPayload(std::uint8_t flashType, std::uint32_t address, std::uint32_t length)
{
    Bytes body {
        flashType,
        static_cast<std::uint8_t>(address & 0xff),
        static_cast<std::uint8_t>((address >> 8) & 0xff),
        static_cast<std::uint8_t>((address >> 16) & 0xff),
        static_cast<std::uint8_t>((address >> 24) & 0xff),
        static_cast<std::uint8_t>(length & 0xff),
        static_cast<std::uint8_t>((length >> 8) & 0xff),
        static_cast<std::uint8_t>((length >> 16) & 0xff)
    };

    Bytes payload { 0x00, 0x59, 0x23, 0x08, 0x00, 0x00 };
    payload.insert(payload.end(), body.begin(), body.end());
    payload.push_back(VendorProtocol::checksum(body));
    return payload;
}

Bytes SettingsProtocol::readSysEx(std::uint8_t flashType, std::uint32_t address, std::uint32_t length)
{
    return sysEx(readPayload(flashType, address, length));
}

Bytes SettingsProtocol::sysEx(const Bytes& payload)
{
    Bytes message { 0xf0 };
    const auto encoded = VendorProtocol::encode8To7(payload);
    message.insert(message.end(), encoded.begin(), encoded.end());
    message.push_back(0xf7);
    return message;
}

namespace
{
Bytes writeFrame(std::uint32_t address, const Bytes& data, std::uint8_t flashType)
{
    Bytes body {
        flashType,
        static_cast<std::uint8_t>(address & 0xff),
        static_cast<std::uint8_t>((address >> 8) & 0xff),
        static_cast<std::uint8_t>((address >> 16) & 0xff),
        static_cast<std::uint8_t>((address >> 24) & 0xff),
        static_cast<std::uint8_t>(data.size() & 0xff),
        static_cast<std::uint8_t>((data.size() >> 8) & 0xff),
        static_cast<std::uint8_t>((data.size() >> 16) & 0xff)
    };
    body.insert(body.end(), data.begin(), data.end());

    const auto bodyLength = static_cast<std::uint32_t>(body.size());
    Bytes payload {
        0x00, 0x59, 0x22,
        static_cast<std::uint8_t>(bodyLength & 0xff),
        static_cast<std::uint8_t>((bodyLength >> 8) & 0xff),
        static_cast<std::uint8_t>((bodyLength >> 16) & 0xff)
    };
    payload.insert(payload.end(), body.begin(), body.end());
    payload.push_back(VendorProtocol::checksum(body));
    return payload;
}
}

Bytes SettingsProtocol::writePayload(std::uint32_t address, const Bytes& data, std::uint8_t flashType)
{
    // Command 0x22. USB-C wraps the result with sysEx(). Bluetooth sends these bytes unchanged.
    // The mixer acknowledges it at once, but the bytes stay in RAM until savePayload().
    if (data.empty() || data.size() > 256)
        throw std::runtime_error("Write length is invalid");
    return writeFrame(address, data, flashType);
}

Bytes SettingsProtocol::savePayload(std::uint8_t flashType)
{
    // Command 0x22 with address 0 and no data. Midi Suite sends this after its writes.
    // The mixer then commits RAM to flash and acknowledges after about 60 ms.
    return writeFrame(0, {}, flashType);
}

Bytes SettingsProtocol::writeSysEx(std::uint32_t address, const Bytes& data, std::uint8_t flashType)
{
    return sysEx(writePayload(address, data, flashType));
}

SettingsReadResult SettingsProtocol::parseResponses(const std::vector<Bytes>& messages)
{
    std::vector<Bytes> payloads;
    for (const auto& message : messages)
    {
        if (message.size() >= 3 && message.front() == 0xf0 && message.back() == 0xf7)
            payloads.push_back(VendorProtocol::decode7To8(Bytes(message.begin() + 1, message.end() - 1)));
    }
    return parsePayloads(payloads);
}

SettingsReadResult SettingsProtocol::parsePayloads(const std::vector<Bytes>& payloads)
{
    std::map<int, Bytes> chunks;
    int acknowledgementCount = 0;

    for (const auto& payload : payloads)
    {
        if (payload.size() < 7 || payload[0] != 0x00 || payload[1] != 0x59 || payload[2] != 0x23)
            continue;

        const auto dataLength = read24(payload, 3);
        if (payload.size() != static_cast<std::size_t>(dataLength + 7))
            throw std::runtime_error("Settings reply length does not match");

        const Bytes body(payload.begin() + 6, payload.begin() + 6 + dataLength);
        const auto expectedChecksum = VendorProtocol::checksum(body);
        const auto actualChecksum = payload[static_cast<std::size_t>(6 + dataLength)];
        if (actualChecksum != expectedChecksum)
            throw std::runtime_error("Settings reply checksum mismatch: expected "
                                     + VendorProtocol::hexString(Bytes { expectedChecksum })
                                     + ", received " + VendorProtocol::hexString(Bytes { actualChecksum }));

        if (body.empty() || body.front() != 0x05)
        {
            ++acknowledgementCount;
            continue;
        }
        if (body.size() < 8)
            throw std::runtime_error("Settings chunk header is too short");

        const auto address = static_cast<int>(read32(body, 1));
        const auto chunkLength = read24(body, 5);
        if (body.size() != static_cast<std::size_t>(chunkLength + 8))
            throw std::runtime_error("Settings chunk length mismatch at address " + std::to_string(address));

        const Bytes chunk(body.begin() + 8, body.end());
        const auto existing = chunks.find(address);
        if (existing != chunks.end() && existing->second != chunk)
            throw std::runtime_error("Conflicting settings chunk at address " + std::to_string(address));
        chunks[address] = chunk;
    }

    SettingsReadResult result;
    int expectedAddress = 0;
    for (const auto& [address, chunk] : chunks)
    {
        if (address != expectedAddress)
            throw std::runtime_error("Settings data is missing at address " + std::to_string(expectedAddress)
                                     + ". The next chunk is at " + std::to_string(address));
        result.data.insert(result.data.end(), chunk.begin(), chunk.end());
        expectedAddress += static_cast<int>(chunk.size());
    }

    if (result.data.empty())
        throw std::runtime_error("No settings data chunk was received");

    result.chunkCount = static_cast<int>(chunks.size());
    result.acknowledgementCount = acknowledgementCount;
    return result;
}

namespace
{
std::vector<Bytes> vendorPayloads(const std::vector<Bytes>& messages)
{
    // USB-C delivers F0 … F7 SysEx. Bluetooth delivers the same unpacked 00 59 payload.
    std::vector<Bytes> payloads;
    for (const auto& message : messages)
    {
        if (message.size() >= 3 && message.front() == 0xf0 && message.back() == 0xf7)
            payloads.push_back(VendorProtocol::decode7To8(Bytes(message.begin() + 1, message.end() - 1)));
        else if (message.size() >= 7 && message[0] == 0x00 && message[1] == 0x59)
            payloads.push_back(message);
    }
    return payloads;
}

bool takeBody(const Bytes& payload, Bytes& body)
{
    if (payload.size() < 7 || payload[0] != 0x00 || payload[1] != 0x59)
        return false;
    const auto dataLength = read24(payload, 3);
    if (dataLength < 0 || payload.size() != static_cast<std::size_t>(dataLength + 7))
        throw std::runtime_error("Settings reply length does not match");
    body.assign(payload.begin() + 6, payload.begin() + 6 + dataLength);
    if (payload[static_cast<std::size_t>(6 + dataLength)] != VendorProtocol::checksum(body))
        throw std::runtime_error("Settings reply checksum does not match");
    return true;
}
}

void SettingsProtocol::parseWriteAcknowledgement(const std::vector<Bytes>& messages)
{
    bool accepted = false;
    for (const auto& payload : vendorPayloads(messages))
    {
        if (payload.size() < 3 || payload[2] != 0x00)
            continue;
        Bytes body;
        if (!takeBody(payload, body))
            continue;
        if (body.size() != 1)
            throw std::runtime_error("Write acknowledgement has an unexpected length");
        if (body.front() != 0x00)
            throw std::runtime_error("The mixer rejected the write, status "
                                     + VendorProtocol::hexString(Bytes { body.front() }));
        accepted = true;
    }
    if (!accepted)
        throw std::runtime_error("No write acknowledgement was received");
}

Bytes SettingsProtocol::parseChunk(const std::vector<Bytes>& messages, std::uint32_t address, std::uint32_t length)
{
    std::optional<Bytes> chunk;
    for (const auto& payload : vendorPayloads(messages))
    {
        if (payload.size() < 3 || payload[2] != 0x23)
            continue;
        Bytes body;
        if (!takeBody(payload, body) || body.empty() || body.front() != 0x05 || body.size() < 8)
            continue;
        const auto chunkAddress = read32(body, 1);
        const auto chunkLength = static_cast<std::uint32_t>(read24(body, 5));
        if (chunkAddress != address || chunkLength != length || body.size() != static_cast<std::size_t>(chunkLength + 8))
            continue;
        const Bytes data(body.begin() + 8, body.end());
        if (chunk && *chunk != data)
            throw std::runtime_error("Conflicting settings chunk at address " + std::to_string(address));
        chunk = data;
    }
    if (!chunk)
        throw std::runtime_error("No settings chunk was received at address " + std::to_string(address));
    return *chunk;
}
