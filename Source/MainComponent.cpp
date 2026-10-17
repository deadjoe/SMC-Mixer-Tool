#include "MainComponent.h"

GearButton::GearButton()
    : juce::Button("Diagnostics")
{
    setTooltip("Diagnostics");
}

void GearButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(5.0f);
    const auto centre = bounds.getCentre();
    const auto outer = bounds.getWidth() * 0.48f;
    const auto inner = outer * 0.62f;
    const auto hole = outer * 0.28f;
    graphics.setColour(getToggleState() || down || highlighted ? mixerui::ink : mixerui::muted);

    juce::Path teeth;
    for (int index = 0; index < 8; ++index)
    {
        juce::Rectangle<float> tooth(-1.35f, -outer, 2.7f, outer - inner + 1.6f);
        teeth.addRectangle(tooth.transformedBy(juce::AffineTransform::rotation(
            juce::MathConstants<float>::twoPi * static_cast<float>(index) / 8.0f, 0.0f, 0.0f)));
    }
    teeth.addEllipse(-inner, -inner, inner * 2.0f, inner * 2.0f);
    graphics.fillPath(teeth, juce::AffineTransform::translation(centre.x, centre.y));

    graphics.setColour(mixerui::plate);
    graphics.fillEllipse(centre.x - hole, centre.y - hole, hole * 2.0f, hole * 2.0f);
}

MainComponent::MainComponent()
    : queryProgress(progressValue)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("SMC Mixer Tool", juce::dontSendNotification);
    titleLabel.setFont(MixerLookAndFeel::font(24.0f, true));
    versionLabel.setText(kSmcVersion, juce::dontSendNotification);
    versionLabel.setFont(MixerLookAndFeel::font(11.0f));
    versionLabel.setColour(juce::Label::textColourId, mixerui::muted);
    versionLabel.setMinimumHorizontalScale(0.7f);
    versionLabel.setTooltip("Version and build hash");
    subtitleLabel.setText("CC / User settings for the M-VAVE SMC-Mixer", juce::dontSendNotification);
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

    for (auto* label : { &titleLabel, &versionLabel, &subtitleLabel, &statusLabel, &detailLabel, &connectionCaption })
        addAndMakeVisible(*label);

    gearButton.onClick = [this] { showDiagnostics(!showingDiagnostics); };
    gearButton.setMouseClickGrabsKeyboardFocus(false);
    usbButton.onClick = [this] { selectTransport(TransportKind::usbC); };
    bluetoothButton.onClick = [this] { selectTransport(TransportKind::bluetooth); };
    refreshButton.onClick = [this] { refreshDevice(); };
    for (auto* button : { &usbButton, &bluetoothButton, &refreshButton })
        addAndMakeVisible(*button);
    addAndMakeVisible(gearButton);

    refreshButton.setColour(juce::TextButton::buttonColourId, mixerui::keyIdle);
    refreshButton.setColour(juce::TextButton::textColourOffId, mixerui::ink);

    queryProgress.setPercentageDisplay(false);
    queryProgress.setVisible(false);
    addAndMakeVisible(queryProgress);

    addChildComponent(overview);
    addChildComponent(diagnostics);
    diagnostics.onReadSettings = [this] { readSettings(); };
    overview.onSettingsRequired = [this] { readSettings(); };
    overview.onControlEdited = [this](ControlSelection selection, DeviceProfile profile, EditableField field, std::uint8_t value) {
        editControl(selection, profile, field, value);
    };
    overview.onRevertRequested = [this](ControlSelection selection, DeviceProfile profile) {
        revertSelected(selection, profile);
    };
    overview.onWriteRequested = [this] { writeSettings(); };
    overview.isRecordDirty = [this](ControlSelection selection, DeviceProfile profile) {
        return document.isRecordDirty(selection, profile);
    };
    diagnostics.setActiveTransport(requestedTransport);
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
    overview.onControlEdited = nullptr;
    overview.onRevertRequested = nullptr;
    overview.onWriteRequested = nullptr;
    overview.isRecordDirty = nullptr;
    controller.shutdown();
    setLookAndFeel(nullptr);
}

void MainComponent::selectTransport(TransportKind transport)
{
    if (busy)
        return;
    confirmDiscard([this, transport] {
        if (busy)
            return;
        requestedTransport = transport;
        diagnostics.setActiveTransport(transport);
        updateSelectionColours();
        refreshDeviceNow();
    });
}

void MainComponent::showDiagnostics(bool shouldShowDiagnostics)
{
    showingDiagnostics = shouldShowDiagnostics;
    overview.setVisible(!showingDiagnostics);
    diagnostics.setVisible(showingDiagnostics);
    if (shouldShowDiagnostics)
        diagnostics.refreshEndpoints();
    updateSelectionColours();
}

void MainComponent::updateSelectionColours()
{
    const auto style = [](juce::TextButton& button, bool selected) {
        button.setColour(juce::TextButton::buttonColourId, selected ? mixerui::keyDown : mixerui::keyIdle);
        button.setColour(juce::TextButton::textColourOffId, selected ? mixerui::onShell : mixerui::ink);
        button.repaint();
    };
    gearButton.setToggleState(showingDiagnostics, juce::dontSendNotification);
    style(usbButton, requestedTransport == TransportKind::usbC);
    style(bluetoothButton, requestedTransport == TransportKind::bluetooth);
}

bool MainComponent::hasUnsavedEdits() const
{
    return document.isDirty();
}

bool MainComponent::editingEnabled() const
{
    return !busy && document.loaded();
}

void MainComponent::confirmDiscard(std::function<void()> onConfirmed)
{
    if (!document.isDirty())
    {
        if (onConfirmed)
            onConfirmed();
        return;
    }
    confirmThen(juce::MessageBoxIconType::WarningIcon,
                "Unsaved edits",
                "This reads the device again and discards CC / User edits that have not been written.",
                "Read again",
                "Keep edits",
                std::move(onConfirmed));
}

void MainComponent::confirmThen(juce::MessageBoxIconType icon,
                                const juce::String& title,
                                const juce::String& message,
                                const juce::String& confirmText,
                                const juce::String& cancelText,
                                std::function<void()> onConfirmed)
{
    if (choicePending)
        return;
    choicePending = true;
    // JUCE 9 leaves modal loops off, so this returns before the click. 1 is the confirm button.
    juce::AlertWindow::showOkCancelBox(
        icon,
        title,
        message,
        confirmText,
        cancelText,
        this,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<MainComponent>(this),
             action = std::move(onConfirmed)](int result) {
                if (safe == nullptr)
                    return;
                safe->choicePending = false;
                if (result == 1 && action)
                    action();
            }));
}

void MainComponent::publishDocument(bool resetEditors)
{
    if (!document.loaded())
        return;
    const auto configuration = document.configuration();
    const auto dirty = document.dirtyControls();
    if (resetEditors)
        overview.showWorkingCopy(configuration, dirty, editingEnabled());
    else
        overview.cacheWorkingCopy(configuration, dirty);
}

void MainComponent::refreshDevice()
{
    if (busy)
        return;
    confirmDiscard([this] {
        if (!busy)
            refreshDeviceNow();
    });
}

void MainComponent::refreshDeviceNow()
{
    if (!controller.requestRefresh(requestedTransport))
        return;

    hasSnapshot = false;
    document.clear();
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
    if (busy)
    {
        overview.setSettingsError("The device is busy. Select the control again in a moment.");
        return;
    }
    confirmDiscard([this] {
        if (!busy)
            readSettingsNow();
    });
}

void MainComponent::readSettingsNow()
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

void MainComponent::editControl(ControlSelection selection,
                               DeviceProfile profile,
                               EditableField field,
                               std::uint8_t value)
{
    std::string error;
    if (!document.apply(selection, profile, field, value, error))
    {
        publishDocument(true);
        if (!error.empty())
            overview.setSettingsError(mixerui::utf8(error));
        return;
    }
    publishDocument(false);
}

void MainComponent::revertSelected(ControlSelection selection, DeviceProfile profile)
{
    if (!document.loaded())
        return;
    document.revert(selection, profile);
    publishDocument(true);
}

void MainComponent::writeSettings()
{
    if (busy || !document.isDirty())
        return;
    const auto spans = document.dirtySpans();
    if (spans.empty())
        return;

    const auto count = static_cast<int>(spans.size());
    const auto link = requestedTransport == TransportKind::bluetooth ? "Bluetooth" : "USB-C";
    confirmThen(juce::MessageBoxIconType::QuestionIcon,
                "Write settings",
                "Write " + juce::String(count) + (count == 1 ? " changed record" : " changed records")
                    + " to the SMC-Mixer over " + link + "? The mixer saves them and keeps its current mode.",
                "Write",
                "Cancel",
                [this] { startWrite(); });
}

void MainComponent::startWrite()
{
    if (busy || !document.isDirty())
        return;

    auto freshSpans = document.dirtySpans();
    if (freshSpans.empty())
        return;
    if (!controller.requestWrite(requestedTransport, std::move(freshSpans)))
    {
        overview.setSettingsError("The device is busy. Try the write again in a moment.");
        return;
    }

    overview.setSettingsWriting(true, 0.0);
    statusLabel.setColour(juce::Label::textColourId, mixerui::ink);
    statusLabel.setText("WRITING", juce::dontSendNotification);
    detailLabel.setColour(juce::Label::textColourId, mixerui::muted);
    detailLabel.setText("Writing changed records, reading them back, then saving them on the mixer.",
                        juce::dontSendNotification);
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
            if (event.writeOperation)
                overview.setSettingsWriting(true, progressValue);
            else
                overview.setSettingsLoading(true, progressValue);
            break;
        case DeviceEvent::Kind::settingsReady:
            finishBusyState();
            diagnostics.setBusy(false, 1.0, true);
            diagnostics.setSettings(event.settings);
            try
            {
                document.load(event.settings.data);
                publishDocument(true);
            }
            catch (const std::exception& error)
            {
                overview.setSettingsError(mixerui::utf8(error.what()));
                statusLabel.setColour(juce::Label::textColourId, mixerui::error);
                statusLabel.setText("SNAPSHOT FAILED", juce::dontSendNotification);
                detailLabel.setColour(juce::Label::textColourId, mixerui::error);
                detailLabel.setText("Settings snapshot could not be read.", juce::dontSendNotification);
                detailLabel.setTooltip(mixerui::utf8(error.what()));
                break;
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
        case DeviceEvent::Kind::settingsWritten:
            finishBusyState();
            diagnostics.setBusy(false, 1.0, true);
            try
            {
                document.noteVerified(event.written);
                diagnostics.adoptWorkingImage(document.workingBytes());
                publishDocument(true);
                statusLabel.setColour(juce::Label::textColourId, mixerui::success);
                statusLabel.setText(hasSnapshot ? "ONLINE / " + mixerui::utf8(currentSnapshot.version.versionLabel)
                                                : "WRITE VERIFIED",
                                    juce::dontSendNotification);
                detailLabel.setColour(juce::Label::textColourId, mixerui::success);
                detailLabel.setText("Write verified and saved. Read-back matches, and the mixer stored it in flash.",
                                    juce::dontSendNotification);
            }
            catch (const std::exception& error)
            {
                publishDocument(true);
                overview.setSettingsError("The mixer saved the write, but it no longer matches the edits on screen."
                                          " Read the snapshot again.");
                statusLabel.setColour(juce::Label::textColourId, mixerui::error);
                statusLabel.setText("WRITE FAILED", juce::dontSendNotification);
                detailLabel.setColour(juce::Label::textColourId, mixerui::error);
                detailLabel.setText("Saved, but the edits on screen differ. Read the snapshot again.",
                                    juce::dontSendNotification);
                detailLabel.setTooltip(mixerui::utf8(error.what()));
            }
            break;
        case DeviceEvent::Kind::operationFailed:
        {
            finishBusyState();
            diagnostics.setBusy(false, 0.0, event.settingsOperation);
            if (event.writeOperation)
                publishDocument(true);
            const auto summary = event.writeOperation
                ? juce::String("Settings write failed. Your edits are kept, so Write can try again.")
                : event.settingsOperation
                    ? juce::String("Settings read failed. Check the connection and retry.")
                    : event.transport == TransportKind::bluetooth
                        ? juce::String("Bluetooth failed. Check macOS permission and Audio MIDI Setup.")
                        : juce::String("USB-C device not found or not responding.");
            auto detail = summary;
            if (!event.message.empty())
                detail += "\n" + mixerui::utf8(event.message);
            const auto visible = event.message.empty() ? summary : mixerui::utf8(event.message);
            diagnostics.setError(detail);
            if (event.settingsOperation)
                overview.setSettingsError(visible);
            statusLabel.setColour(juce::Label::textColourId, mixerui::error);
            statusLabel.setText(event.writeOperation ? "WRITE FAILED"
                                    : event.settingsOperation ? "SNAPSHOT FAILED" : "CONNECTION FAILED",
                                juce::dontSendNotification);
            detailLabel.setColour(juce::Label::textColourId, mixerui::error);
            detailLabel.setText(visible, juce::dontSendNotification);
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
    titleLabel.setBounds(24, 14, 240, 28);
    versionLabel.setBounds(268, 20, 250, 18);
    subtitleLabel.setBounds(24, 42, 460, 18);
    statusLabel.setBounds(getWidth() - 560, 14, 400, 24);
    detailLabel.setBounds(getWidth() - 560, 40, 400, 18);
    refreshButton.setBounds(getWidth() - 128, 18, 104, 34);

    gearButton.setBounds(24, 70, 28, 28);
    bluetoothButton.setBounds(getWidth() - 128, 68, 104, 32);
    usbButton.setBounds(getWidth() - 220, 68, 86, 32);
    connectionCaption.setBounds(getWidth() - 318, 68, 90, 32);
    queryProgress.setBounds(0, 108, getWidth(), 3);

    overview.setBounds(0, 109, getWidth(), getHeight() - 109);
    diagnostics.setBounds(0, 109, getWidth(), getHeight() - 109);
}
