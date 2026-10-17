#pragma once

#include "DeviceModel.h"
#include "SettingsLayout.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum class DeviceProfile
{
    daw = 0,
    ccUser = 1
};

enum class EditableField
{
    type,
    mode,
    channel,
    cc,
    minimum,
    maximum,
    value1,
    value2,
    led
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
    bool operator!=(const ControlSelection& other) const { return !(*this == other); }
};

bool isButton(ControlKind kind);
// Whether the index names a control of that kind.
bool isValidControl(ControlSelection selection);
// The position of a track or global button among the 43 button records.
std::size_t buttonRecordIndex(ControlSelection selection);
// Every control in record order: knobs, faders, track buttons, global buttons.
const std::vector<ControlSelection>& allControls();

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
    std::array<KnobConfiguration, layout::knobCount> knobs;
    std::array<SliderConfiguration, layout::sliderCount> sliders;
    std::array<ButtonConfiguration, layout::buttonCount> buttons;

    const ButtonConfiguration& button(ControlSelection selection) const { return buttons[buttonRecordIndex(selection)]; }
};

struct FieldRange
{
    int minimum = 0;
    int maximum = 0;

    bool contains(int value) const { return value >= minimum && value <= maximum; }
};

bool hasField(ControlKind kind, EditableField field);
// The values a field can hold at all: channel 0-15, MIDI data 0-127, and so on.
FieldRange fieldLimits(ControlKind kind, EditableField field);
std::uint8_t fieldValue(const ControlProfile& profile, ControlSelection selection, EditableField field);
// The values a field may take now. Min stays at or below Max, except on a relative knob,
// whose Min and Max are two values to send rather than a range.
FieldRange allowedRange(const ControlProfile& profile, ControlSelection selection, EditableField field);
bool isRelativeKnob(const ControlProfile& profile, ControlSelection selection);

struct ControlConfiguration
{
    std::array<ControlProfile, layout::profileCount> profiles;

    static ControlConfiguration parse(const Bytes& data);
    // The inverse of parse() for one profile. Bytes the records do not describe, such as
    // the unused tail of a button's message area, keep their value in the image.
    static void writeProfile(const ControlProfile& profile, DeviceProfile which, Bytes& image);
    const ControlProfile& profile(DeviceProfile profile) const;
    std::string toJson(DeviceProfile profile) const;
};

// Knob type 3 is relative (Midi Suite calls it CW): Min and Max are the values sent
// per counter-clockwise and clockwise step.
constexpr std::uint8_t knobTypeRelative = 3;
constexpr std::uint8_t knobTypeCount = 4;
constexpr std::uint8_t sliderTypeCount = 3;
constexpr std::uint8_t buttonModeCount = 6;
constexpr std::uint8_t buttonModeCustom = 4;

const char* knobTypeName(std::uint8_t type);
const char* sliderTypeName(std::uint8_t type);
const char* buttonModeName(std::uint8_t mode);
// The MIDI 1.0 name of a control change number, or "Undefined".
const char* controlChangeName(std::uint8_t cc);

std::string profileName(DeviceProfile profile);
std::string controlDisplayName(ControlSelection selection);
std::string trackButtonShortName(int buttonIndex);
std::string trackButtonFullName(int buttonIndex);
std::string globalButtonShortName(int buttonIndex);
std::string globalButtonFullName(int buttonIndex);
