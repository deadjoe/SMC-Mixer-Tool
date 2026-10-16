#pragma once

#include "../ControlConfiguration.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <optional>

class ControlInspectorComponent final : public juce::Component
{
public:
    ControlInspectorComponent();

    std::function<void()> onSettingsRequired;

    void selectControl(ControlSelection selection);
    void setConfiguration(const ControlConfiguration& configuration);
    void clearConfiguration();
    void setLoading(bool loading, double progress = 0.0);
    void setReadError(const juce::String& message);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void refreshFields();
    void updateProfileButtons();
    void setField(int index, const juce::String& name, const juce::String& value, bool visible = true);
    void hideFields();

    juce::Label titleLabel;
    juce::Label selectionLabel;
    juce::Label statusLabel;
    juce::TextButton ccProfileButton { "CC / User" };
    juce::TextButton dawProfileButton { "DAW" };
    std::array<juce::Label, 6> fieldNames;
    std::array<juce::Label, 6> fieldValues;
    std::optional<ControlConfiguration> configuration;
    ControlSelection selected;
    DeviceProfile activeProfile = DeviceProfile::ccUser;
    juce::String readError;
    bool hasSelection = false;
    bool loading = false;
    double loadProgress = 0.0;
};
