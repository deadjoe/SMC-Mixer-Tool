#include "MainComponent.h"

MainComponent::MainComponent()
    : queryProgress(progressValue)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("SMC Mixer Tool", juce::dontSendNotification);
    titleLabel.setFont(MixerLookAndFeel::font(24.0f, true));
    subtitleLabel.setText("Read-only settings for the M-VAVE SMC-Mixer", juce::dontSendNotification);
    subtitleLabel.setFont(MixerLookAndFeel::font(12.0f));
    subtitleLabel.setColour(juce::Label::textColourId, mixerui::muted);
    statusLabel.setText("READY", juce::dontSendNotification);
    statusLabel.setFont(MixerLookAndFeel::font(14.0f, true));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    detailLabel.setText("USB-C and Bluetooth", juce::dontSendNotification);
    detailLabel.setFont(MixerLookAndFeel::font(11.0f));
    detailLabel.setColour(juce::Label::textColourId, mixerui::muted);
    detailLabel.setJustificationType(juce::Justification::centredRight);
    detailLabel.setMinimumHorizontalScale(0.55f);
    connectionCaption.setText("CONNECTION", juce::dontSendNotification);
    connectionCaption.setFont(MixerLookAndFeel::font(10.0f, true));
    connectionCaption.setColour(juce::Label::textColourId, mixerui::muted);
    connectionCaption.setJustificationType(juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &subtitleLabel, &statusLabel, &detailLabel, &connectionCaption })
        addAndMakeVisible(*label);

    overviewTabButton.onClick = [this] { showDiagnostics(false); };
    diagnosticsTabButton.onClick = [this] { showDiagnostics(true); };
    usbButton.onClick = [this] { selectTransport(TransportKind::usbC); };
    bluetoothButton.onClick = [this] { selectTransport(TransportKind::bluetooth); };
    refreshButton.onClick = [this] { refreshDevice(); };
    for (auto* button : { &overviewTabButton, &diagnosticsTabButton, &usbButton, &bluetoothButton, &refreshButton })
        addAndMakeVisible(*button);

    refreshButton.setColour(juce::TextButton::buttonColourId, mixerui::keyIdle);
    refreshButton.setColour(juce::TextButton::textColourOffId, mixerui::ink);

    queryProgress.setPercentageDisplay(false);
    queryProgress.setVisible(false);
    addAndMakeVisible(queryProgress);

    addChildComponent(overview);
    addChildComponent(diagnostics);
    diagnostics.onReadSettings = [this] { readSettings(); };
    overview.onSettingsRequired = [this] { readSettings(); };
    showDiagnostics(false);
    updateSelectionColours();

    startTimer(100);
    refreshDevice();
}

MainComponent::~MainComponent()
{
    stopTimer();
    diagnostics.onReadSettings = nullptr;
    overview.onSettingsRequired = nullptr;
    controller.shutdown();
    setLookAndFeel(nullptr);
}

void MainComponent::selectTransport(TransportKind transport)
{
    if (busy)
        return;
    requestedTransport = transport;
    updateSelectionColours();
    refreshDevice();
}

void MainComponent::showDiagnostics(bool shouldShowDiagnostics)
{
    showingDiagnostics = shouldShowDiagnostics;
    overview.setVisible(!showingDiagnostics);
    diagnostics.setVisible(showingDiagnostics);
    updateSelectionColours();
}

void MainComponent::updateSelectionColours()
{
    const auto style = [](juce::TextButton& button, bool selected) {
        button.setColour(juce::TextButton::buttonColourId, selected ? mixerui::keyDown : mixerui::keyIdle);
        button.setColour(juce::TextButton::textColourOffId, selected ? mixerui::onShell : mixerui::ink);
        button.repaint();
    };
    style(overviewTabButton, !showingDiagnostics);
    style(diagnosticsTabButton, showingDiagnostics);
    style(usbButton, requestedTransport == TransportKind::usbC);
    style(bluetoothButton, requestedTransport == TransportKind::bluetooth);
}

void MainComponent::refreshDevice()
{
    if (!controller.requestRefresh(requestedTransport))
        return;

    hasSnapshot = false;
    overview.clear(requestedTransport);
    diagnostics.clearSnapshot();
    diagnostics.refreshEndpoints();
    statusLabel.setColour(juce::Label::textColourId, mixerui::ink);
    statusLabel.setText("CONNECTING", juce::dontSendNotification);
    detailLabel.setColour(juce::Label::textColourId, mixerui::muted);
    detailLabel.setText(requestedTransport == TransportKind::usbC
                            ? "Reading USB-C device data..."
                            : "Scanning Bluetooth and reading device data...",
                        juce::dontSendNotification);
    setBusy(true, false);
}

void MainComponent::readSettings()
{
    if (!controller.requestSettings(requestedTransport))
    {
        overview.setSettingsError("The device is busy. Select the control again in a moment.");
        return;
    }

    overview.setSettingsLoading(true, 0.0);
    statusLabel.setColour(juce::Label::textColourId, mixerui::ink);
    statusLabel.setText("READING SNAPSHOT", juce::dontSendNotification);
    detailLabel.setColour(juce::Label::textColourId, mixerui::muted);
    detailLabel.setText("Reading 6,282 bytes in chunks. Keep the device connected.", juce::dontSendNotification);
    setBusy(true, true);
}

void MainComponent::setBusy(bool shouldBeBusy, bool settingsOperation)
{
    busy = shouldBeBusy;
    usbButton.setEnabled(!busy);
    bluetoothButton.setEnabled(!busy);
    refreshButton.setEnabled(!busy);
    queryProgress.setVisible(busy);
    progressValue = settingsOperation ? 0.0 : -1.0;
    queryProgress.repaint();
    diagnostics.setBusy(busy, progressValue, settingsOperation);
}

void MainComponent::finishBusyState()
{
    busy = false;
    usbButton.setEnabled(true);
    bluetoothButton.setEnabled(true);
    refreshButton.setEnabled(true);
    queryProgress.setVisible(false);
}

void MainComponent::timerCallback()
{
    for (auto& event : controller.pollEvents())
        handleEvent(std::move(event));
}

void MainComponent::handleEvent(DeviceEvent event)
{
    switch (event.kind)
    {
        case DeviceEvent::Kind::snapshotReady:
        {
            finishBusyState();
            diagnostics.setBusy(false, 1.0, false);
            currentSnapshot = event.snapshot;
            hasSnapshot = true;
            overview.setSnapshot(currentSnapshot);
            diagnostics.setSnapshot(currentSnapshot);
            statusLabel.setColour(juce::Label::textColourId,
                                  currentSnapshot.warning.empty() ? mixerui::success : mixerui::warning);
            statusLabel.setText("ONLINE / " + mixerui::utf8(currentSnapshot.version.versionLabel),
                                juce::dontSendNotification);
            detailLabel.setColour(juce::Label::textColourId,
                                  currentSnapshot.warning.empty() ? mixerui::muted : mixerui::warning);
            detailLabel.setText(currentSnapshot.warning.empty()
                                    ? (currentSnapshot.transport == TransportKind::usbC
                                           ? "USB-C link is healthy" : "Bluetooth link is healthy")
                                    : "Firmware online; some wireless health fields are unavailable",
                                juce::dontSendNotification);
            break;
        }
        case DeviceEvent::Kind::settingsProgress:
            progressValue = event.progress;
            queryProgress.repaint();
            diagnostics.setBusy(true, progressValue, true);
            overview.setSettingsLoading(true, progressValue);
            break;
        case DeviceEvent::Kind::settingsReady:
            finishBusyState();
            diagnostics.setBusy(false, 1.0, true);
            diagnostics.setSettings(event.settings);
            try
            {
                overview.setSettings(event.settings);
            }
            catch (const std::exception& error)
            {
                overview.setSettingsError(mixerui::utf8(error.what()));
            }
            statusLabel.setColour(juce::Label::textColourId, mixerui::success);
            statusLabel.setText(hasSnapshot ? "ONLINE / " + mixerui::utf8(currentSnapshot.version.versionLabel)
                                            : "SNAPSHOT READY",
                                juce::dontSendNotification);
            detailLabel.setColour(juce::Label::textColourId, mixerui::success);
            detailLabel.setText("Settings snapshot complete / "
                                    + juce::String(static_cast<int>(event.settings.data.size())) + " bytes",
                                juce::dontSendNotification);
            break;
        case DeviceEvent::Kind::operationFailed:
        {
            finishBusyState();
            diagnostics.setBusy(false, 0.0, event.settingsOperation);
            const auto summary = event.settingsOperation
                ? juce::String("Settings read failed. Check the connection and retry.")
                : event.transport == TransportKind::bluetooth
                    ? juce::String("Bluetooth failed. Check macOS permission and Audio MIDI Setup.")
                    : juce::String("USB-C device not found or not responding.");
            auto detail = summary;
            if (!event.message.empty())
                detail += "\n" + mixerui::utf8(event.message);
            diagnostics.setError(detail);
            if (event.settingsOperation)
                overview.setSettingsError(summary);
            statusLabel.setColour(juce::Label::textColourId, mixerui::error);
            statusLabel.setText(event.settingsOperation ? "SNAPSHOT FAILED" : "CONNECTION FAILED",
                                juce::dontSendNotification);
            detailLabel.setColour(juce::Label::textColourId, mixerui::error);
            detailLabel.setText(summary, juce::dontSendNotification);
            detailLabel.setTooltip(detail);
            if (!event.settingsOperation)
            {
                hasSnapshot = false;
                overview.clear(event.transport);
                diagnostics.clearSnapshot();
            }
            break;
        }
    }
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(mixerui::shell);
    graphics.setColour(mixerui::plate);
    graphics.fillRect(0, 0, getWidth(), 108);
    graphics.setColour(mixerui::hairline);
    graphics.fillRect(0, 108, getWidth(), 1);
}

void MainComponent::resized()
{
    titleLabel.setBounds(24, 14, 280, 28);
    subtitleLabel.setBounds(24, 42, 420, 18);
    statusLabel.setBounds(getWidth() - 560, 14, 400, 24);
    detailLabel.setBounds(getWidth() - 560, 40, 400, 18);
    refreshButton.setBounds(getWidth() - 128, 18, 104, 34);

    overviewTabButton.setBounds(24, 68, 108, 32);
    diagnosticsTabButton.setBounds(138, 68, 118, 32);
    bluetoothButton.setBounds(getWidth() - 128, 68, 104, 32);
    usbButton.setBounds(getWidth() - 220, 68, 86, 32);
    connectionCaption.setBounds(getWidth() - 318, 68, 90, 32);
    queryProgress.setBounds(0, 108, getWidth(), 3);

    overview.setBounds(0, 109, getWidth(), getHeight() - 109);
    diagnostics.setBounds(0, 109, getWidth(), getHeight() - 109);
}
