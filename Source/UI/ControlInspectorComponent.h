#pragma once

#include "../ControlConfiguration.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ControlInspectorComponent final : public juce::Component
{
public:
    ControlInspectorComponent();

    std::function<void()> onSettingsRequired;
    std::function<void(ControlSelection, DeviceProfile, EditableField, std::uint8_t)> onControlEdited;
    std::function<void(ControlSelection, DeviceProfile)> onRevertRequested;
    std::function<void()> onWriteRequested;
    std::function<bool(ControlSelection, DeviceProfile)> isRecordDirty;

    void selectControl(ControlSelection selection);
    void setConfiguration(const ControlConfiguration& configuration);
    void cacheConfiguration(const ControlConfiguration& configuration);
    void setDirtyControls(const std::vector<ControlSelection>& dirty);
    void setEditingEnabled(bool enabled);
    void clearConfiguration();
    void setLoading(bool loading, double progress = 0.0);
    void setWriting(bool writing, double progress = 0.0);
    void setReadError(const juce::String& message);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void refreshFields();
    void updateProfileButtons();
    void copyProfile();
    void exportProfile();
    void showNotice(const juce::String& message, juce::Colour colour);
    void updateStatus();
    void updateActionButtons();
    void setField(int index, const juce::String& name, const juce::String& value, bool visible = true);
    void setChoice(int index, const juce::String& name, EditableField field, std::uint8_t value,
                   const std::vector<std::uint8_t>& values, const juce::String& tooltip);
    void setEditor(int index, const juce::String& name, EditableField field, std::uint8_t value, int maximum, bool asChannel);
    void hideFields();
    void handleEditor(int index);
    bool selectedRecordDirty() const;
    bool anyDirty() const;

    juce::Label titleLabel;
    juce::Label selectionLabel;
    juce::Label statusLabel;
    juce::TextButton revertButton { "Revert" };
    juce::TextButton writeButton { "Write" };
    juce::TextButton exportButton { "Export" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton ccUserButton { "CC / User" };
    juce::TextButton dawButton { "DAW" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::array<juce::Label, 6> fieldNames;
    std::array<juce::Label, 6> fieldValues;
    std::array<juce::Slider, 6> editors;
    juce::ComboBox choiceBox;
    std::array<std::optional<EditableField>, 6> editorFields;
    std::optional<EditableField> choiceField;
    int choiceIndex = 0;
    std::optional<ControlConfiguration> configuration;
    std::vector<ControlSelection> dirtyControls;
    ControlSelection selected;
    DeviceProfile activeProfile = DeviceProfile::ccUser;
    juce::String readError;
    bool hasSelection = false;
    bool loading = false;
    bool writing = false;
    bool editingEnabled = true;
    bool suppressEditorCallbacks = false;
    bool suppressChoiceCallbacks = false;
    double loadProgress = 0.0;
    double writeProgress = 0.0;
};
