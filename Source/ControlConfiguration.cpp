#include "ControlConfiguration.h"

#include <iterator>
#include <stdexcept>

namespace
{
const char* trackNames[] { "Mute", "Solo", "Record", "Select" };
const char* trackShortNames[] { "M", "S", "R", "SEL" };
const char* globalNames[] {
    "Play", "Stop", "Record", "Rewind", "Fast Forward", "Bank Previous",
    "Bank Next", "Up", "Down", "Left", "Right"
};
const char* globalShortNames[] {
    "Play", "Stop", "Rec", "RW", "FF", "Bank-", "Bank+", "Up", "Down", "Left", "Right"
};

template <typename Names>
std::string nameAt(Names& names, std::size_t count, int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < count ? names[index] : "Unknown";
}
}

const char* knobTypeName(std::uint8_t type)
{
    switch (type)
    {
        case 0: return "CC Abs";
        case 1: return "Aftertouch";
        case 2: return "Note";
        case 3: return "CC Rel"; // Midi Suite calls this CW
        default: return nullptr;
    }
}

const char* sliderTypeName(std::uint8_t type)
{
    switch (type)
    {
        case 0: return "CC";
        case 1: return "Aftertouch";
        case 2: return "DAW";
        default: return nullptr;
    }
}

const char* buttonModeName(std::uint8_t mode)
{
    switch (mode)
    {
        case 0: return "CC Single";
        case 1: return "CC Toggle";
        case 2: return "CC Push";
        case 3: return "Program";
        case 4: return "Custom";
        case 5: return "Note";
        default: return nullptr;
    }
}

const char* controlChangeName(std::uint8_t cc)
{
    static const char* const msb[] {
        "Bank Select", "Modulation", "Breath Controller", "Undefined", "Foot Controller", "Portamento Time",
        "Data Entry", "Channel Volume", "Balance", "Undefined", "Pan", "Expression", "Effect Control 1",
        "Effect Control 2", "Undefined", "Undefined", "General Purpose 1", "General Purpose 2",
        "General Purpose 3", "General Purpose 4", "Undefined", "Undefined", "Undefined", "Undefined",
        "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined"
    };
    static const char* const lsb[] {
        "Bank Select LSB", "Modulation LSB", "Breath Controller LSB", "Undefined LSB", "Foot Controller LSB",
        "Portamento Time LSB", "Data Entry LSB", "Channel Volume LSB", "Balance LSB", "Undefined LSB", "Pan LSB",
        "Expression LSB", "Effect Control 1 LSB", "Effect Control 2 LSB", "Undefined LSB", "Undefined LSB",
        "General Purpose 1 LSB", "General Purpose 2 LSB", "General Purpose 3 LSB", "General Purpose 4 LSB",
        "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB",
        "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB", "Undefined LSB"
    };
    static const char* const upper[] {
        "Sustain", "Portamento", "Sostenuto", "Soft Pedal", "Legato", "Hold 2",
        "Sound Variation", "Timbre", "Release Time", "Attack Time", "Brightness", "Decay Time",
        "Vibrato Rate", "Vibrato Depth", "Vibrato Delay", "Sound Controller 10",
        "General Purpose 5", "General Purpose 6", "General Purpose 7", "General Purpose 8", "Portamento Control",
        "Undefined", "Undefined", "Undefined", "High Resolution Velocity", "Undefined", "Undefined",
        "Reverb Depth", "Tremolo Depth", "Chorus Depth", "Detune Depth", "Phaser Depth",
        "Data Increment", "Data Decrement", "NRPN LSB", "NRPN MSB", "RPN LSB", "RPN MSB",
        "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined",
        "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined", "Undefined",
        "Undefined", "Undefined", "All Sound Off", "Reset All Controllers", "Local Control", "All Notes Off", "Omni Off", "Omni On",
        "Mono On", "Poly On"
    };
    static_assert(std::size(msb) == 32 && std::size(lsb) == 32 && std::size(upper) == 64,
                  "Each MIDI CC range needs one name per number");
    if (cc < 32)
        return msb[cc];
    if (cc < 64)
        return lsb[cc - 32];
    if (cc < 128)
        return upper[cc - 64];
    return "Out of range";
}

ControlConfiguration ControlConfiguration::parse(const Bytes& data)
{
    if (data.size() != layout::snapshotSize)
        throw std::runtime_error("Control configuration must contain exactly 6282 bytes");

    ControlConfiguration result;
    for (std::size_t profileIndex = 0; profileIndex < 2; ++profileIndex)
    {
        const auto profileBase = profileIndex * layout::profileSize;
        auto& profile = result.profiles[profileIndex];

        for (std::size_t index = 0; index < layout::buttonCount; ++index)
        {
            const auto offset = profileBase + index * layout::buttonSize;
            auto& button = profile.buttons[index];
            button.mode = data[offset];
            button.channel = data[offset + 1];
            button.cc = data[offset + 2];
            button.value1 = data[offset + 3];
            button.value2 = data[offset + 4];
            const auto dawLength = data[offset + 5];
            if (dawLength > 64)
                throw std::runtime_error("Invalid DAW message length in button record");
            button.dawMessage = Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset + 6),
                                      data.begin() + static_cast<std::ptrdiff_t>(offset + 6 + dawLength));
            button.led = data[offset + 70];
        }

        const auto knobBase = profileBase + layout::knobsOffset;
        for (std::size_t index = 0; index < layout::knobCount; ++index)
        {
            const auto offset = knobBase + index * layout::knobSize;
            auto& knob = profile.knobs[index];
            knob.type = data[offset];
            knob.speed = data[offset + 1];
            knob.channel = data[offset + 2];
            knob.cc = data[offset + 3];
            knob.minimum = data[offset + 4];
            knob.maximum = data[offset + 5];
        }

        const auto sliderBase = profileBase + layout::slidersOffset;
        for (std::size_t index = 0; index < layout::sliderCount; ++index)
        {
            const auto offset = sliderBase + index * layout::sliderSize;
            auto& slider = profile.sliders[index];
            slider.type = data[offset];
            slider.channel = data[offset + 1];
            slider.cc = data[offset + 2];
            slider.minimum = data[offset + 3];
            slider.maximum = data[offset + 4];
        }
    }
    return result;
}

const ControlProfile& ControlConfiguration::profile(DeviceProfile selectedProfile) const
{
    return profiles[static_cast<std::size_t>(selectedProfile)];
}

namespace
{
void appendJsonString(std::string& json, const std::string& value)
{
    json.push_back('"');
    for (const char character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte)
        {
            case '"': json += "\\\""; break;
            case '\\': json += "\\\\"; break;
            case '\n': json += "\\n"; break;
            case '\r': json += "\\r"; break;
            case '\t': json += "\\t"; break;
            default:
                if (byte < 0x20)
                {
                    constexpr char digits[] = "0123456789abcdef";
                    json += "\\u00";
                    json.push_back(digits[byte >> 4]);
                    json.push_back(digits[byte & 0x0f]);
                }
                else
                {
                    json.push_back(character);
                }
                break;
        }
    }
    json.push_back('"');
}

std::string hexMessage(const Bytes& bytes)
{
    std::string text;
    for (const auto byte : bytes)
    {
        if (!text.empty())
            text.push_back(' ');
        constexpr char digits[] = "0123456789ABCDEF";
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0f]);
    }
    return text;
}

void appendKnob(std::string& json, const char* name, int index, const KnobConfiguration& knob)
{
    json += "    {\n";
    json += "      \"index\": " + std::to_string(index) + ",\n";
    json += "      \"name\": ";
    appendJsonString(json, std::string(name) + " " + std::to_string(index));
    json += ",\n";
    json += "      \"type\": " + std::to_string(knob.type) + ",\n";
    json += "      \"speed\": " + std::to_string(knob.speed) + ",\n";
    json += "      \"channel\": " + std::to_string(static_cast<int>(knob.channel) + 1) + ",\n";
    json += "      \"cc\": " + std::to_string(knob.cc) + ",\n";
    json += "      \"minimum\": " + std::to_string(knob.minimum) + ",\n";
    json += "      \"maximum\": " + std::to_string(knob.maximum) + "\n";
    json += "    }";
}

void appendButton(std::string& json, int track, const char* name, const ButtonConfiguration& button)
{
    json += "    {\n";
    if (track > 0)
        json += "      \"track\": " + std::to_string(track) + ",\n";
    json += "      \"name\": ";
    appendJsonString(json, name);
    json += ",\n";
    json += "      \"mode\": " + std::to_string(button.mode) + ",\n";
    json += "      \"channel\": " + std::to_string(static_cast<int>(button.channel) + 1) + ",\n";
    json += "      \"cc\": " + std::to_string(button.cc) + ",\n";
    json += "      \"value1\": " + std::to_string(button.value1) + ",\n";
    json += "      \"value2\": " + std::to_string(button.value2) + ",\n";
    json += "      \"led\": " + std::to_string(button.led) + ",\n";
    json += "      \"message\": ";
    appendJsonString(json, hexMessage(button.dawMessage));
    json += "\n    }";
}
}

std::string ControlConfiguration::toJson(DeviceProfile selectedProfile) const
{
    const auto& source = profile(selectedProfile);
    std::string json = "{\n  \"profile\": ";
    appendJsonString(json, selectedProfile == DeviceProfile::ccUser ? "CC / User" : "DAW");
    json += ",\n  \"knobs\": [\n";
    for (std::size_t index = 0; index < source.knobs.size(); ++index)
    {
        appendKnob(json, "Knob", static_cast<int>(index + 1), source.knobs[index]);
        json += index + 1 == source.knobs.size() ? "\n" : ",\n";
    }
    json += "  ],\n  \"faders\": [\n";
    for (std::size_t index = 0; index < source.sliders.size(); ++index)
    {
        const auto& slider = source.sliders[index];
        json += "    {\n";
        json += "      \"index\": " + std::to_string(index + 1) + ",\n";
        json += "      \"name\": ";
        appendJsonString(json, "Fader " + std::to_string(index + 1));
        json += ",\n";
        json += "      \"type\": " + std::to_string(slider.type) + ",\n";
        json += "      \"channel\": " + std::to_string(static_cast<int>(slider.channel) + 1) + ",\n";
        json += "      \"cc\": " + std::to_string(slider.cc) + ",\n";
        json += "      \"minimum\": " + std::to_string(slider.minimum) + ",\n";
        json += "      \"maximum\": " + std::to_string(slider.maximum) + "\n";
        json += "    }";
        json += index + 1 == source.sliders.size() ? "\n" : ",\n";
    }
    json += "  ],\n  \"trackButtons\": [\n";
    for (int index = 0; index < 32; ++index)
    {
        appendButton(json, index / 4 + 1, trackButtonFullName(index % 4).c_str(), source.buttons[static_cast<std::size_t>(index)]);
        json += index == 31 ? "\n" : ",\n";
    }
    json += "  ],\n  \"globalButtons\": [\n";
    for (int index = 0; index < 11; ++index)
    {
        appendButton(json, 0, globalButtonFullName(index).c_str(), source.buttons[static_cast<std::size_t>(32 + index)]);
        json += index == 10 ? "\n" : ",\n";
    }
    json += "  ]\n}\n";
    return json;
}

std::string controlDisplayName(ControlSelection selection)
{
    switch (selection.kind)
    {
        case ControlKind::knob:
            return "Knob " + std::to_string(selection.index + 1);
        case ControlKind::slider:
            return "Fader " + std::to_string(selection.index + 1);
        case ControlKind::trackButton:
            return "Track " + std::to_string(selection.index / 4 + 1) + " · "
                 + trackButtonFullName(selection.index % 4);
        case ControlKind::globalButton:
            return "Global · " + globalButtonFullName(selection.index);
    }
    return "Unknown control";
}

std::string trackButtonShortName(int buttonIndex)
{
    return nameAt(trackShortNames, 4, buttonIndex);
}

std::string trackButtonFullName(int buttonIndex)
{
    return nameAt(trackNames, 4, buttonIndex);
}

std::string globalButtonShortName(int buttonIndex)
{
    return nameAt(globalShortNames, 11, buttonIndex);
}

std::string globalButtonFullName(int buttonIndex)
{
    return nameAt(globalNames, 11, buttonIndex);
}
