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

    void setActiveTransport(TransportKind transport);
    void refreshEndpoints();
    void setSnapshot(const DeviceSnapshot& snapshot);
    void clearSnapshot();
    void setBusy(bool busy, double progress, bool settingsOperation);
    void setSettings(const SettingsReadResult& settings);
    void adoptWorkingImage(const Bytes& image);
    void setError(const juce::String& message);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void exportSettings();
    void updateEnabledState();
    void render();
    juce::String linkReport() const;

    juce::TextEditor reportEditor;
    juce::TextButton readButton { "Read snapshot" };
    juce::TextButton exportButton { "Export .bin" };
    juce::Label statusLabel;
    juce::Label settingsSummary;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    bool controlsBusy = false;
    bool readingSettings = false;
    TransportKind activeTransport = TransportKind::usbC;
    bool settingsFromRead = false;
    std::optional<DeviceSnapshot> snapshot;
    std::optional<SettingsReadResult> settings;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> reportCard;
    juce::Rectangle<int> settingsCard;
};
