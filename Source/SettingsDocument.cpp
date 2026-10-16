#include "SettingsDocument.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{
bool sameBytes(const Bytes& left, const Bytes& right, std::size_t offset, std::size_t size)
{
    return offset + size <= left.size()
        && offset + size <= right.size()
        && std::memcmp(left.data() + offset, right.data() + offset, size) == 0;
}
}

void SettingsDocument::load(const Bytes& snapshot)
{
    if (snapshot.size() != layout::snapshotSize)
        throw std::runtime_error("Control configuration must contain exactly 6282 bytes");
    ControlConfiguration::parse(snapshot);
    original = snapshot;
    working = snapshot;
}

void SettingsDocument::clear()
{
    original.clear();
    working.clear();
}

ControlConfiguration SettingsDocument::configuration() const
{
    return ControlConfiguration::parse(working);
}

std::optional<SettingsDocument::RecordRange> SettingsDocument::recordRange(ControlSelection selection,
                                                                           DeviceProfile profile)
{
    if (selection.index < 0)
        return std::nullopt;

    const auto base = static_cast<std::size_t>(profile) * layout::profileSize;
    switch (selection.kind)
    {
        case ControlKind::knob:
            if (selection.index >= static_cast<int>(layout::knobCount))
                return std::nullopt;
            return RecordRange { base + layout::knobsOffset + static_cast<std::size_t>(selection.index) * layout::knobSize,
                                 layout::knobSize };
        case ControlKind::slider:
            if (selection.index >= static_cast<int>(layout::sliderCount))
                return std::nullopt;
            return RecordRange { base + layout::slidersOffset + static_cast<std::size_t>(selection.index) * layout::sliderSize,
                                 layout::sliderSize };
        case ControlKind::trackButton:
            if (selection.index >= 32)
                return std::nullopt;
            return RecordRange { base + static_cast<std::size_t>(selection.index) * layout::buttonSize,
                                 layout::buttonSize };
        case ControlKind::globalButton:
            if (selection.index >= 11)
                return std::nullopt;
            return RecordRange { base + static_cast<std::size_t>(32 + selection.index) * layout::buttonSize,
                                 layout::buttonSize };
    }
    return std::nullopt;
}

std::optional<std::size_t> SettingsDocument::fieldOffset(ControlKind kind, EditableField field)
{
    switch (kind)
    {
        case ControlKind::knob:
            switch (field)
            {
                case EditableField::channel: return 2;
                case EditableField::cc: return 3;
                case EditableField::minimum: return 4;
                case EditableField::maximum: return 5;
                case EditableField::value1:
                case EditableField::value2:
                case EditableField::led: return std::nullopt;
            }
        case ControlKind::slider:
            switch (field)
            {
                case EditableField::channel: return 1;
                case EditableField::cc: return 2;
                case EditableField::minimum: return 3;
                case EditableField::maximum: return 4;
                case EditableField::value1:
                case EditableField::value2:
                case EditableField::led: return std::nullopt;
            }
        case ControlKind::trackButton:
        case ControlKind::globalButton:
            switch (field)
            {
                case EditableField::channel: return 1;
                case EditableField::cc: return 2;
                case EditableField::value1: return 3;
                case EditableField::value2: return 4;
                case EditableField::led: return 70;
                case EditableField::minimum:
                case EditableField::maximum: return std::nullopt;
            }
    }
    return std::nullopt;
}

bool SettingsDocument::apply(ControlSelection selection,
                             DeviceProfile profile,
                             EditableField field,
                             std::uint8_t value,
                             std::string& error)
{
    error.clear();
    if (!loaded())
    {
        error = "Configuration is not loaded.";
        return false;
    }
    if (profile != DeviceProfile::ccUser)
    {
        error = "DAW profile is read-only.";
        return false;
    }

    const auto range = recordRange(selection, profile);
    const auto localOffset = fieldOffset(selection.kind, field);
    if (!range || !localOffset || range->offset + range->size > working.size() || *localOffset >= range->size)
    {
        error = "This field cannot be edited.";
        return false;
    }

    if (field == EditableField::minimum || field == EditableField::maximum)
    {
        const auto minimumOffset = fieldOffset(selection.kind, EditableField::minimum);
        const auto maximumOffset = fieldOffset(selection.kind, EditableField::maximum);
        if (!minimumOffset || !maximumOffset)
        {
            error = "This field cannot be edited.";
            return false;
        }
        const auto minimum = field == EditableField::minimum ? value : working[range->offset + *minimumOffset];
        const auto maximum = field == EditableField::maximum ? value : working[range->offset + *maximumOffset];
        if (minimum > maximum)
        {
            error = "Min cannot exceed Max.";
            return false;
        }
    }

    working[range->offset + *localOffset] = value;
    return true;
}

void SettingsDocument::revert(ControlSelection selection, DeviceProfile profile)
{
    const auto range = recordRange(selection, profile);
    if (!range || range->offset + range->size > working.size() || range->offset + range->size > original.size())
        return;
    std::copy_n(original.begin() + static_cast<std::ptrdiff_t>(range->offset),
                range->size,
                working.begin() + static_cast<std::ptrdiff_t>(range->offset));
}

bool SettingsDocument::isDirty() const
{
    return loaded() && working != original;
}

bool SettingsDocument::isRecordDirty(ControlSelection selection, DeviceProfile profile) const
{
    if (!loaded())
        return false;
    const auto range = recordRange(selection, profile);
    if (!range)
        return false;
    return !sameBytes(working, original, range->offset, range->size);
}

std::vector<ControlSelection> SettingsDocument::dirtyControls() const
{
    std::vector<ControlSelection> dirty;
    const ControlSelection controls[] = {
        { ControlKind::knob, 0 }, { ControlKind::knob, 1 }, { ControlKind::knob, 2 }, { ControlKind::knob, 3 },
        { ControlKind::knob, 4 }, { ControlKind::knob, 5 }, { ControlKind::knob, 6 }, { ControlKind::knob, 7 },
        { ControlKind::slider, 0 }, { ControlKind::slider, 1 }, { ControlKind::slider, 2 }, { ControlKind::slider, 3 },
        { ControlKind::slider, 4 }, { ControlKind::slider, 5 }, { ControlKind::slider, 6 }, { ControlKind::slider, 7 }
    };
    for (const auto& control : controls)
        if (isRecordDirty(control, DeviceProfile::daw) || isRecordDirty(control, DeviceProfile::ccUser))
            dirty.push_back(control);

    for (int index = 0; index < 32; ++index)
    {
        const ControlSelection control { ControlKind::trackButton, index };
        if (isRecordDirty(control, DeviceProfile::daw) || isRecordDirty(control, DeviceProfile::ccUser))
            dirty.push_back(control);
    }
    for (int index = 0; index < 11; ++index)
    {
        const ControlSelection control { ControlKind::globalButton, index };
        if (isRecordDirty(control, DeviceProfile::daw) || isRecordDirty(control, DeviceProfile::ccUser))
            dirty.push_back(control);
    }
    return dirty;
}

std::vector<SettingsSpan> SettingsDocument::dirtySpans() const
{
    std::vector<SettingsSpan> spans;
    const auto append = [&](ControlSelection selection, DeviceProfile profile) {
        if (!isRecordDirty(selection, profile))
            return;
        const auto range = recordRange(selection, profile);
        if (!range)
            return;
        SettingsSpan span;
        span.address = static_cast<std::uint32_t>(range->offset);
        span.data.assign(working.begin() + static_cast<std::ptrdiff_t>(range->offset),
                         working.begin() + static_cast<std::ptrdiff_t>(range->offset + range->size));
        spans.push_back(std::move(span));
    };

    for (const auto profile : { DeviceProfile::daw, DeviceProfile::ccUser })
    {
        for (int index = 0; index < 32; ++index)
            append({ ControlKind::trackButton, index }, profile);
        for (int index = 0; index < 11; ++index)
            append({ ControlKind::globalButton, index }, profile);
        for (int index = 0; index < 8; ++index)
            append({ ControlKind::knob, index }, profile);
        for (int index = 0; index < 8; ++index)
            append({ ControlKind::slider, index }, profile);
    }
    return spans;
}

void SettingsDocument::noteVerified(const std::vector<SettingsSpan>& spans)
{
    if (!loaded())
        throw std::runtime_error("Configuration is not loaded");

    for (const auto& span : spans)
    {
        const auto offset = static_cast<std::size_t>(span.address);
        if (span.data.empty() || offset + span.data.size() > working.size())
            throw std::runtime_error("Verified bytes fall outside the snapshot");
        if (!std::equal(span.data.begin(), span.data.end(), working.begin() + static_cast<std::ptrdiff_t>(offset)))
            throw std::runtime_error("Verified bytes do not match the current edit");
    }

    for (const auto& span : spans)
    {
        const auto offset = static_cast<std::size_t>(span.address);
        std::copy(span.data.begin(), span.data.end(), original.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}
