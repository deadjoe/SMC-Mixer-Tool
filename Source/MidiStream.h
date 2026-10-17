#pragma once

#include "DeviceModel.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// Collects complete F0 ... F7 SysEx messages from a raw MIDI byte stream.
// Real-time bytes (F8-FF) may appear anywhere and are skipped. Any other status byte
// ends an unfinished SysEx, which is then dropped rather than joined to later bytes.
class SysExAssembler
{
public:
    static constexpr std::size_t maximumSize = 65536;

    // Appends each message completed by these bytes to completed.
    void feed(const std::uint8_t* data, std::size_t size, std::vector<Bytes>& completed)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            const auto byte = data[index];
            if (byte >= 0xf8)
                continue;
            if (byte == 0xf0)
            {
                buffer = Bytes { byte };
                continue;
            }
            if (buffer.empty())
                continue;
            if (byte >= 0x80 && byte != 0xf7)
            {
                buffer.clear();
                continue;
            }
            if (buffer.size() >= maximumSize)
            {
                buffer.clear();
                continue;
            }
            buffer.push_back(byte);
            if (byte == 0xf7)
            {
                if (buffer.size() > 2)
                    completed.push_back(buffer);
                buffer.clear();
            }
        }
    }

    void reset() { buffer.clear(); }

private:
    Bytes buffer;
};

namespace midiname
{
inline bool containsIgnoringCase(const std::string& value, const std::string& part)
{
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    };
    return lower(value).find(lower(part)) != std::string::npos;
}

// Joins device, entity, and endpoint names. The mixer names its entity and endpoint alike
// ("-Private"), so a part already shown is skipped, and a part that contains the name so
// far replaces it.
inline std::string join(std::initializer_list<std::string> parts)
{
    std::string result;
    for (const auto& part : parts)
    {
        if (part.empty() || part == "(none)" || part == "(unknown)")
            continue;
        if (containsIgnoringCase(result, part))
            continue;
        if (!result.empty() && containsIgnoringCase(part, result))
        {
            result = part;
            continue;
        }
        if (!result.empty())
            result += ' ';
        result += part;
    }
    return result;
}
}
