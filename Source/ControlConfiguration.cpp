#include "ControlConfiguration.h"

#include <stdexcept>

namespace
{
constexpr std::size_t profileSize = 3141;
constexpr std::size_t buttonSize = 71;
constexpr std::size_t buttonCount = 43;
constexpr std::size_t knobSize = 6;
constexpr std::size_t sliderSize = 5;
constexpr std::size_t knobsOffset = buttonCount * buttonSize;
constexpr std::size_t slidersOffset = knobsOffset + 8 * knobSize;

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

ControlConfiguration ControlConfiguration::parse(const Bytes& data)
{
    if (data.size() != profileSize * 2)
        throw std::runtime_error("Control configuration must contain exactly 6282 bytes");

    ControlConfiguration result;
    for (std::size_t profileIndex = 0; profileIndex < 2; ++profileIndex)
    {
        const auto profileBase = profileIndex * profileSize;
        auto& profile = result.profiles[profileIndex];

        for (std::size_t index = 0; index < buttonCount; ++index)
        {
            const auto offset = profileBase + index * buttonSize;
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

        const auto knobBase = profileBase + knobsOffset;
        for (std::size_t index = 0; index < 8; ++index)
        {
            const auto offset = knobBase + index * knobSize;
            auto& knob = profile.knobs[index];
            knob.type = data[offset];
            knob.speed = data[offset + 1];
            knob.channel = data[offset + 2];
            knob.cc = data[offset + 3];
            knob.minimum = data[offset + 4];
            knob.maximum = data[offset + 5];
        }

        const auto sliderBase = profileBase + slidersOffset;
        for (std::size_t index = 0; index < 8; ++index)
        {
            const auto offset = sliderBase + index * sliderSize;
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
