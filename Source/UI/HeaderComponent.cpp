#include "HeaderComponent.h"

namespace
{
constexpr int connectionGroup = 2;
}

HeaderComponent::HeaderComponent()
{
    using namespace mixerui;
    stateLabel.setFont(captionFont(10.0f));
    deviceLabel.setFont(font(15.0f, Weight::semibold));
    deviceLabel.setColour(juce::Label::textColourId, ink);
    firmwareLabel.setFont(font(11.0f, Weight::semibold));
    firmwareLabel.setColour(juce::Label::textColourId, inkSoft);
    firmwareLabel.setJustificationType(juce::Justification::centred);
    detailLabel.setFont(font(11.5f));
    detailLabel.setColour(juce::Label::textColourId, muted);
    detailLabel.setMinimumHorizontalScale(1.0f);
    for (auto* label : { &stateLabel, &deviceLabel, &firmwareLabel, &detailLabel })
    {
        label->setBorderSize({});
        label->setInterceptsMouseClicks(label == &detailLabel, false);
        addAndMakeVisible(*label);
    }

    for (auto* button : { &usbButton, &bluetoothButton })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(connectionGroup);
        button->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    usbButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    bluetoothButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    usbButton.setTooltip("Read and write settings over the USB-C cable.");
    bluetoothButton.setTooltip("Read and write settings over Bluetooth.");
    usbButton.onClick = [this] {
        if (usbButton.getToggleState() && onTransportChosen)
            onTransportChosen(TransportKind::usbC);
    };
    bluetoothButton.onClick = [this] {
        if (bluetoothButton.getToggleState() && onTransportChosen)
            onTransportChosen(TransportKind::bluetooth);
    };

    refreshButton.setTooltip(juce::String::fromUTF8("Read the identity and settings again (\xe2\x8c\x98R)."));
    refreshButton.onClick = [this] { if (onRefresh) onRefresh(); };
    refreshButton.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(refreshButton);

    diagnosticsButton.setClickingTogglesState(true);
    diagnosticsButton.setTooltip(juce::String::fromUTF8("Diagnostics (\xe2\x87\xa7\xe2\x8c\x98" "D)"));
    diagnosticsButton.onClick = [this] {
        if (onDiagnosticsToggled)
            onDiagnosticsToggled(diagnosticsButton.getToggleState());
    };
    addAndMakeVisible(diagnosticsButton);

    setTransport(TransportKind::usbC);
    setDevice("SMC-Mixer", {});
    setStatus({ Tone::neutral, "Ready", "Choose a link to connect.", {} });
}

void HeaderComponent::setStatus(const Status& newStatus)
{
    status = newStatus;
    stateLabel.setText(status.state.toUpperCase(), juce::dontSendNotification);
    stateLabel.setColour(juce::Label::textColourId, mixerui::toneColour(status.tone));
    detailLabel.setText(status.detail, juce::dontSendNotification);
    detailLabel.setColour(juce::Label::textColourId,
                          status.tone == mixerui::Tone::error || status.tone == mixerui::Tone::warning
                              ? mixerui::toneColour(status.tone) : mixerui::muted);
    // Every status replaces the tooltip, so an old error never lingers behind a newer state.
    detailLabel.setTooltip(status.tooltip);
    resized();
    repaint();
}

void HeaderComponent::setDevice(const juce::String& name, const juce::String& firmware)
{
    deviceLabel.setText(name.isEmpty() ? juce::String("SMC-Mixer") : name, juce::dontSendNotification);
    firmwareLabel.setText(firmware, juce::dontSendNotification);
    firmwareLabel.setVisible(firmware.isNotEmpty());
    resized();
    repaint();
}

void HeaderComponent::setTransport(TransportKind transport)
{
    usbButton.setToggleState(transport == TransportKind::usbC, juce::dontSendNotification);
    bluetoothButton.setToggleState(transport == TransportKind::bluetooth, juce::dontSendNotification);
}

void HeaderComponent::setDiagnosticsShown(bool shown)
{
    diagnosticsButton.setToggleState(shown, juce::dontSendNotification);
}

void HeaderComponent::setBusy(bool busy)
{
    usbButton.setEnabled(!busy);
    bluetoothButton.setEnabled(!busy);
    refreshButton.setEnabled(!busy);
}

void HeaderComponent::setProgress(double newProgress)
{
    progress = newProgress;
    if (progress > -1.5 && progress < 0.0)
        startTimerHz(30);
    else
        stopTimer();
    repaint();
}

void HeaderComponent::paint(juce::Graphics& graphics)
{
    using namespace mixerui;
    graphics.fillAll(surface);
    graphics.setColour(hairline);
    graphics.fillRect(0, getHeight() - 1, getWidth(), 1);

    graphics.setColour(toneColour(status.tone));
    graphics.fillEllipse(statusDot.toFloat());
    if (status.tone == Tone::success || status.tone == Tone::busy)
    {
        graphics.setColour(toneColour(status.tone).withAlpha(0.18f));
        graphics.fillEllipse(statusDot.toFloat().expanded(3.0f));
    }

    if (firmwareLabel.isVisible())
    {
        graphics.setColour(well);
        graphics.fillRoundedRectangle(firmwareLabel.getBounds().toFloat(), 4.0f);
    }

    if (progress > -1.5)
    {
        const auto track = juce::Rectangle<float>(0.0f, static_cast<float>(getHeight() - 2), static_cast<float>(getWidth()), 2.0f);
        if (progress >= 0.0)
        {
            graphics.setColour(knobDeep);
            graphics.fillRect(track.withWidth(track.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, progress))));
        }
        else
        {
            // Activity without a known end: a short bar that sweeps across.
            const auto phase = static_cast<float>(juce::Time::getMillisecondCounter() % 1400) / 1400.0f;
            const auto width = track.getWidth() * 0.22f;
            graphics.setColour(knobDeep);
            graphics.fillRect(track.withX(-width + phase * (track.getWidth() + width)).withWidth(width));
        }
    }
}

void HeaderComponent::resized()
{
    auto area = getLocalBounds().reduced(20, 0);

    auto right = area.removeFromRight(420);
    diagnosticsButton.setBounds(right.removeFromRight(32).withSizeKeepingCentre(32, 32));
    right.removeFromRight(10);
    refreshButton.setBounds(right.removeFromRight(92).withSizeKeepingCentre(92, 32));
    right.removeFromRight(16);
    bluetoothButton.setBounds(right.removeFromRight(96).withSizeKeepingCentre(96, 32));
    usbButton.setBounds(right.removeFromRight(80).withSizeKeepingCentre(80, 32));

    area.removeFromRight(16);
    deviceBounds = area;
    const auto top = area.getCentreY() - 17;
    statusDot = juce::Rectangle<int>(area.getX(), top + 6, 8, 8);
    auto line = juce::Rectangle<int>(area.getX() + 16, top, area.getWidth() - 16, 20);

    const auto deviceWidth = juce::GlyphArrangement::getStringWidthInt(deviceLabel.getFont(), deviceLabel.getText()) + 2;
    deviceLabel.setBounds(line.removeFromLeft(deviceWidth));
    line.removeFromLeft(8);
    if (firmwareLabel.isVisible())
    {
        const auto firmwareWidth = juce::GlyphArrangement::getStringWidthInt(firmwareLabel.getFont(), firmwareLabel.getText()) + 12;
        firmwareLabel.setBounds(line.removeFromLeft(firmwareWidth).withSizeKeepingCentre(firmwareWidth, 18));
        line.removeFromLeft(10);
    }
    stateLabel.setBounds(line.withTrimmedTop(1));
    detailLabel.setBounds(area.getX() + 16, top + 22, area.getWidth() - 16, 16);
}
