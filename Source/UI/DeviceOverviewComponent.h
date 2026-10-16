#pragma once

#include "../DeviceModel.h"
#include "ControlInspectorComponent.h"
#include "DeviceDiagramComponent.h"

#include <JuceHeader.h>

#include <functional>
#include <vector>

class DeviceOverviewComponent final : public juce::Component
{
public:
    DeviceOverviewComponent();

    std::function<void()> onSettingsRequired;
    std::function<void(ControlSelection, DeviceProfile, EditableField, std::uint8_t)> onControlEdited;
    std::function<void(ControlSelection, DeviceProfile)> onRevertRequested;
    std::function<void()> onWriteRequested;
    std::function<bool(ControlSelection, DeviceProfile)> isRecordDirty;

    void setSnapshot(const DeviceSnapshot& snapshot);
    void showWorkingCopy(const ControlConfiguration& configuration,
                         const std::vector<ControlSelection>& dirty,
                         bool editingEnabled);
    void cacheWorkingCopy(const ControlConfiguration& configuration,
                          const std::vector<ControlSelection>& dirty);
    void setSettingsLoading(bool loading, double progress = 0.0);
    void setSettingsWriting(bool writing, double progress = 0.0);
    void setSettingsError(const juce::String& message);
    void clear(TransportKind transport);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void styleValue(juce::Label& label, float fontSize = 14.0f);

    DeviceDiagramComponent diagram;
    ControlInspectorComponent inspector;
    juce::Label deviceValue;
    juce::Label firmwareValue;
    juce::Label transportValue;
    juce::Label modeValue;
    juce::Label manufacturerValue;
    juce::Label modelValue;
    juce::Label batteryValue;
    juce::Label rssiValue;
    juce::Label sourceValue;
    juce::Label destinationValue;
    juce::Label warningLabel;
    double batteryLevel = 0.0;
    juce::ProgressBar batteryBar;

    juce::Rectangle<int> identityCard;
    juce::Rectangle<int> inspectorArea;
};
