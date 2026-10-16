#pragma once

#include "DeviceController.h"
#include "UI/DeviceOverviewComponent.h"
#include "UI/DiagnosticsComponent.h"
#include "UI/MixerLookAndFeel.h"

#include <JuceHeader.h>

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshDevice();
    void readSettings();
    void handleEvent(DeviceEvent event);
    void setBusy(bool busy, bool settingsOperation);
    void finishBusyState();
    void selectTransport(TransportKind transport);
    void showDiagnostics(bool shouldShowDiagnostics);
    void updateSelectionColours();

    MixerLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 480 };
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label detailLabel;
    juce::Label connectionCaption;
    juce::TextButton overviewTabButton { "Overview" };
    juce::TextButton diagnosticsTabButton { "Diagnostics" };
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
    DeviceController controller;
};
