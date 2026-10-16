#pragma once

#include "../DeviceModel.h"

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <optional>

class DiagnosticsComponent final : public juce::Component
{
public:
    DiagnosticsComponent();

    std::function<void()> onReadSettings;

    void refreshEndpoints();
    void setSnapshot(const DeviceSnapshot& snapshot);
    void clearSnapshot();
    void setBusy(bool busy, double progress, bool settingsOperation);
    void setSettings(const SettingsReadResult& settings);
    void setError(const juce::String& message);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void exportSettings();
    void updateEnabledState();

    juce::TextEditor endpointsEditor;
    juce::TextEditor protocolEditor;
    juce::TextButton readButton { "Read settings" };
    juce::TextButton exportButton { "Export .bin" };
    juce::Label statusLabel;
    juce::Label settingsSummary;
    juce::Label rawPrefixLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    bool controlsBusy = false;
    bool readingSettings = false;
    std::optional<SettingsReadResult> settings;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> endpointsCard;
    juce::Rectangle<int> protocolCard;
    juce::Rectangle<int> settingsCard;
};
