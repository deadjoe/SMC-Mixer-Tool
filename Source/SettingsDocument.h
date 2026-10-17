#pragma once

#include "ControlConfiguration.h"

#include <optional>
#include <string>
#include <vector>

// What became of one edit. A refused edit says why; an applied edit may say what else it changed.
struct EditResult
{
    bool applied = false;
    std::string message;
};

// The settings image as read from the mixer, and the working copy the person edits.
class SettingsDocument
{
public:
    void load(const Bytes& snapshot);
    void clear();
    bool loaded() const noexcept { return working.size() == layout::snapshotSize; }

    const Bytes& workingBytes() const noexcept { return working; }
    ControlConfiguration configuration() const;

    // A continuing gesture, such as a slider drag, folds into the previous undo step
    // while it keeps editing the same field.
    EditResult apply(ControlSelection selection,
                     DeviceProfile profile,
                     EditableField field,
                     std::uint8_t value,
                     bool continuingGesture = false);
    void revert(ControlSelection selection, DeviceProfile profile);
    void revertAll();

    // Replaces the CC / User profile in one undoable step, as unsaved edits. Every field is
    // checked first; nothing changes if one is out of range.
    EditResult importProfile(const ControlProfile& profile);
    // Takes the CC / User profile from a whole settings image, byte for byte. The DAW profile
    // in the file is ignored, because it is read-only here.
    EditResult importImage(const Bytes& image);

    bool canUndo() const noexcept { return !undoStack.empty(); }
    bool canRedo() const noexcept { return !redoStack.empty(); }
    bool undo();
    bool redo();

    bool isDirty() const;
    bool isRecordDirty(ControlSelection selection, DeviceProfile profile) const;
    std::vector<ControlSelection> dirtyControls() const;
    std::vector<SettingsSpan> dirtySpans() const;
    void noteVerified(const std::vector<SettingsSpan>& spans);

    static constexpr std::size_t undoLimit = 200;

private:
    struct RecordRange
    {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    struct EditKey
    {
        ControlSelection selection;
        DeviceProfile profile = DeviceProfile::ccUser;
        EditableField field = EditableField::type;

        bool operator==(const EditKey& other) const
        {
            return selection == other.selection && profile == other.profile && field == other.field;
        }
    };

    static std::optional<RecordRange> recordRange(ControlSelection selection, DeviceProfile profile);
    static std::optional<std::size_t> fieldOffset(ControlKind kind, EditableField field);
    void remember();
    EditResult replaceWorking(Bytes next, const std::string& extra);

    Bytes original;
    Bytes working;
    std::vector<Bytes> undoStack;
    std::vector<Bytes> redoStack;
    std::optional<EditKey> lastEdit;
};
