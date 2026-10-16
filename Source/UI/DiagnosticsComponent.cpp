#include "DiagnosticsComponent.h"

#include "../CoreMidiTransport.h"
#include "../VendorProtocol.h"
#include "MixerLookAndFeel.h"

DiagnosticsComponent::DiagnosticsComponent()
    : progressBar(progressValue)
{
    for (auto* editor : { &endpointsEditor, &protocolEditor })
    {
        editor->setMultiLine(true);
        editor->setReadOnly(true);
        editor->setScrollbarsShown(true);
        editor->setCaretVisible(false);
        editor->setPopupMenuEnabled(true);
        editor->setColour(juce::TextEditor::textColourId, mixerui::ink);
        editor->setColour(juce::TextEditor::backgroundColourId, mixerui::plate);
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::highlightColourId, mixerui::knob.withAlpha(0.35f));
        editor->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                     12.0f,
                                                     juce::Font::plain)));
        addAndMakeVisible(*editor);
    }

    addAndMakeVisible(readButton);
    addAndMakeVisible(exportButton);
    readButton.onClick = [this] { if (onReadSettings) onReadSettings(); };
    exportButton.onClick = [this] { exportSettings(); };
    readButton.setColour(juce::TextButton::buttonColourId, mixerui::keyDown);
    readButton.setColour(juce::TextButton::textColourOffId, mixerui::onShell);
    exportButton.setColour(juce::TextButton::buttonColourId, mixerui::keyIdle);
    exportButton.setColour(juce::TextButton::textColourOffId, mixerui::ink);

    for (auto* label : { &statusLabel, &settingsSummary, &rawPrefixLabel })
    {
        label->setFont(MixerLookAndFeel::font(12.0f));
        label->setMinimumHorizontalScale(0.6f);
        addAndMakeVisible(*label);
    }
    statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
    rawPrefixLabel.setColour(juce::Label::textColourId, mixerui::muted);

    progressBar.setPercentageDisplay(false);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, mixerui::knobOutline);
    addAndMakeVisible(progressBar);
    progressBar.setVisible(false);

    refreshEndpoints();
    clearSnapshot();
    statusLabel.setText("Ready", juce::dontSendNotification);
}

void DiagnosticsComponent::refreshEndpoints()
{
    juce::String content;
    const auto sources = CoreMidiTransport::sources();
    content += "COREMIDI INPUTS / SOURCES (" + juce::String(static_cast<int>(sources.size())) + ")\n";
    for (const auto& source : sources)
        content += "  " + mixerui::utf8(source.fullName()) + "\n";

    const auto destinations = CoreMidiTransport::destinations();
    content += "\nCOREMIDI OUTPUTS / DESTINATIONS (" + juce::String(static_cast<int>(destinations.size())) + ")\n";
    for (const auto& destination : destinations)
        content += "  " + mixerui::utf8(destination.fullName()) + "\n";
    endpointsEditor.setText(content);
}

void DiagnosticsComponent::clearSnapshot()
{
    protocolEditor.setText("No device data yet.\n\nConnect over USB-C or Bluetooth to inspect raw version packets and GATT services.");
    settings.reset();
    settingsSummary.setText("No settings snapshot loaded", juce::dontSendNotification);
    rawPrefixLabel.setText("Raw 6,282-byte snapshot. Export keeps both stored profiles.",
                           juce::dontSendNotification);
    updateEnabledState();
}

void DiagnosticsComponent::setSnapshot(const DeviceSnapshot& snapshot)
{
    juce::String content;
    content += "Device: " + mixerui::utf8(snapshot.version.deviceName) + "\n";
    content += "Firmware: " + mixerui::utf8(snapshot.version.versionLabel) + "\n";
    content += "Transport: " + juce::String(snapshot.transport == TransportKind::usbC
                                               ? "USB-C" : "Bluetooth GATT + BLE MIDI") + "\n\n";
    if (!snapshot.rawSysEx.empty())
        content += "Raw SysEx:\n" + mixerui::utf8(VendorProtocol::hexString(snapshot.rawSysEx)) + "\n\n";
    content += "Raw vendor payload:\n" + mixerui::utf8(VendorProtocol::hexString(snapshot.version.rawPayload)) + "\n\n";
    content += "Version digit bytes:\n" + mixerui::utf8(VendorProtocol::hexString(snapshot.version.versionDigitBytes)) + "\n\n";
    content += "GATT services:\n";
    if (snapshot.hasHealth && !snapshot.health.services.empty())
    {
        for (const auto& service : snapshot.health.services)
            content += "  " + mixerui::utf8(service) + "\n";
    }
    else
    {
        content += "  Available over Bluetooth\n";
    }
    if (!snapshot.warning.empty())
        content += "\nWarning:\nWireless health data is unavailable. Refresh over Bluetooth to retry.";
    protocolEditor.setText(content);
}

void DiagnosticsComponent::setBusy(bool busy, double progress, bool settingsOperation)
{
    controlsBusy = busy;
    readingSettings = busy && settingsOperation;
    progressValue = juce::jlimit(0.0, 1.0, progress);
    progressBar.setVisible(readingSettings);
    progressBar.repaint();

    if (readingSettings)
    {
        statusLabel.setColour(juce::Label::textColourId, mixerui::ink);
        statusLabel.setText("Reading settings snapshot... "
                                + juce::String(juce::roundToInt(progressValue * 100.0)) + " %",
                            juce::dontSendNotification);
    }
    else if (busy)
    {
        statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
        statusLabel.setText("Reading device information...", juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
        statusLabel.setText("Ready", juce::dontSendNotification);
    }
    updateEnabledState();
}

void DiagnosticsComponent::setSettings(const SettingsReadResult& result)
{
    settings = result;
    settingsSummary.setText(juce::String(static_cast<int>(result.data.size())) + " bytes / "
                                + juce::String(result.chunkCount) + " chunks / "
                                + juce::String(result.acknowledgementCount) + " ACK / "
                                + (result.transport == TransportKind::usbC ? "USB-C" : "Bluetooth"),
                            juce::dontSendNotification);
    rawPrefixLabel.setText("Prefix: " + mixerui::utf8(VendorProtocol::hexString(result.data, 32)),
                           juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, mixerui::success);
    statusLabel.setText("Settings snapshot complete. Export is ready.", juce::dontSendNotification);
    updateEnabledState();
}

void DiagnosticsComponent::setError(const juce::String& message)
{
    statusLabel.setColour(juce::Label::textColourId, mixerui::error);
    statusLabel.setText(message, juce::dontSendNotification);
    statusLabel.setTooltip(message);
}

void DiagnosticsComponent::updateEnabledState()
{
    readButton.setEnabled(!controlsBusy);
    exportButton.setEnabled(!controlsBusy && settings.has_value());
}

void DiagnosticsComponent::exportSettings()
{
    if (!settings)
        return;

    const auto transportName = settings->transport == TransportKind::usbC ? "USB" : "BLE";
    const auto initialFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("SMC-Mixer-" + juce::String(transportName) + "-settings.bin");
    fileChooser = std::make_unique<juce::FileChooser>("Export SMC-Mixer settings snapshot", initialFile, "*.bin");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<DiagnosticsComponent>(this)](const juce::FileChooser& chooser) {
                                 if (safe == nullptr)
                                     return;
                                 const auto file = chooser.getResult();
                                 safe->fileChooser.reset();
                                 if (file == juce::File() || !safe->settings)
                                     return;

                                 if (file.replaceWithData(safe->settings->data.data(), safe->settings->data.size()))
                                 {
                                     safe->statusLabel.setColour(juce::Label::textColourId, mixerui::success);
                                     safe->statusLabel.setText("Exported: " + file.getFullPathName(),
                                                               juce::dontSendNotification);
                                     safe->statusLabel.setTooltip(file.getFullPathName());
                                 }
                                 else
                                 {
                                     safe->setError("Could not write the file. Check the location and permissions.");
                                 }
                             });
}

void DiagnosticsComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintPlate(graphics, endpointsCard);
    mixerui::paintPlate(graphics, protocolCard);
    mixerui::paintPlate(graphics, settingsCard);

    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(10.0f, true));
    graphics.drawText("MIDI ENDPOINTS", endpointsCard.withHeight(34).reduced(16, 0),
                      juce::Justification::centredLeft, false);
    graphics.drawText("PROTOCOL & SERVICES", protocolCard.withHeight(34).reduced(16, 0),
                      juce::Justification::centredLeft, false);
    graphics.drawText("SETTINGS SNAPSHOT / READ ONLY", settingsCard.withHeight(34).reduced(16, 0),
                      juce::Justification::centredLeft, false);
}

void DiagnosticsComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    settingsCard = area.removeFromBottom(172);
    area.removeFromBottom(14);
    protocolCard = area.removeFromRight(area.getWidth() / 2);
    area.removeFromRight(14);
    endpointsCard = area;

    endpointsEditor.setBounds(endpointsCard.reduced(16).withTrimmedTop(24));
    protocolEditor.setBounds(protocolCard.reduced(16).withTrimmedTop(24));

    auto settingsArea = settingsCard.reduced(16, 0);
    readButton.setBounds(settingsArea.getRight() - 282, settingsArea.getY() + 9, 132, 34);
    exportButton.setBounds(settingsArea.getRight() - 140, settingsArea.getY() + 9, 124, 34);
    statusLabel.setBounds(settingsArea.getX(), settingsArea.getY() + 48, settingsArea.getWidth(), 22);
    progressBar.setBounds(settingsArea.getX(), settingsArea.getY() + 76, settingsArea.getWidth(), 9);
    settingsSummary.setBounds(settingsArea.getX(), settingsArea.getY() + 99, settingsArea.getWidth(), 24);
    rawPrefixLabel.setBounds(settingsArea.getX(), settingsArea.getY() + 129, settingsArea.getWidth(), 26);
}
