#pragma once

#include "DeviceModel.h"

#include <array>
#include <cstddef>
#include <string>

enum class DeviceProfile
{
    daw = 0,
    ccUser = 1
};

enum class ControlKind
{
    knob,
    slider,
    trackButton,
    globalButton
};

struct ControlSelection
{
    ControlKind kind = ControlKind::knob;
    int index = 0;

    bool operator==(const ControlSelection& other) const
    {
        return kind == other.kind && index == other.index;
    }
};

struct KnobConfiguration
{
    std::uint8_t type = 0;
    std::uint8_t speed = 0;
    std::uint8_t channel = 0;
    std::uint8_t cc = 0;
    std::uint8_t minimum = 0;
    std::uint8_t maximum = 0;
};

struct SliderConfiguration
{
    std::uint8_t type = 0;
    std::uint8_t channel = 0;
    std::uint8_t cc = 0;
    std::uint8_t minimum = 0;
    std::uint8_t maximum = 0;
};

struct ButtonConfiguration
{
    std::uint8_t mode = 0;
    std::uint8_t channel = 0;
    std::uint8_t cc = 0;
    std::uint8_t value1 = 0;
    std::uint8_t value2 = 0;
    std::uint8_t led = 0;
    Bytes dawMessage;
};

struct ControlProfile
{
    std::array<KnobConfiguration, 8> knobs;
    std::array<SliderConfiguration, 8> sliders;
    std::array<ButtonConfiguration, 43> buttons;
};

struct ControlConfiguration
{
    std::array<ControlProfile, 2> profiles;

    static ControlConfiguration parse(const Bytes& data);
    const ControlProfile& profile(DeviceProfile profile) const;
};

std::string controlDisplayName(ControlSelection selection);
std::string trackButtonShortName(int buttonIndex);
std::string trackButtonFullName(int buttonIndex);
std::string globalButtonShortName(int buttonIndex);
std::string globalButtonFullName(int buttonIndex);
