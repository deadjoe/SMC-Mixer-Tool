#include "VendorProtocol.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
int read24(const Bytes& bytes, std::size_t offset)
{
    return static_cast<int>(bytes[offset])
         | (static_cast<int>(bytes[offset + 1]) << 8)
         | (static_cast<int>(bytes[offset + 2]) << 16);
}

std::string byteLabel(std::uint8_t value)
{
    return VendorProtocol::hexString(Bytes { value });
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

Bytes VendorProtocol::versionQuerySysEx()
{
    Bytes message { 0xf0 };
    const auto encoded = encode8To7(versionQueryPayload());
    message.insert(message.end(), encoded.begin(), encoded.end());
    message.push_back(0xf7);
    return message;
}

VersionInfo VendorProtocol::parseVersionResponse(const Bytes& message)
{
    if (message.size() < 3 || message.front() != 0xf0 || message.back() != 0xf7)
        throw std::runtime_error("版本响应不是完整的 F0...F7 SysEx 消息");

    return parseVersionPayload(decode7To8(Bytes(message.begin() + 1, message.end() - 1)));
}

VersionInfo VendorProtocol::parseVersionPayload(const Bytes& payload)
{
    if (payload.size() < 7)
        throw std::runtime_error("厂商响应短于 7 字节头部");
    if (payload[0] != 0x00 || payload[1] != 0x59)
        throw std::runtime_error("厂商响应头部不匹配");
    if (payload[2] != 0x11)
        throw std::runtime_error("版本响应命令不匹配：" + byteLabel(payload[2]));

    const auto dataLength = read24(payload, 3);
    if (payload.size() != static_cast<std::size_t>(dataLength + 7))
        throw std::runtime_error("版本响应声明长度与实际负载不一致");
    if (dataLength < 9)
        throw std::runtime_error("版本响应数据过短");

    const Bytes data(payload.begin() + 6, payload.begin() + 6 + dataLength);
    const auto expectedChecksum = checksum(data);
    const auto actualChecksum = payload[static_cast<std::size_t>(6 + dataLength)];
    if (actualChecksum != expectedChecksum)
        throw std::runtime_error("校验和不匹配：期望 " + byteLabel(expectedChecksum)
                                 + "，实际 " + byteLabel(actualChecksum));

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
        std::ostringstream version;
        version << 'V' << std::setw(3) << std::setfill('0') << info.versionNumber;
        info.versionLabel = version.str();
        info.versionDigitBytes = Bytes(components.back().begin(), components.back().end());
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
        throw std::runtime_error("响应通过校验，但没有可识别的数字版本字段");

    VersionInfo info;
    info.deviceName = visibleText;
    info.versionNumber = std::stoi(digits);
    std::ostringstream version;
    version << 'V' << std::setw(3) << std::setfill('0') << info.versionNumber;
    info.versionLabel = version.str();
    info.versionDigitBytes = versionBytes;
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
