#pragma once

#include "../DeviceModel.h"
#include "MixerLookAndFeel.h"

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <optional>

class DiagnosticsComponent final : public juce::Component
{
public:
    DiagnosticsComponent();

    std::function<void()> onReadSettings;
    std::function<void(const juce::String&, mixerui::Tone)> onNotice;

    void setActiveTransport(TransportKind transport);
    void refreshEndpoints();
    void setSnapshot(const DeviceSnapshot& snapshot);
    void clearSnapshot();
    void setBusy(bool busy);
    void setSettings(const SettingsReadResult& settings);
    void adoptWorkingImage(const Bytes& image);
    void setLastError(const juce::String& message);
    void exportSettings();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void updateEnabledState();
    void render();
    juce::String linkReport() const;

    juce::TextEditor reportEditor;
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::TextButton readButton { "Read settings" };
    juce::TextButton exportButton { juce::String::fromUTF8("Export image\xe2\x80\xa6") };
    bool controlsBusy = false;
    juce::String lastError;
    TransportKind activeTransport = TransportKind::usbC;
    bool settingsFromRead = false;
    std::optional<DeviceSnapshot> snapshot;
    std::optional<SettingsReadResult> settings;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> reportCard;
};
