#include "SettingsDocument.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
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
    // parse() checks the size and every button message before anything is replaced.
    ControlConfiguration::parse(snapshot);
    original = snapshot;
    working = snapshot;
    undoStack.clear();
    redoStack.clear();
    lastEdit.reset();
}

void SettingsDocument::clear()
{
    original.clear();
    working.clear();
    undoStack.clear();
    redoStack.clear();
    lastEdit.reset();
}

ControlConfiguration SettingsDocument::configuration() const
{
    return ControlConfiguration::parse(working);
}

std::optional<SettingsDocument::RecordRange> SettingsDocument::recordRange(ControlSelection selection,
                                                                           DeviceProfile profile)
{
    if (!isValidControl(selection))
        return std::nullopt;

    const auto base = static_cast<std::size_t>(profile) * layout::profileSize;
    const auto index = static_cast<std::size_t>(selection.index);
    switch (selection.kind)
    {
        case ControlKind::knob:
            return RecordRange { base + layout::knobsOffset + index * layout::knobSize, layout::knobSize };
        case ControlKind::slider:
            return RecordRange { base + layout::slidersOffset + index * layout::sliderSize, layout::sliderSize };
        case ControlKind::trackButton:
        case ControlKind::globalButton:
            return RecordRange { base + buttonRecordIndex(selection) * layout::buttonSize, layout::buttonSize };
    }
    return std::nullopt;
}

std::optional<std::size_t> SettingsDocument::fieldOffset(ControlKind kind, EditableField field)
{
    if (!hasField(kind, field))
        return std::nullopt;

    // Byte positions inside each record. hasField() above has ruled out the rest.
    const auto at = [field](std::initializer_list<std::pair<EditableField, std::size_t>> offsets) -> std::optional<std::size_t> {
        for (const auto& [candidate, offset] : offsets)
            if (candidate == field)
                return offset;
        return std::nullopt;
    };
    switch (kind)
    {
        case ControlKind::knob:
            return at({ { EditableField::type, 0 }, { EditableField::channel, 2 }, { EditableField::cc, 3 },
                        { EditableField::minimum, 4 }, { EditableField::maximum, 5 } });
        case ControlKind::slider:
            return at({ { EditableField::type, 0 }, { EditableField::channel, 1 }, { EditableField::cc, 2 },
                        { EditableField::minimum, 3 }, { EditableField::maximum, 4 } });
        case ControlKind::trackButton:
        case ControlKind::globalButton:
            return at({ { EditableField::mode, 0 }, { EditableField::channel, 1 }, { EditableField::cc, 2 },
                        { EditableField::value1, 3 }, { EditableField::value2, 4 },
                        { EditableField::led, layout::buttonSize - 1 } });
    }
    return std::nullopt;
}

void SettingsDocument::remember()
{
    undoStack.push_back(working);
    if (undoStack.size() > undoLimit)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

EditResult SettingsDocument::apply(ControlSelection selection,
                                   DeviceProfile profile,
                                   EditableField field,
                                   std::uint8_t value,
                                   bool continuingGesture)
{
    if (!loaded())
        return { false, "Configuration is not loaded." };
    if (profile != DeviceProfile::ccUser)
        return { false, "DAW profile is read-only." };

    const auto range = recordRange(selection, profile);
    const auto localOffset = fieldOffset(selection.kind, field);
    if (!range || !localOffset)
        return { false, "This field cannot be edited." };

    const auto current = configuration().profile(profile);
    if (!fieldLimits(selection.kind, field).contains(value))
    {
        const auto limit = fieldLimits(selection.kind, field).maximum;
        return { false, field == EditableField::channel ? std::string("Channel must be 1 to 16.")
                                                        : "Value must be 0 to " + std::to_string(limit) + "." };
    }
    if (!allowedRange(current, selection, field).contains(value))
        return { false, field == EditableField::minimum ? "Min cannot exceed Max." : "Max cannot be below Min." };

    const EditKey key { selection, profile, field };
    const auto offset = range->offset + *localOffset;
    if (working[offset] == value)
        return { true, {} };

    if (!(continuingGesture && lastEdit && *lastEdit == key && !undoStack.empty()))
        remember();
    lastEdit = key;
    working[offset] = value;

    // Leaving the relative type turns Min and Max back into a range. Two values sent per
    // step, such as 65 and 1, would otherwise leave Min above Max with no way to edit Min.
    EditResult result { true, {} };
    if (field == EditableField::type && selection.kind == ControlKind::knob && value != knobTypeRelative)
    {
        const auto minimumAt = range->offset + *fieldOffset(selection.kind, EditableField::minimum);
        const auto maximumAt = range->offset + *fieldOffset(selection.kind, EditableField::maximum);
        if (working[minimumAt] > working[maximumAt])
        {
            std::swap(working[minimumAt], working[maximumAt]);
            result.message = "Min and Max were swapped so that Min is not above Max.";
        }
    }
    return result;
}

void SettingsDocument::revert(ControlSelection selection, DeviceProfile profile)
{
    const auto range = recordRange(selection, profile);
    if (!loaded() || !range || sameBytes(working, original, range->offset, range->size))
        return;
    remember();
    lastEdit.reset();
    std::copy_n(original.begin() + static_cast<std::ptrdiff_t>(range->offset),
                range->size,
                working.begin() + static_cast<std::ptrdiff_t>(range->offset));
}

void SettingsDocument::revertAll()
{
    if (!isDirty())
        return;
    remember();
    lastEdit.reset();
    working = original;
}

bool SettingsDocument::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back(std::move(working));
    working = std::move(undoStack.back());
    undoStack.pop_back();
    lastEdit.reset();
    return true;
}

bool SettingsDocument::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back(std::move(working));
    working = std::move(redoStack.back());
    redoStack.pop_back();
    lastEdit.reset();
    return true;
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
    return range && !sameBytes(working, original, range->offset, range->size);
}

std::vector<ControlSelection> SettingsDocument::dirtyControls() const
{
    std::vector<ControlSelection> dirty;
    for (const auto& control : allControls())
        if (isRecordDirty(control, DeviceProfile::daw) || isRecordDirty(control, DeviceProfile::ccUser))
            dirty.push_back(control);
    return dirty;
}

std::vector<SettingsSpan> SettingsDocument::dirtySpans() const
{
    std::vector<SettingsSpan> spans;
    for (const auto profile : { DeviceProfile::daw, DeviceProfile::ccUser })
    {
        for (const auto& control : allControls())
        {
            if (!isRecordDirty(control, profile))
                continue;
            const auto range = recordRange(control, profile);
            SettingsSpan span;
            span.address = static_cast<std::uint32_t>(range->offset);
            span.data.assign(working.begin() + static_cast<std::ptrdiff_t>(range->offset),
                             working.begin() + static_cast<std::ptrdiff_t>(range->offset + range->size));
            spans.push_back(std::move(span));
        }
    }
    // Ascending addresses, the order the records sit in the image.
    std::sort(spans.begin(), spans.end(), [](const SettingsSpan& left, const SettingsSpan& right) {
        return left.address < right.address;
    });
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
