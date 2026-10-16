#include "DeviceOverviewComponent.h"

#include "MixerLookAndFeel.h"

DeviceOverviewComponent::DeviceOverviewComponent()
    : batteryBar(batteryLevel)
{
    addAndMakeVisible(diagram);
    addAndMakeVisible(inspector);
    for (auto* label : { &deviceValue, &firmwareValue, &transportValue, &modeValue, &manufacturerValue,
                         &modelValue, &batteryValue, &rssiValue, &sourceValue, &destinationValue, &warningLabel })
        addAndMakeVisible(*label);

    styleValue(deviceValue, 18.0f);
    styleValue(firmwareValue, 18.0f);
    styleValue(transportValue, 12.0f);
    transportValue.setJustificationType(juce::Justification::centredRight);
    styleValue(modeValue, 12.0f);
    modeValue.setColour(juce::Label::textColourId, mixerui::muted);
    styleValue(manufacturerValue, 13.0f);
    styleValue(modelValue, 13.0f);
    styleValue(batteryValue, 13.0f);
    styleValue(rssiValue, 13.0f);
    styleValue(sourceValue, 13.0f);
    styleValue(destinationValue, 13.0f);
    styleValue(warningLabel, 12.0f);
    warningLabel.setColour(juce::Label::textColourId, mixerui::warning);

    batteryBar.setPercentageDisplay(false);
    addAndMakeVisible(batteryBar);

    diagram.onControlSelected = [this](ControlSelection selection) { inspector.selectControl(selection); };
    inspector.onSettingsRequired = [this] { if (onSettingsRequired) onSettingsRequired(); };
    inspector.onControlEdited = [this](ControlSelection selection, DeviceProfile profile, EditableField field, std::uint8_t value) {
        if (onControlEdited)
            onControlEdited(selection, profile, field, value);
    };
    inspector.onRevertRequested = [this](ControlSelection selection, DeviceProfile profile) {
        if (onRevertRequested)
            onRevertRequested(selection, profile);
    };
    inspector.onWriteRequested = [this] { if (onWriteRequested) onWriteRequested(); };
    inspector.isRecordDirty = [this](ControlSelection selection, DeviceProfile profile) {
        return isRecordDirty && isRecordDirty(selection, profile);
    };
    clear(TransportKind::usbC);
}

void DeviceOverviewComponent::styleValue(juce::Label& label, float fontSize)
{
    label.setFont(MixerLookAndFeel::font(fontSize, fontSize >= 17.0f));
    label.setColour(juce::Label::textColourId, mixerui::text);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(0.55f);
}

void DeviceOverviewComponent::clear(TransportKind transport)
{
    const auto isBluetooth = transport == TransportKind::bluetooth;
    deviceValue.setText("SMC-Mixer", juce::dontSendNotification);
    firmwareValue.setText("--", juce::dontSendNotification);
    transportValue.setText(isBluetooth ? "BLUETOOTH" : "USB-C", juce::dontSendNotification);
    modeValue.setText("DAW and CC / User profiles", juce::dontSendNotification);
    const auto pendingText = isBluetooth ? "Waiting" : "Bluetooth only";
    manufacturerValue.setText(pendingText, juce::dontSendNotification);
    modelValue.setText(pendingText, juce::dontSendNotification);
    batteryValue.setText(pendingText, juce::dontSendNotification);
    rssiValue.setText(pendingText, juce::dontSendNotification);
    sourceValue.setText("Waiting for query", juce::dontSendNotification);
    destinationValue.setText("Waiting for query", juce::dontSendNotification);
    warningLabel.setText({}, juce::dontSendNotification);
    warningLabel.setVisible(false);
    batteryBar.setVisible(false);
    inspector.clearConfiguration();
    diagram.setDirtyControls({});
    repaint();
}

void DeviceOverviewComponent::setSnapshot(const DeviceSnapshot& snapshot)
{
    deviceValue.setText(snapshot.version.deviceName.empty() ? "SMC-Mixer" : snapshot.version.deviceName,
                        juce::dontSendNotification);
    firmwareValue.setText(snapshot.version.versionLabel, juce::dontSendNotification);
    transportValue.setText(snapshot.transport == TransportKind::bluetooth ? "BLUETOOTH" : "USB-C",
                           juce::dontSendNotification);

    const auto unavailable = snapshot.transport == TransportKind::bluetooth ? "Not provided" : "Bluetooth only";
    manufacturerValue.setText(snapshot.hasHealth && snapshot.health.manufacturer
                                  ? *snapshot.health.manufacturer : unavailable,
                              juce::dontSendNotification);
    modelValue.setText(snapshot.hasHealth && snapshot.health.model ? *snapshot.health.model : unavailable,
                       juce::dontSendNotification);
    batteryValue.setText(snapshot.hasHealth && snapshot.health.battery
                             ? juce::String(*snapshot.health.battery) + " %" : unavailable,
                         juce::dontSendNotification);
    rssiValue.setText(snapshot.hasHealth && snapshot.health.rssi
                          ? juce::String(*snapshot.health.rssi) + " dBm" : unavailable,
                      juce::dontSendNotification);
    sourceValue.setText(snapshot.sourceName, juce::dontSendNotification);
    sourceValue.setTooltip(snapshot.sourceName);
    destinationValue.setText(snapshot.destinationName, juce::dontSendNotification);
    destinationValue.setTooltip(snapshot.destinationName);

    const auto hasBattery = snapshot.hasHealth && snapshot.health.battery.has_value();
    batteryBar.setVisible(hasBattery);
    if (hasBattery)
    {
        batteryLevel = juce::jlimit(0, 100, *snapshot.health.battery) / 100.0;
        batteryBar.setColour(juce::ProgressBar::foregroundColourId,
                             *snapshot.health.battery > 30 ? mixerui::success : mixerui::error);
        batteryBar.repaint();
    }

    warningLabel.setVisible(!snapshot.warning.empty());
    warningLabel.setText(snapshot.warning.empty() ? "" : "Some wireless health fields are unavailable",
                         juce::dontSendNotification);
    warningLabel.setTooltip("Connect over Bluetooth and refresh to retry wireless health data.");
    repaint();
}

void DeviceOverviewComponent::showWorkingCopy(const ControlConfiguration& configuration,
                                              const std::vector<ControlSelection>& dirty,
                                              bool editingEnabled)
{
    inspector.setEditingEnabled(editingEnabled);
    inspector.setDirtyControls(dirty);
    diagram.setDirtyControls(dirty);
    inspector.setConfiguration(configuration);
}

void DeviceOverviewComponent::cacheWorkingCopy(const ControlConfiguration& configuration,
                                               const std::vector<ControlSelection>& dirty)
{
    inspector.setDirtyControls(dirty);
    diagram.setDirtyControls(dirty);
    inspector.cacheConfiguration(configuration);
}

void DeviceOverviewComponent::setSettingsLoading(bool loading, double progress)
{
    inspector.setLoading(loading, progress);
}

void DeviceOverviewComponent::setSettingsWriting(bool writing, double progress)
{
    inspector.setWriting(writing, progress);
}

void DeviceOverviewComponent::setSettingsError(const juce::String& message)
{
    inspector.setReadError(message);
}

void DeviceOverviewComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintPlate(graphics, identityCard);

    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(10.0f, true));
    const char* captions[] { "Manufacturer", "Model", "Battery", "Signal", "MIDI in", "MIDI out" };
    const auto columns = identityCard.reduced(18, 12);
    const auto captionRow = columns.withTrimmedTop(34).withHeight(14);
    const auto columnWidth = captionRow.getWidth() / 6;
    for (int index = 0; index < 6; ++index)
    {
        graphics.drawText(captions[index],
                          captionRow.getX() + index * columnWidth,
                          captionRow.getY(),
                          columnWidth - 8,
                          captionRow.getHeight(),
                          juce::Justification::centredLeft,
                          false);
    }
}

void DeviceOverviewComponent::resized()
{
    auto area = getLocalBounds().reduced(20, 16);
    identityCard = area.removeFromTop(104);
    area.removeFromTop(12);
    inspectorArea = area.removeFromBottom(200);
    area.removeFromBottom(12);
    diagram.setBounds(area);
    inspector.setBounds(inspectorArea);

    auto identity = identityCard.reduced(18, 12);
    auto titleRow = identity.removeFromTop(28);
    deviceValue.setBounds(titleRow.removeFromLeft(156).reduced(0, 2));
    firmwareValue.setBounds(titleRow.removeFromLeft(108).reduced(0, 2));
    transportValue.setBounds(titleRow.removeFromRight(108).reduced(0, 4));
    warningLabel.setBounds(titleRow.removeFromRight(250).reduced(8, 4));
    modeValue.setBounds(titleRow.reduced(8, 4));

    auto valueRow = identity.removeFromBottom(26);
    const auto columnWidth = valueRow.getWidth() / 6;
    juce::Label* values[] { &manufacturerValue, &modelValue, &batteryValue, &rssiValue,
                            &sourceValue, &destinationValue };
    for (int index = 0; index < 6; ++index)
    {
        values[index]->setBounds(valueRow.getX() + index * columnWidth,
                                 valueRow.getY(),
                                 columnWidth - 10,
                                 20);
    }
    batteryBar.setBounds(valueRow.getX() + 2 * columnWidth, valueRow.getY() + 21, juce::jmax(24, columnWidth / 3), 3);
}
