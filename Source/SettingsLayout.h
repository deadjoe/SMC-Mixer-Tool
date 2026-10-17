#pragma once

#include <cstddef>
#include <cstdint>

// The settings image as the mixer stores it: the DAW profile, then the CC / User profile.
// Each profile holds 43 button records, then 8 knob records, then 8 fader records.
namespace layout
{
constexpr std::size_t profileCount = 2;
constexpr std::size_t profileSize = 3141;
constexpr std::size_t snapshotSize = profileSize * profileCount;

constexpr std::size_t trackButtonCount = 32; // four per channel: Mute, Solo, Record, Select
constexpr std::size_t globalButtonCount = 11;
constexpr std::size_t buttonCount = trackButtonCount + globalButtonCount;
constexpr std::size_t buttonSize = 71;
constexpr std::size_t buttonMessageCapacity = 64;

constexpr std::size_t channelCount = 8;
constexpr std::size_t knobCount = channelCount;
constexpr std::size_t knobSize = 6;
constexpr std::size_t sliderCount = channelCount;
constexpr std::size_t sliderSize = 5;

constexpr std::size_t knobsOffset = buttonCount * buttonSize;
constexpr std::size_t slidersOffset = knobsOffset + knobCount * knobSize;
static_assert(slidersOffset + sliderCount * sliderSize == profileSize, "Records fill the profile exactly");

// Flash type 5 addresses the settings image in the vendor read and write commands.
constexpr std::uint8_t settingsFlash = 5;
// The mixer answers a full read in pieces; this size splits 6,282 bytes into seven requests.
constexpr std::uint32_t readChunkSize = 1009;
// A single vendor write carries at most this many data bytes.
constexpr std::size_t maximumWriteSize = 256;
}
