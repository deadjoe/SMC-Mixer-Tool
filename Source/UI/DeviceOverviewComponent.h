#pragma once

#include "../DeviceModel.h"
#include "ControlInspectorComponent.h"
#include "DeviceDiagramComponent.h"

#include <JuceHeader.h>

#include <functional>

class DeviceOverviewComponent final : public juce::Component
{
public:
    DeviceOverviewComponent();

    std::function<void()> onSettingsRequired;

    void setSnapshot(const DeviceSnapshot& snapshot);
    void setSettings(const SettingsReadResult& settings);
    void setSettingsLoading(bool loading, double progress = 0.0);
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
