#include "ControlDescription.h"

#include "VendorProtocol.h"

namespace
{
const std::string dot = " \xc2\xb7 ";

std::string channelText(std::uint8_t channel)
{
    return "Ch " + std::to_string(static_cast<int>(channel) + 1);
}

std::string nameOr(const char* name, std::uint8_t value)
{
    return name != nullptr ? std::string(name) : std::to_string(value);
}
}

FieldMeaning knobMeaning(std::uint8_t type)
{
    FieldMeaning meaning;
    switch (type)
    {
        case 0:
            meaning.note = "Sends the CC with a value that moves between Min and Max.";
            break;
        case 1:
            meaning.ccIsController = false;
            meaning.ccUsed = false;
            meaning.note = "Sends Channel Pressure between Min and Max. No CC number is sent.";
            break;
        case 2:
            meaning.ccIsController = false;
            meaning.ccUsed = false;
            meaning.note = "Like a Mackie fader: Note On 104 plus the channel number on channel 1 while turning, "
                           "and Pitch Bend between Min and Max on this channel. No CC number is sent.";
            break;
        case knobTypeRelative:
            meaning.minimumName = "CCW value";
            meaning.maximumName = "CW value";
            meaning.note = "Each step sends the CC with the CW or CCW value. Use 1 and 127 for two's complement, "
                           "or 65 and 63 for binary offset.";
            break;
        default:
            break;
    }
    return meaning;
}

FieldMeaning sliderMeaning(std::uint8_t type)
{
    FieldMeaning meaning;
    switch (type)
    {
        case 0:
            meaning.note = "Sends the CC with a value between Min and Max.";
            break;
        case 1:
            meaning.ccIsController = false;
            meaning.ccUsed = false;
            meaning.note = "Sends Channel Pressure between Min and Max. No CC number is sent.";
            break;
        case 2:
            meaning.ccIsController = false;
            meaning.ccUsed = false;
            meaning.note = "Sends Pitch Bend between Min and Max on the channel, as a Mackie fader does. "
                           "No CC number is sent.";
            break;
        default:
            break;
    }
    return meaning;
}

FieldMeaning buttonMeaning(std::uint8_t mode)
{
    FieldMeaning meaning;
    switch (mode)
    {
        case 0:
            meaning.value1Name = "Press value";
            meaning.value2Used = false;
            meaning.note = "Sends Value 1 once on each press. Nothing is sent on release.";
            break;
        case 1:
            meaning.value1Name = "Off value";
            meaning.value2Name = "On value";
            meaning.note = "Each press alternates between Value 2 and Value 1.";
            break;
        case 2:
            meaning.value1Name = "Release value";
            meaning.value2Name = "Press value";
            meaning.note = "Sends Value 2 on press and Value 1 on release.";
            break;
        case 3:
            meaning.ccIsController = false;
            meaning.ccName = "Bank MSB";
            meaning.value1Name = "Bank LSB";
            meaning.value2Name = "Program";
            meaning.note = "Each press sends Bank Select MSB, Bank Select LSB, then Program Change.";
            break;
        case buttonModeCustom:
            meaning.ccIsController = false;
            meaning.ccUsed = false;
            meaning.value1Used = false;
            meaning.value2Used = false;
            meaning.note = "Each press sends the stored message.";
            break;
        case 5:
            meaning.ccIsController = false;
            meaning.ccName = "Note";
            meaning.value1Used = false;
            meaning.value2Used = false;
            meaning.note = "Sends Note On at velocity 127 on press and at velocity 0 on release.";
            break;
        default:
            break;
    }
    return meaning;
}

FieldMeaning meaningFor(const ControlProfile& profile, ControlSelection selection)
{
    if (!isValidControl(selection))
        return {};
    const auto index = static_cast<std::size_t>(selection.index);
    switch (selection.kind)
    {
        case ControlKind::knob: return knobMeaning(profile.knobs[index].type);
        case ControlKind::slider: return sliderMeaning(profile.sliders[index].type);
        case ControlKind::trackButton:
        case ControlKind::globalButton: return buttonMeaning(profile.button(selection).mode);
    }
    return {};
}

std::string controlNote(const ControlProfile& profile, ControlSelection selection)
{
    if (!isValidControl(selection))
        return {};
    if (isButton(selection.kind) && profile.button(selection).mode == buttonModeCustom)
    {
        const auto& message = profile.button(selection).dawMessage;
        return message.empty() ? "Custom has no stored message."
                               : "Each press sends " + VendorProtocol::hexString(message) + ".";
    }
    return meaningFor(profile, selection).note;
}

ControlSummary summarize(const ControlProfile& profile, ControlSelection selection)
{
    ControlSummary summary;
    if (!isValidControl(selection))
        return summary;
    const auto index = static_cast<std::size_t>(selection.index);

    switch (selection.kind)
    {
        case ControlKind::knob:
        {
            const auto& knob = profile.knobs[index];
            const auto channel = channelText(knob.channel);
            switch (knob.type)
            {
                case 0: summary = { std::to_string(knob.cc), "Abs" + dot + channel, true, knob.minimum, knob.maximum, false }; break;
                case 1: summary = { "AT", channel, true, knob.minimum, knob.maximum, false }; break;
                case 2: summary = { "PB", "Touch" + dot + channel, true, knob.minimum, knob.maximum, false }; break;
                case knobTypeRelative: summary = { std::to_string(knob.cc), "Rel" + dot + channel, false, 0, 127, true }; break;
                default: summary = { "?", "Type " + std::to_string(knob.type), false, 0, 127, false }; break;
            }
            break;
        }
        case ControlKind::slider:
        {
            const auto& slider = profile.sliders[index];
            const auto channel = channelText(slider.channel);
            switch (slider.type)
            {
                case 0: summary = { "CC " + std::to_string(slider.cc), channel, true, slider.minimum, slider.maximum, false }; break;
                case 1: summary = { "AT", channel, true, slider.minimum, slider.maximum, false }; break;
                case 2: summary = { "PB", channel, true, slider.minimum, slider.maximum, false }; break;
                default: summary = { "?", "Type " + std::to_string(slider.type), false, 0, 127, false }; break;
            }
            break;
        }
        case ControlKind::trackButton:
        case ControlKind::globalButton:
        {
            const auto& button = profile.button(selection);
            summary.secondary = channelText(button.channel);
            switch (button.mode)
            {
                case 0:
                case 1:
                case 2: summary.primary = "CC " + std::to_string(button.cc); break;
                case 3: summary.primary = "PC " + std::to_string(button.value2); break;
                case buttonModeCustom: summary.primary = "SysEx"; break;
                case 5: summary.primary = "Note " + std::to_string(button.cc); break;
                default: summary.primary = "Mode " + std::to_string(button.mode); break;
            }
            break;
        }
    }
    return summary;
}

std::string describe(const ControlProfile& profile, ControlSelection selection)
{
    if (!isValidControl(selection))
        return {};
    const auto meaning = meaningFor(profile, selection);
    const auto value = [&](EditableField field) { return std::to_string(fieldValue(profile, selection, field)); };

    std::string text = controlDisplayName(selection) + "\n";
    const auto typeOrMode = isButton(selection.kind) ? EditableField::mode : EditableField::type;
    const auto code = fieldValue(profile, selection, typeOrMode);
    text += selection.kind == ControlKind::knob ? nameOr(knobTypeName(code), code)
          : selection.kind == ControlKind::slider ? nameOr(sliderTypeName(code), code)
                                                  : nameOr(buttonModeName(code), code);
    text += dot + "Channel " + std::to_string(fieldValue(profile, selection, EditableField::channel) + 1);
    if (meaning.ccUsed)
    {
        text += dot + meaning.ccName + " " + value(EditableField::cc);
        if (meaning.ccIsController)
            text += std::string(" (") + controlChangeName(fieldValue(profile, selection, EditableField::cc)) + ")";
    }
    if (!isButton(selection.kind))
        text += dot + meaning.minimumName + " " + value(EditableField::minimum)
              + dot + meaning.maximumName + " " + value(EditableField::maximum);
    if (isButton(selection.kind))
    {
        if (meaning.value1Used)
            text += dot + meaning.value1Name + " " + value(EditableField::value1);
        if (meaning.value2Used)
            text += dot + meaning.value2Name + " " + value(EditableField::value2);
    }
    return text;
}
