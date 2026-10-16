#include "SettingsProtocol.h"

#include "VendorProtocol.h"

#include <map>
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
    Bytes message { 0xf0 };
    const auto encoded = VendorProtocol::encode8To7(readPayload(flashType, address, length));
    message.insert(message.end(), encoded.begin(), encoded.end());
    message.push_back(0xf7);
    return message;
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
            throw std::runtime_error("设置响应长度不匹配");

        const Bytes body(payload.begin() + 6, payload.begin() + 6 + dataLength);
        const auto expectedChecksum = VendorProtocol::checksum(body);
        const auto actualChecksum = payload[static_cast<std::size_t>(6 + dataLength)];
        if (actualChecksum != expectedChecksum)
            throw std::runtime_error("设置响应校验和不匹配：期望 "
                                     + VendorProtocol::hexString(Bytes { expectedChecksum })
                                     + "，实际 " + VendorProtocol::hexString(Bytes { actualChecksum }));

        if (body.empty() || body.front() != 0x05)
        {
            ++acknowledgementCount;
            continue;
        }
        if (body.size() < 8)
            throw std::runtime_error("设置数据块头部过短");

        const auto address = static_cast<int>(read32(body, 1));
        const auto chunkLength = read24(body, 5);
        if (body.size() != static_cast<std::size_t>(chunkLength + 8))
            throw std::runtime_error("地址 " + std::to_string(address) + " 的设置数据块长度不匹配");

        const Bytes chunk(body.begin() + 8, body.end());
        const auto existing = chunks.find(address);
        if (existing != chunks.end() && existing->second != chunk)
            throw std::runtime_error("地址 " + std::to_string(address) + " 出现冲突的设置数据块");
        chunks[address] = chunk;
    }

    SettingsReadResult result;
    int expectedAddress = 0;
    for (const auto& [address, chunk] : chunks)
    {
        if (address != expectedAddress)
            throw std::runtime_error("设置数据在地址 " + std::to_string(expectedAddress)
                                     + " 处缺失，下一个块位于 " + std::to_string(address));
        result.data.insert(result.data.end(), chunk.begin(), chunk.end());
        expectedAddress += static_cast<int>(chunk.size());
    }

    if (result.data.empty())
        throw std::runtime_error("没有收到设置数据块");

    result.chunkCount = static_cast<int>(chunks.size());
    result.acknowledgementCount = acknowledgementCount;
    return result;
}
