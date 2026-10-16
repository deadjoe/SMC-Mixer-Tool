#include "DiagnosticsComponent.h"

#include "../CoreMidiTransport.h"
#include "../VendorProtocol.h"
#include "MixerLookAndFeel.h"

namespace
{
bool matches(const std::string& name, const char* part)
{
    return juce::String(name).containsIgnoreCase(part);
}

juce::String portNamed(const char* part)
{
    const auto named = [&](const MidiEndpointDescriptor& endpoint) {
        return matches(endpoint.fullName(), "SMC-Mixer") && matches(endpoint.fullName(), part);
    };
    for (const auto& source : CoreMidiTransport::sources())
        if (named(source))
            return mixerui::utf8(source.fullName());
    for (const auto& destination : CoreMidiTransport::destinations())
        if (named(destination))
            return mixerui::utf8(destination.fullName());
    return {};
}

juce::String serviceLabel(const std::string& uuid)
{
    const auto text = juce::String(uuid).toUpperCase();
    if (text.contains("180A"))
        return "Device Information (180A)";
    if (text.contains("180F"))
        return "Battery (180F)";
    if (text.contains("AE40"))
        return "Vendor settings (AE40)";
    if (text.contains("03B80E5A"))
        return "BLE MIDI";
    if (text.contains("AE00"))
        return "Vendor service AE00";
    return mixerui::utf8(uuid);
}
}

DiagnosticsComponent::DiagnosticsComponent()
    : progressBar(progressValue)
{
    reportEditor.setMultiLine(true);
    reportEditor.setReadOnly(true);
    reportEditor.setScrollbarsShown(true);
    reportEditor.setCaretVisible(false);
    reportEditor.setPopupMenuEnabled(true);
    reportEditor.setColour(juce::TextEditor::textColourId, mixerui::ink);
    reportEditor.setColour(juce::TextEditor::backgroundColourId, mixerui::plate);
    reportEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    reportEditor.setColour(juce::TextEditor::highlightColourId, mixerui::knob.withAlpha(0.35f));
    reportEditor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                      13.0f,
                                                      juce::Font::plain)));
    addAndMakeVisible(reportEditor);

    addAndMakeVisible(readButton);
    addAndMakeVisible(exportButton);
    readButton.onClick = [this] { if (onReadSettings) onReadSettings(); };
    exportButton.onClick = [this] { exportSettings(); };
    readButton.setTooltip("Read the 6,282-byte settings image from the selected link.");
    exportButton.setTooltip("Save the settings image. It contains the DAW and CC / User profiles.");
    readButton.setColour(juce::TextButton::buttonColourId, mixerui::keyDown);
    readButton.setColour(juce::TextButton::textColourOffId, mixerui::onShell);
    exportButton.setColour(juce::TextButton::buttonColourId, mixerui::keyIdle);
    exportButton.setColour(juce::TextButton::textColourOffId, mixerui::ink);

    for (auto* label : { &statusLabel, &settingsSummary })
    {
        label->setFont(MixerLookAndFeel::font(12.0f));
        label->setMinimumHorizontalScale(0.6f);
        addAndMakeVisible(*label);
    }
    statusLabel.setColour(juce::Label::textColourId, mixerui::muted);

    progressBar.setPercentageDisplay(false);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, mixerui::knobOutline);
    addAndMakeVisible(progressBar);
    progressBar.setVisible(false);

    render();
    statusLabel.setText("Ready", juce::dontSendNotification);
}

void DiagnosticsComponent::setActiveTransport(TransportKind transport)
{
    activeTransport = transport;
    render();
}

void DiagnosticsComponent::refreshEndpoints()
{
    render();
}

void DiagnosticsComponent::clearSnapshot()
{
    snapshot.reset();
    settings.reset();
    settingsFromRead = false;
    render();
    updateEnabledState();
}

void DiagnosticsComponent::setSnapshot(const DeviceSnapshot& next)
{
    snapshot = next;
    render();
}

void DiagnosticsComponent::setSettings(const SettingsReadResult& result)
{
    settings = result;
    settingsFromRead = true;
    statusLabel.setColour(juce::Label::textColourId, mixerui::success);
    statusLabel.setText("Settings snapshot complete. Export is ready.", juce::dontSendNotification);
    render();
    updateEnabledState();
}

void DiagnosticsComponent::adoptWorkingImage(const Bytes& image)
{
    if (!settings)
    {
        SettingsReadResult local;
        local.data = image;
        local.transport = activeTransport;
        settings = std::move(local);
    }
    else
    {
        settings->data = image;
    }
    settingsFromRead = false;
    statusLabel.setColour(juce::Label::textColourId, mixerui::success);
    statusLabel.setText("Write verified. Export uses this image.", juce::dontSendNotification);
    render();
    updateEnabledState();
}

juce::String DiagnosticsComponent::linkReport() const
{
    const auto privatePort = portNamed("Private");
    const auto masterPort = portNamed("Master");
    const auto bluetoothPort = portNamed("Bluetooth");
    const auto linkName = [](TransportKind transport) {
        return juce::String(transport == TransportKind::usbC ? "USB-C" : "Bluetooth");
    };
    const bool usb = activeTransport == TransportKind::usbC;

    juce::String text;
    text += "Selected link: " + linkName(activeTransport) + "\n";
    if (usb)
    {
        text += privatePort.isEmpty()
            ? "Not ready. The USB-C Private port was not found. Check the cable and that the mixer is on.\n"
            : "Ready. Settings travel as SysEx on the Private port.\n";
    }
    else if (snapshot && snapshot->transport == TransportKind::bluetooth && !snapshot->version.versionLabel.empty())
    {
        text += "Ready. Vendor service AE40 answered with firmware "
             + mixerui::utf8(snapshot->version.versionLabel) + ".\n";
        text += "AE41 takes one write without response. AE42 returns one reply. The MIDI port is not used for settings.\n";
    }
    else if (bluetoothPort.isNotEmpty())
    {
        text += "The system MIDI port is present. Settings still need vendor service AE40. Refresh to confirm it.\n";
    }
    else
    {
        text += "Not ready. Connect the mixer in Audio MIDI Setup, then Refresh.\n";
    }

    if (snapshot && snapshot->transport != activeTransport)
        text += "The identity below was read over " + linkName(snapshot->transport)
             + ". Refresh queries the selected link.\n";

    text += "\nUSB-C\n";
    text += "  Private: " + (privatePort.isEmpty() ? juce::String("not found") : privatePort) + "\n";
    text += "  Master: " + (masterPort.isEmpty() ? juce::String("not found") : masterPort) + "\n";
    text += "  Private carries settings. Master carries performance MIDI.\n";

    text += "\nBluetooth\n";
    text += "  MIDI port: " + (bluetoothPort.isEmpty()
                                   ? juce::String("not found. Audio MIDI Setup adds it after the mixer connects.")
                                   : bluetoothPort) + "\n";
    text += "  Settings path: GATT service AE40, characteristic AE41 out and AE42 back.\n";

    if (!snapshot)
    {
        text += "\nNo identity yet. Refresh queries the selected link.\n";
        return text;
    }

    text += "\nIdentity\n";
    text += "  Read over: " + linkName(snapshot->transport) + "\n";
    text += "  Device: " + mixerui::utf8(snapshot->version.deviceName) + "\n";
    text += "  Firmware: " + mixerui::utf8(snapshot->version.versionLabel) + "\n";
    if (snapshot->transport == TransportKind::usbC)
    {
        if (!snapshot->sourceName.empty())
            text += "  Opened input: " + mixerui::utf8(snapshot->sourceName) + "\n";
        if (!snapshot->destinationName.empty())
            text += "  Opened output: " + mixerui::utf8(snapshot->destinationName) + "\n";
    }
    else if (snapshot->hasHealth)
    {
        const auto& health = snapshot->health;
        text += "  Manufacturer: " + (health.manufacturer ? mixerui::utf8(*health.manufacturer) : juce::String("unavailable")) + "\n";
        text += "  Model: " + (health.model ? mixerui::utf8(*health.model) : juce::String("unavailable")) + "\n";
        text += "  Battery: " + (health.battery ? juce::String(*health.battery) + "%" : juce::String("unavailable")) + "\n";
        text += "  Signal: " + (health.rssi ? juce::String(*health.rssi) + " dBm" : juce::String("unavailable")) + "\n";
        text += "  Services:\n";
        if (health.services.empty())
            text += "    none reported\n";
        for (const auto& service : health.services)
            text += "    " + serviceLabel(service) + "\n";
    }
    else
    {
        text += "  Battery, model, and signal did not arrive after the firmware reply.\n";
        if (!snapshot->warning.empty())
            text += "  Detail: " + mixerui::utf8(snapshot->warning) + "\n";
    }

    if (!snapshot->version.rawPayload.empty())
        text += "\nVendor payload\n  " + mixerui::utf8(VendorProtocol::hexString(snapshot->version.rawPayload)) + "\n";

    text += "\nSettings image\n";
    if (!settings)
    {
        text += "  Not loaded. Read snapshot stores both profiles in one 6,282-byte image.\n";
    }
    else
    {
        const auto expected = settings->data.size() == 6282;
        const auto link = linkName(settings->transport);
        text += "  " + juce::String(static_cast<int>(settings->data.size())) + " bytes";
        text += expected ? ", both profiles are present.\n" : ", expected 6,282 bytes.\n";
        if (settingsFromRead)
        {
            text += "  " + juce::String(settings->chunkCount) + " data chunks, "
                  + juce::String(settings->acknowledgementCount) + " other replies, over " + link + ".\n";
        }
        else if (settings->chunkCount > 0 || settings->acknowledgementCount > 0)
        {
            text += "  This is the verified working copy. The last full read had "
                  + juce::String(settings->chunkCount) + " data chunks and "
                  + juce::String(settings->acknowledgementCount) + " other replies over " + link + ".\n";
        }
        else
        {
            text += "  This is the verified working copy.\n";
        }
        text += "  Prefix: " + mixerui::utf8(VendorProtocol::hexString(settings->data, 24)) + "\n";
    }
    return text;
}

void DiagnosticsComponent::render()
{
    reportEditor.setText(linkReport());
    if (settings)
        settingsSummary.setText(juce::String(static_cast<int>(settings->data.size()))
                                    + " bytes ready to export",
                                juce::dontSendNotification);
    else
        settingsSummary.setText("Export is available after the snapshot is read", juce::dontSendNotification);
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
                                + juce::String(juce::roundToInt(progressValue * 100.0)) + "%",
                            juce::dontSendNotification);
    }
    else if (busy)
    {
        statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
        statusLabel.setText("Reading device information...", juce::dontSendNotification);
    }
    else if (statusLabel.getText() == "Reading device information..."
             || statusLabel.getText().startsWith("Reading settings"))
    {
        statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
        statusLabel.setText("Ready", juce::dontSendNotification);
    }
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
    mixerui::paintPlate(graphics, reportCard);
    mixerui::paintPlate(graphics, settingsCard);

    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(10.0f, true));
    graphics.drawText("LINK", reportCard.withHeight(34).reduced(16, 0),
                      juce::Justification::centredLeft, false);
    graphics.drawText("SNAPSHOT", settingsCard.withHeight(34).reduced(16, 0),
                      juce::Justification::centredLeft, false);
}

void DiagnosticsComponent::resized()
{
    auto area = getLocalBounds().reduced(20);
    settingsCard = area.removeFromBottom(118);
    area.removeFromBottom(14);
    reportCard = area;

    reportEditor.setBounds(reportCard.reduced(16).withTrimmedTop(28).withTrimmedBottom(8));

    auto settingsArea = settingsCard.reduced(16, 0);
    readButton.setBounds(settingsArea.getRight() - 292, settingsArea.getY() + 9, 140, 34);
    exportButton.setBounds(settingsArea.getRight() - 140, settingsArea.getY() + 9, 124, 34);
    statusLabel.setBounds(settingsArea.getX(), settingsArea.getY() + 52, settingsArea.getWidth(), 22);
    progressBar.setBounds(settingsArea.getX(), settingsArea.getY() + 78, settingsArea.getWidth(), 8);
    settingsSummary.setBounds(settingsArea.getX(), settingsArea.getY() + 90, settingsArea.getWidth() - 300, 20);
}
