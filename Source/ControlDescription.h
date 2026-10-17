#pragma once

#include "ControlConfiguration.h"

#include <string>

// What each stored field means for a type or mode, as measured on the mixer's Master port.
// The README lists the same measurements.
struct FieldMeaning
{
    bool ccIsController = true; // the CC byte goes out as a controller number
    bool ccUsed = true;
    std::string ccName = "CC";
    bool rangeUsed = true;
    std::string minimumName = "Min";
    std::string maximumName = "Max";
    bool value1Used = true;
    bool value2Used = true;
    std::string value1Name = "Value 1";
    std::string value2Name = "Value 2";
    std::string note;
};

FieldMeaning knobMeaning(std::uint8_t type);
FieldMeaning sliderMeaning(std::uint8_t type);
FieldMeaning buttonMeaning(std::uint8_t mode);
FieldMeaning meaningFor(const ControlProfile& profile, ControlSelection selection);

// One sentence on what the control sends, with a Custom button's stored message.
std::string controlNote(const ControlProfile& profile, ControlSelection selection);

// A compact label for the panel: the CC or note number when one is sent, else the type.
struct ControlSummary
{
    std::string primary;   // "102", "AT", "PB", "PC"
    std::string secondary; // "Rel · Ch 1"
    bool hasRange = false; // Min and Max bound the value sent
    int rangeMinimum = 0;
    int rangeMaximum = 127;
    bool relative = false;
};

ControlSummary summarize(const ControlProfile& profile, ControlSelection selection);
// A full description for a tooltip: name, type, channel, and every field in use.
std::string describe(const ControlProfile& profile, ControlSelection selection);
