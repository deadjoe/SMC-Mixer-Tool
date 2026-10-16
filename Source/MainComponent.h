#pragma once

#include "DeviceController.h"
#include "SettingsDocument.h"
#include "BuildInfo.h"
#include "UI/DeviceOverviewComponent.h"
#include "UI/DiagnosticsComponent.h"
#include "UI/MixerLookAndFeel.h"

#include <JuceHeader.h>

class GearButton final : public juce::Button
{
public:
    GearButton();
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
};

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    bool hasUnsavedEdits() const;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshDevice();
    void refreshDeviceNow();
    void readSettings();
    void readSettingsNow();
    void editControl(ControlSelection selection, DeviceProfile profile, EditableField field, std::uint8_t value);
    void revertSelected(ControlSelection selection, DeviceProfile profile);
    void writeSettings();
    void publishDocument(bool resetEditors);
    bool confirmDiscard();
    bool editingEnabled() const;
    void handleEvent(DeviceEvent event);
    void setBusy(bool busy, bool settingsOperation);
    void finishBusyState();
    void selectTransport(TransportKind transport);
    void showDiagnostics(bool shouldShowDiagnostics);
    void updateSelectionColours();

    MixerLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 480 };
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label detailLabel;
    juce::Label connectionCaption;
    GearButton gearButton;
    juce::TextButton usbButton { "USB-C" };
    juce::TextButton bluetoothButton { "Bluetooth" };
    juce::TextButton refreshButton { "Refresh" };
    double progressValue = -1.0;
    juce::ProgressBar queryProgress;
    DeviceOverviewComponent overview;
    DiagnosticsComponent diagnostics;
    TransportKind requestedTransport = TransportKind::usbC;
    bool showingDiagnostics = false;
    bool busy = false;
    bool hasSnapshot = false;
    DeviceSnapshot currentSnapshot;
    SettingsDocument document;
    DeviceController controller;
};
