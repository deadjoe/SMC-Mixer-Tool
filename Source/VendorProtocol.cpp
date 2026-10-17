#include "VendorProtocol.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace
{
std::string byteLabel(std::uint8_t value)
{
    return VendorProtocol::hexString(Bytes { value });
}

std::string versionLabel(int number)
{
    std::ostringstream label;
    label << 'V' << std::setw(3) << std::setfill('0') << number;
    return label.str();
}

std::string trim(std::string value)
{
    const auto isSpace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::vector<std::string> split(const std::string& value, char separator)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true)
    {
        const auto end = value.find(separator, begin);
        parts.push_back(value.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return parts;
}
}

Bytes VendorProtocol::encode8To7(const Bytes& input)
{
    Bytes output;
    output.reserve(input.size() * 8 / 7 + 1);
    int accumulator = 0;
    int bitCount = 0;

    for (const auto byte : input)
    {
        accumulator |= static_cast<int>(byte) << bitCount;
        bitCount += 8;
        while (bitCount >= 7)
        {
            output.push_back(static_cast<std::uint8_t>(accumulator & 0x7f));
            accumulator >>= 7;
            bitCount -= 7;
        }
    }

    if (bitCount > 0)
        output.push_back(static_cast<std::uint8_t>(accumulator & 0x7f));
    return output;
}

Bytes VendorProtocol::decode7To8(const Bytes& input)
{
    Bytes output;
    output.reserve(input.size() * 7 / 8 + 1);
    int accumulator = 0;
    int bitCount = 0;

    for (const auto byte : input)
    {
        accumulator |= static_cast<int>(byte) << bitCount;
        bitCount += 7;
        while (bitCount >= 8)
        {
            output.push_back(static_cast<std::uint8_t>(accumulator & 0xff));
            accumulator >>= 8;
            bitCount -= 8;
        }
    }
    return output;
}

Bytes VendorProtocol::versionQueryPayload()
{
    return { 0x00, 0x59, 0x11, 0x00, 0x00, 0x00, 0xff };
}

Bytes VendorProtocol::frame(std::uint8_t command, const Bytes& body)
{
    Bytes payload { 0x00, 0x59, command };
    appendLE(payload, static_cast<std::uint32_t>(body.size()), 3);
    payload.insert(payload.end(), body.begin(), body.end());
    payload.push_back(checksum(body));
    return payload;
}

std::optional<VendorFrame> VendorProtocol::parseFrame(const Bytes& payload)
{
    if (payload.size() < headerSize + 1 || payload[0] != 0x00 || payload[1] != 0x59)
        return std::nullopt;

    const auto length = static_cast<std::size_t>(readLE(payload, 3, 3));
    if (payload.size() != headerSize + length + 1)
        throw ProtocolError("Vendor reply length does not match its payload");

    VendorFrame result;
    result.command = payload[2];
    result.body.assign(payload.begin() + static_cast<std::ptrdiff_t>(headerSize),
                       payload.begin() + static_cast<std::ptrdiff_t>(headerSize + length));
    const auto expected = checksum(result.body);
    const auto actual = payload.back();
    if (actual != expected)
        throw ProtocolError("Vendor reply checksum mismatch: expected " + byteLabel(expected)
                            + ", received " + byteLabel(actual));
    return result;
}

Bytes VendorProtocol::sysEx(const Bytes& payload)
{
    Bytes message { 0xf0 };
    const auto encoded = encode8To7(payload);
    message.insert(message.end(), encoded.begin(), encoded.end());
    message.push_back(0xf7);
    return message;
}

std::optional<Bytes> VendorProtocol::unwrapSysEx(const Bytes& message)
{
    if (message.size() < 3 || message.front() != 0xf0 || message.back() != 0xf7)
        return std::nullopt;
    return decode7To8(Bytes(message.begin() + 1, message.end() - 1));
}

std::vector<Bytes> VendorProtocol::vendorPayloads(const std::vector<Bytes>& messages)
{
    const auto isVendor = [](const Bytes& bytes) {
        return bytes.size() > headerSize && bytes[0] == 0x00 && bytes[1] == 0x59;
    };
    std::vector<Bytes> payloads;
    for (const auto& message : messages)
    {
        if (auto payload = unwrapSysEx(message); payload && isVendor(*payload))
            payloads.push_back(std::move(*payload));
        else if (isVendor(message))
            payloads.push_back(message);
    }
    return payloads;
}

std::vector<Bytes> VendorProtocol::fragments(const Bytes& payload, std::size_t maximumWrite)
{
    if (maximumWrite == 0 || payload.size() <= maximumWrite)
        return { payload };
    std::vector<Bytes> pieces;
    for (std::size_t offset = 0; offset < payload.size(); offset += maximumWrite)
    {
        const auto end = std::min(payload.size(), offset + maximumWrite);
        pieces.emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return pieces;
}

VersionInfo VendorProtocol::parseVersionResponse(const Bytes& message)
{
    const auto payload = unwrapSysEx(message);
    if (!payload)
        throw ProtocolError("Version reply is not a complete F0...F7 SysEx message");
    return parseVersionPayload(*payload);
}

VersionInfo VendorProtocol::parseVersionPayload(const Bytes& payload)
{
    if (payload.size() < headerSize + 1)
        throw ProtocolError("Vendor reply is shorter than its 7-byte header");
    const auto parsed = parseFrame(payload);
    if (!parsed)
        throw ProtocolError("Vendor reply header does not match");
    if (parsed->command != 0x11)
        throw ProtocolError("Version reply command does not match: " + byteLabel(parsed->command));
    const auto& data = parsed->body;
    if (data.size() < 9)
        throw ProtocolError("Version reply data is too short");

    const auto textEnd = std::find(data.begin(), data.end(), 0);
    const auto visibleText = trim(std::string(data.begin(), textEnd));
    const auto components = split(visibleText, '_');

    if (components.size() >= 2 && !components.back().empty()
        && std::all_of(components.back().begin(), components.back().end(), [](unsigned char character) {
               return std::isdigit(character) != 0;
           }))
    {
        VersionInfo info;
        info.versionNumber = std::stoi(components.back());
        std::ostringstream deviceName;
        for (std::size_t index = 0; index + 1 < components.size(); ++index)
        {
            if (index > 0)
                deviceName << '_';
            deviceName << components[index];
        }
        info.deviceName = deviceName.str();
        info.versionLabel = versionLabel(info.versionNumber);
        info.rawPayload = payload;
        return info;
    }

    const auto versionBegin = std::min<std::size_t>(14, payload.size());
    const auto versionEnd = std::min(versionBegin + 20, payload.size());
    const Bytes versionBytes(payload.begin() + static_cast<std::ptrdiff_t>(versionBegin),
                             payload.begin() + static_cast<std::ptrdiff_t>(versionEnd));
    std::string digits;
    for (const auto byte : versionBytes)
    {
        if (byte > 9)
            break;
        digits += std::to_string(static_cast<int>(byte));
    }
    if (digits.empty())
        throw ProtocolError("The reply passed its checksum, but it has no numeric version");

    VersionInfo info;
    info.deviceName = visibleText;
    info.versionNumber = std::stoi(digits);
    info.versionLabel = versionLabel(info.versionNumber);
    info.rawPayload = payload;
    return info;
}

std::uint8_t VendorProtocol::checksum(const Bytes& body)
{
    int sum = 0;
    for (const auto byte : body)
        sum = (sum + static_cast<int>(byte)) & 0xff;
    return static_cast<std::uint8_t>(~sum);
}

std::uint32_t VendorProtocol::readLE(const Bytes& bytes, std::size_t offset, std::size_t count)
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < count; ++index)
        value |= static_cast<std::uint32_t>(bytes.at(offset + index)) << (8 * index);
    return value;
}

void VendorProtocol::appendLE(Bytes& bytes, std::uint32_t value, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xff));
}

std::string VendorProtocol::hexString(const Bytes& bytes, std::size_t maximumCount)
{
    const auto count = maximumCount == 0 ? bytes.size() : std::min(maximumCount, bytes.size());
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < count; ++index)
    {
        if (index > 0)
            output << ' ';
        output << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return output.str();
}
