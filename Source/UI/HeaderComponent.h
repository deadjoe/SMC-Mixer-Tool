#pragma once

#include "../DeviceModel.h"
#include "MixerLookAndFeel.h"
#include "Widgets.h"

#include <JuceHeader.h>

#include <functional>

// The toolbar across the top of the window, and the one place that reports the link: its
// state, what the app is doing, and the facts the selected link provides.
class HeaderComponent final : public juce::Component,
                              private juce::Timer
{
public:
    HeaderComponent();

    std::function<void(TransportKind)> onTransportChosen;
    std::function<void()> onRefresh;
    std::function<void(bool)> onDiagnosticsToggled;

    struct Status
    {
        mixerui::Tone tone = mixerui::Tone::neutral;
        juce::String state;   // "Online", "Connecting", "Failed"
        juce::String detail;  // one line under the device name
        juce::String tooltip; // the full message when the detail is shortened
    };

    void setStatus(const Status& status);
    const Status& getStatus() const noexcept { return status; }
    void setDevice(const juce::String& name, const juce::String& firmware);
    void setTransport(TransportKind transport);
    void setDiagnosticsShown(bool shown);
    void setBusy(bool busy);
    // A value from 0 to 1 draws a bar under the toolbar; a negative value shows activity.
    void setProgress(double progress);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    static constexpr int preferredHeight = 64;

    // For layout tests.
    juce::Rectangle<int> deviceArea() const noexcept { return deviceBounds; }

private:
    void timerCallback() override { repaint(0, getHeight() - 2, getWidth(), 2); }

    juce::Label stateLabel;
    juce::Label deviceLabel;
    juce::Label firmwareLabel;
    juce::Label detailLabel;
    juce::TextButton usbButton { "USB-C" };
    juce::TextButton bluetoothButton { "Bluetooth" };
    juce::TextButton refreshButton { "Refresh" };
    IconButton diagnosticsButton { "Diagnostics", mixerui::Icon::gear, true };
    Status status;
    double progress = -2.0;
    juce::Rectangle<int> deviceBounds;
    juce::Rectangle<int> statusDot;
};
