#include "ProfileJson.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
// Copies one whole-number property into a stored byte. Missing properties are left alone.
juce::Result take(const juce::var& object, const char* key, std::uint8_t& target, const juce::String& where, int offset = 0)
{
    const auto value = object.getProperty(key, juce::var());
    if (value.isVoid())
        return juce::Result::ok();
    if (!(value.isInt() || value.isInt64() || (value.isDouble() && static_cast<double>(value) == std::floor(static_cast<double>(value)))))
        return juce::Result::fail(where + ": \"" + key + "\" must be a whole number.");
    const auto number = static_cast<juce::int64>(value) - offset;
    if (number < 0 || number > 255)
        return juce::Result::fail(where + ": \"" + key + "\" is out of range.");
    target = static_cast<std::uint8_t>(number);
    return juce::Result::ok();
}

juce::Result takeMessage(const juce::var& object, Bytes& target, const juce::String& where)
{
    const auto value = object.getProperty("message", juce::var());
    if (value.isVoid())
        return juce::Result::ok();
    if (!value.isString())
        return juce::Result::fail(where + ": \"message\" must be hex bytes such as \"F0 7F F7\".");
    Bytes bytes;
    for (const auto& token : juce::StringArray::fromTokens(value.toString(), " ", ""))
    {
        if (token.isEmpty())
            continue;
        if (token.length() != 2 || !token.containsOnly("0123456789abcdefABCDEF"))
            return juce::Result::fail(where + ": \"message\" must be hex bytes such as \"F0 7F F7\".");
        bytes.push_back(static_cast<std::uint8_t>(token.getHexValue32()));
    }
    target = std::move(bytes);
    return juce::Result::ok();
}

const juce::Array<juce::var>* arrayOf(const juce::var& root, const char* key)
{
    return root.getProperty(key, juce::var()).getArray();
}

// Finds the position a list entry describes: its "index" or "track" and "name", else its place.
int positionOf(const juce::var& entry, int place, int count, const char* const* names, int nameCount, bool tracks)
{
    if (tracks)
    {
        const auto track = static_cast<int>(entry.getProperty("track", 0));
        const auto name = entry.getProperty("name", "").toString();
        for (int slot = 0; slot < nameCount; ++slot)
            if (track >= 1 && name.equalsIgnoreCase(names[slot]))
                return (track - 1) * nameCount + slot;
        return place;
    }
    if (names != nullptr)
    {
        const auto name = entry.getProperty("name", "").toString();
        for (int slot = 0; slot < nameCount; ++slot)
            if (name.equalsIgnoreCase(names[slot]))
                return slot;
        return place;
    }
    const auto index = static_cast<int>(entry.getProperty("index", 0));
    return index >= 1 && index <= count ? index - 1 : place;
}
}

juce::Result profilejson::read(const juce::String& text, ControlProfile& profile)
{
    juce::var root;
    const auto parsed = juce::JSON::parse(text, root);
    if (parsed.failed())
        return juce::Result::fail("The file is not valid JSON: " + parsed.getErrorMessage());
    if (!root.isObject())
        return juce::Result::fail("The file is not an SMC Mixer Tool profile.");
    if (arrayOf(root, "knobs") == nullptr && arrayOf(root, "faders") == nullptr
        && arrayOf(root, "trackButtons") == nullptr && arrayOf(root, "globalButtons") == nullptr)
        return juce::Result::fail("The file has no knobs, faders, or buttons to import.");

    auto result = profile;
    const auto check = [](const juce::Result& step, bool& ok, juce::Result& first) {
        if (step.failed() && ok)
        {
            ok = false;
            first = step;
        }
    };
    bool ok = true;
    auto first = juce::Result::ok();

    if (const auto* knobs = arrayOf(root, "knobs"))
        for (int place = 0; place < knobs->size(); ++place)
        {
            const auto& entry = knobs->getReference(place);
            const auto index = positionOf(entry, place, static_cast<int>(layout::knobCount), nullptr, 0, false);
            if (index >= static_cast<int>(layout::knobCount))
                return juce::Result::fail("The file lists more than " + juce::String(static_cast<int>(layout::knobCount)) + " knobs.");
            auto& knob = result.knobs[static_cast<std::size_t>(index)];
            const auto where = "Knob " + juce::String(index + 1);
            check(take(entry, "type", knob.type, where), ok, first);
            check(take(entry, "speed", knob.speed, where), ok, first);
            check(take(entry, "channel", knob.channel, where, 1), ok, first);
            check(take(entry, "cc", knob.cc, where), ok, first);
            check(take(entry, "minimum", knob.minimum, where), ok, first);
            check(take(entry, "maximum", knob.maximum, where), ok, first);
        }

    if (const auto* faders = arrayOf(root, "faders"))
        for (int place = 0; place < faders->size(); ++place)
        {
            const auto& entry = faders->getReference(place);
            const auto index = positionOf(entry, place, static_cast<int>(layout::sliderCount), nullptr, 0, false);
            if (index >= static_cast<int>(layout::sliderCount))
                return juce::Result::fail("The file lists more than " + juce::String(static_cast<int>(layout::sliderCount)) + " faders.");
            auto& slider = result.sliders[static_cast<std::size_t>(index)];
            const auto where = "Fader " + juce::String(index + 1);
            check(take(entry, "type", slider.type, where), ok, first);
            check(take(entry, "channel", slider.channel, where, 1), ok, first);
            check(take(entry, "cc", slider.cc, where), ok, first);
            check(take(entry, "minimum", slider.minimum, where), ok, first);
            check(take(entry, "maximum", slider.maximum, where), ok, first);
        }

    const auto readButtons = [&](const char* key, ControlKind kind, int count) -> juce::Result {
        const auto* list = arrayOf(root, key);
        if (list == nullptr)
            return juce::Result::ok();
        static const char* const trackNames[] { "Mute", "Solo", "Record", "Select" };
        std::vector<std::string> globalNames;
        std::vector<const char*> globalPointers;
        for (int index = 0; index < static_cast<int>(layout::globalButtonCount); ++index)
            globalNames.push_back(globalButtonFullName(index));
        for (const auto& name : globalNames)
            globalPointers.push_back(name.c_str());

        for (int place = 0; place < list->size(); ++place)
        {
            const auto& entry = list->getReference(place);
            const auto tracks = kind == ControlKind::trackButton;
            const auto index = tracks ? positionOf(entry, place, count, trackNames, 4, true)
                                      : positionOf(entry, place, count, globalPointers.data(), static_cast<int>(globalPointers.size()), false);
            if (index >= count)
                return juce::Result::fail(juce::String("The file lists too many ") + (tracks ? "track" : "global") + " buttons.");
            const ControlSelection control { kind, index };
            auto& button = result.buttons[buttonRecordIndex(control)];
            const auto where = juce::String::fromUTF8(controlDisplayName(control).c_str());
            check(take(entry, "mode", button.mode, where), ok, first);
            check(take(entry, "channel", button.channel, where, 1), ok, first);
            check(take(entry, "cc", button.cc, where), ok, first);
            check(take(entry, "value1", button.value1, where), ok, first);
            check(take(entry, "value2", button.value2, where), ok, first);
            check(take(entry, "led", button.led, where), ok, first);
            check(takeMessage(entry, button.dawMessage, where), ok, first);
        }
        return juce::Result::ok();
    };
    if (const auto tracks = readButtons("trackButtons", ControlKind::trackButton, static_cast<int>(layout::trackButtonCount)); tracks.failed())
        return tracks;
    if (const auto globals = readButtons("globalButtons", ControlKind::globalButton, static_cast<int>(layout::globalButtonCount)); globals.failed())
        return globals;

    if (!ok)
        return first;
    profile = result;
    return juce::Result::ok();
}
