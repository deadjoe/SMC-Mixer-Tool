#pragma once

#include "ControlConfiguration.h"

#include <optional>
#include <string>
#include <vector>

class SettingsDocument
{
public:
    void load(const Bytes& snapshot);
    void clear();
    bool loaded() const noexcept { return working.size() == layout::snapshotSize; }

    const Bytes& workingBytes() const noexcept { return working; }
    ControlConfiguration configuration() const;

    bool apply(ControlSelection selection,
               DeviceProfile profile,
               EditableField field,
               std::uint8_t value,
               std::string& error);
    void revert(ControlSelection selection, DeviceProfile profile);

    bool isDirty() const;
    bool isRecordDirty(ControlSelection selection, DeviceProfile profile) const;
    std::vector<ControlSelection> dirtyControls() const;
    std::vector<SettingsSpan> dirtySpans() const;
    void noteVerified(const std::vector<SettingsSpan>& spans);

private:
    struct RecordRange
    {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    static std::optional<RecordRange> recordRange(ControlSelection selection, DeviceProfile profile);
    static std::optional<std::size_t> fieldOffset(ControlKind kind, EditableField field);
    static std::uint8_t fieldMaximum(ControlKind kind, EditableField field);

    Bytes original;
    Bytes working;
};
