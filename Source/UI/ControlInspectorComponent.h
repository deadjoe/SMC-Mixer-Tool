#pragma once

#include "../ControlDescription.h"
#include "MixerLookAndFeel.h"
#include "Widgets.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <optional>

// The selected control's stored settings. Fields keep their place and update in place, so an
// edit, or the document's answer to it, never rebuilds the field under the pointer.
class ControlInspectorComponent final : public juce::Component
{
public:
    ControlInspectorComponent();

    std::function<void(ControlSelection, DeviceProfile, EditableField, std::uint8_t, bool continuing)> onControlEdited;
    std::function<void(ControlSelection, DeviceProfile)> onRevertRequested;
    std::function<void()> onReadRequested;
    std::function<void(const juce::String&, mixerui::Tone)> onNotice;

    struct View
    {
        const ControlProfile* profile = nullptr; // nullptr until the settings are read
        DeviceProfile profileKind = DeviceProfile::ccUser;
        std::optional<ControlSelection> selection;
        bool editable = false;
        bool selectionDirty = false;
        juce::String emptyMessage; // shown while there is no profile
        bool offerRead = false;    // shows Read settings next to the empty message
    };

    void show(const View& view);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    static constexpr int preferredHeight = 190;
    static constexpr int slotCount = 6;

    // For tests: the field editing a value, or nullptr when that field is not shown.
    NumberField* numberFieldFor(EditableField field);
    juce::ComboBox& choiceBox() noexcept { return choice; }
    juce::TextButton& revertButton() noexcept { return revert; }
    juce::String noteText() const { return note.getText(); }

private:
    struct Slot
    {
        juce::Label caption;
        juce::Label helper;
        NumberField number;
        std::optional<EditableField> field;
        bool visible = false;
    };

    void configureChoice(ControlKind kind);
    void showNumber(int index, EditableField field, const juce::String& caption, const juce::String& helper,
                    bool used, int displayOffset, bool listed);
    void showReadOnly(int index, const juce::String& caption, int value, const juce::String& helper);
    void hideSlot(int index);
    void edit(EditableField field, int value, bool continuing);

    View current;
    std::optional<ControlKind> choiceKind;
    juce::Label kindLabel;
    juce::Label titleLabel;
    juce::Label note;
    juce::Label readOnlyBadge;
    juce::TextButton revert { "Revert" };
    juce::TextButton readButton { "Read settings" };
    juce::Label choiceCaption;
    juce::Label choiceHelper;
    juce::ComboBox choice;
    std::array<Slot, slotCount - 1> slots;
    bool updating = false;
};
