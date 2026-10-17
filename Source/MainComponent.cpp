#include "MainComponent.h"

#include "BuildInfo.h"

namespace
{
const juce::String ellipsis = juce::String::fromUTF8("\xe2\x80\xa6");

juce::String records(int count)
{
    return juce::String(count) + (count == 1 ? " changed record" : " changed records");
}
}

MainComponent::MainComponent(Options options)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);
    setOpaque(true);

    confirm = [this](const Confirmation& confirmation, std::function<void(bool)> answer) {
        auto box = juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon, confirmation.title,
                                                                confirmation.message, confirmation.confirmText,
                                                                confirmation.cancelText, this);
        // With two buttons the first returns 1.
        messageBox = juce::NativeMessageBox::showScopedAsync(box, [reply = std::move(answer)](int result) { reply(result == 1); });
    };

    addAndMakeVisible(headerBar);
    addAndMakeVisible(overviewView);
    addChildComponent(diagnosticsView);
    addChildComponent(toastView);
    toastView.onLayoutChange = [this] { resized(); };

    headerBar.onTransportChosen = [this](TransportKind kind) { chooseTransport(kind); };
    headerBar.onRefresh = [this] { refresh(); };
    headerBar.onDiagnosticsToggled = [this](bool show) { showDiagnostics(show); };

    overviewView.onProfileChosen = [this](DeviceProfile profile) { chooseProfile(profile); };
    overviewView.onWrite = [this] { write(); };
    overviewView.onRevertAll = [this] { revertAll(); };
    overviewView.onUndo = [this] { undo(); };
    overviewView.onRedo = [this] { redo(); };
    overviewView.onExport = [this] { exportProfile(); };
    overviewView.onCopy = [this] { copyProfile(); };
    overviewView.diagram().onControlSelected = [this](ControlSelection control) { selectControl(control); };
    auto& inspector = overviewView.inspector();
    inspector.onControlEdited = [this](ControlSelection control, DeviceProfile profile, EditableField field,
                                       std::uint8_t value, bool continuing) {
        editControl(control, profile, field, value, continuing);
    };
    inspector.onRevertRequested = [this](ControlSelection, DeviceProfile) { revertControl(); };
    inspector.onReadRequested = [this] { readSettings(); };
    inspector.onNotice = [this](const juce::String& message, mixerui::Tone tone) { notify(message, tone); };

    diagnosticsView.onReadSettings = [this] { readSettings(); };
    diagnosticsView.onNotice = [this](const juce::String& message, mixerui::Tone tone) { notify(message, tone); };
    diagnosticsView.setActiveTransport(transport);

    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    addKeyListener(commandManager.getKeyMappings());

    publish();
    startTimer(50);
    if (options.connectOnStart)
        refreshNow();
    else
        headerBar.setStatus({ mixerui::Tone::neutral, "Ready", "Choose USB-C or Bluetooth, then Refresh.", {} });
}

MainComponent::~MainComponent()
{
    stopTimer();
    removeKeyListener(commandManager.getKeyMappings());
    controller.shutdown();
    setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

bool MainComponent::hasUnsavedEdits() const
{
    return settings.isDirty();
}

bool MainComponent::editingEnabled() const
{
    return activity == Activity::idle && settings.loaded() && viewProfile == DeviceProfile::ccUser;
}

juce::String MainComponent::linkName() const
{
    return transport == TransportKind::bluetooth ? "Bluetooth" : "USB-C";
}

void MainComponent::notify(const juce::String& message, mixerui::Tone tone)
{
    toastView.show(message, tone);
}

void MainComponent::ask(const Confirmation& confirmation, std::function<void()> onConfirmed)
{
    if (choicePending || !confirm)
        return;
    choicePending = true;
    confirm(confirmation, [safe = juce::Component::SafePointer<MainComponent>(this), action = std::move(onConfirmed)](bool yes) {
        if (safe == nullptr)
            return;
        safe->choicePending = false;
        if (yes && action)
            action();
    });
}

void MainComponent::askBeforeDiscarding(const juce::String& action, std::function<void()> onConfirmed)
{
    if (!settings.isDirty())
    {
        onConfirmed();
        return;
    }
    const auto count = static_cast<int>(settings.dirtySpans().size());
    ask({ "Discard unsaved edits?",
          action + " discards " + records(count) + " that " + (count == 1 ? "has" : "have")
              + " not been written to the mixer.",
          "Discard edits", "Keep editing" },
        std::move(onConfirmed));
}

void MainComponent::requestQuit(std::function<void()> quit)
{
    if (activity == Activity::writing)
    {
        ask({ "A write is in progress",
              "Quitting now stops the write before the mixer saves. Records that already reached it stay in its "
              "memory until it is switched off.",
              "Quit anyway", "Keep writing" },
            std::move(quit));
        return;
    }
    if (settings.isDirty())
    {
        askBeforeDiscarding("Quitting", std::move(quit));
        return;
    }
    quit();
}

void MainComponent::setActivity(Activity next)
{
    activity = next;
    const auto busy = activity != Activity::idle;
    headerBar.setBusy(busy);
    diagnosticsView.setBusy(busy);
    headerBar.setProgress(activity == Activity::refreshing ? -1.0 : busy ? 0.0 : -2.0);
}

void MainComponent::publish()
{
    if (settings.loaded())
        configuration = settings.configuration();
    else
        configuration.reset();

    const ControlProfile* profile = configuration ? &configuration->profile(viewProfile) : nullptr;
    // Edit dots mark the profile on screen; the DAW profile never has any.
    std::vector<ControlSelection> dirty;
    for (const auto& control : settings.dirtyControls())
        if (settings.isRecordDirty(control, viewProfile))
            dirty.push_back(control);
    auto& diagram = overviewView.diagram();
    diagram.setProfile(profile);
    diagram.setDirtyControls(dirty);
    diagram.setSelection(selection);

    ControlInspectorComponent::View view;
    view.profile = profile;
    view.profileKind = viewProfile;
    view.selection = selection;
    view.editable = editingEnabled();
    view.selectionDirty = selection && settings.isRecordDirty(*selection, viewProfile);
    if (profile == nullptr)
    {
        switch (activity)
        {
            case Activity::refreshing: view.emptyMessage = "Connecting over " + linkName() + ellipsis; break;
            case Activity::readingSettings: view.emptyMessage = "Reading the settings from the mixer" + ellipsis; break;
            case Activity::writing:
            case Activity::idle:
                view.emptyMessage = settingsProblem.isNotEmpty()
                    ? "The settings could not be read. " + settingsProblem
                    : juce::String("The settings are read when the mixer connects. Refresh to connect again.");
                view.offerRead = snapshot.has_value() || settingsProblem.isNotEmpty();
                break;
        }
    }
    overviewView.inspector().show(view);

    const auto changed = static_cast<int>(settings.dirtySpans().size());
    const auto idle = activity == Activity::idle;
    overviewView.setActions({ changed,
                              idle && changed > 0,
                              idle && changed > 0,
                              idle && settings.canUndo(),
                              idle && settings.canRedo(),
                              profile != nullptr,
                              profile != nullptr });
    overviewView.setProfileKind(viewProfile);
    commandManager.commandStatusChanged();
}

void MainComponent::showOnlineStatus()
{
    if (!snapshot)
        return;
    const auto& device = *snapshot;
    juce::StringArray facts { linkName() };
    juce::String tooltip;
    if (device.transport == TransportKind::usbC)
    {
        facts.add(mixerui::utf8(device.sourceName));
        tooltip = "MIDI in: " + mixerui::utf8(device.sourceName) + "\nMIDI out: " + mixerui::utf8(device.destinationName);
    }
    else if (device.hasHealth)
    {
        if (device.health.battery)
            facts.add("Battery " + juce::String(*device.health.battery) + "%");
        if (device.health.rssi)
            facts.add(juce::String(*device.health.rssi) + " dBm");
        if (device.health.manufacturer)
            facts.add(mixerui::utf8(*device.health.manufacturer));
    }
    else
    {
        facts.add("Battery and signal unavailable");
        tooltip = "Refresh over Bluetooth to try reading battery and signal again.";
    }

    const auto tone = device.warning.empty() ? mixerui::Tone::success : mixerui::Tone::warning;
    headerBar.setStatus({ tone, "Online", facts.joinIntoString(juce::String::fromUTF8("  \xc2\xb7  ")), tooltip });
}

void MainComponent::refresh()
{
    if (activity != Activity::idle)
        return;
    askBeforeDiscarding("Reading the mixer again", [this] {
        if (activity == Activity::idle)
            refreshNow();
    });
}

void MainComponent::refreshNow()
{
    if (!controller.requestRefresh(transport, true))
        return;
    settings.clear();
    snapshot.reset();
    settingsProblem.clear();
    diagnosticsView.clearSnapshot();
    diagnosticsView.refreshEndpoints();
    headerBar.setDevice("SMC-Mixer", {});
    headerBar.setStatus({ mixerui::Tone::busy, "Connecting",
                          transport == TransportKind::usbC ? "Looking for the mixer on USB-C" + ellipsis
                                                           : "Scanning Bluetooth for the mixer" + ellipsis,
                          {} });
    setActivity(Activity::refreshing);
    publish();
}

void MainComponent::readSettings()
{
    if (activity != Activity::idle)
        return;
    askBeforeDiscarding("Reading the settings again", [this] {
        if (activity == Activity::idle)
            readSettingsNow();
    });
}

void MainComponent::readSettingsNow()
{
    if (!controller.requestSettings(transport))
        return;
    // The settings on screen stay, read-only, until the new image arrives.
    settingsProblem.clear();
    headerBar.setStatus({ mixerui::Tone::busy, "Reading settings", "Reading 6,282 bytes. Keep the mixer connected.", {} });
    setActivity(Activity::readingSettings);
    publish();
}

void MainComponent::write()
{
    if (activity != Activity::idle || !settings.isDirty())
        return;
    const auto count = static_cast<int>(settings.dirtySpans().size());
    ask({ "Write to the mixer?",
          "Write " + records(count) + " over " + linkName()
              + ". Each record is read back and checked, then the mixer saves them to flash. Its live mode stays as it is.",
          "Write", "Cancel" },
        [this] { startWrite(); });
}

void MainComponent::startWrite()
{
    if (activity != Activity::idle || !settings.isDirty())
        return;
    if (!controller.requestWrite(transport, settings.dirtySpans()))
        return;
    headerBar.setStatus({ mixerui::Tone::busy, "Writing", "Writing, checking, and saving the changed records" + ellipsis, {} });
    setActivity(Activity::writing);
    publish();
}

void MainComponent::chooseTransport(TransportKind kind)
{
    if (activity != Activity::idle || kind == transport)
    {
        headerBar.setTransport(transport);
        return;
    }
    headerBar.setTransport(transport);
    askBeforeDiscarding("Switching the link", [this, kind] {
        if (activity != Activity::idle)
            return;
        transport = kind;
        headerBar.setTransport(transport);
        diagnosticsView.setActiveTransport(transport);
        refreshNow();
    });
}

void MainComponent::chooseProfile(DeviceProfile profile)
{
    viewProfile = profile;
    publish();
}

void MainComponent::showDiagnostics(bool show)
{
    showingDiagnostics = show;
    headerBar.setDiagnosticsShown(show);
    overviewView.setVisible(!show);
    diagnosticsView.setVisible(show);
    if (show)
        diagnosticsView.refreshEndpoints();
    commandManager.commandStatusChanged();
}

void MainComponent::selectControl(ControlSelection control)
{
    if (!isValidControl(control))
        return;
    selection = control;
    publish();
}

void MainComponent::editControl(ControlSelection control, DeviceProfile profile, EditableField field,
                                std::uint8_t value, bool continuing)
{
    if (!editingEnabled())
        return;
    const auto result = settings.apply(control, profile, field, value, continuing);
    if (!result.message.empty())
        notify(mixerui::utf8(result.message), result.applied ? mixerui::Tone::neutral : mixerui::Tone::warning);
    publish();
}

void MainComponent::revertControl()
{
    if (!selection || !editingEnabled() || !settings.isRecordDirty(*selection, viewProfile))
        return;
    settings.revert(*selection, viewProfile);
    publish();
}

void MainComponent::revertAll()
{
    if (activity != Activity::idle || !settings.isDirty())
        return;
    settings.revertAll();
    notify("Every unsaved edit was reverted. Undo brings them back.", mixerui::Tone::neutral);
    publish();
}

void MainComponent::undo()
{
    if (activity == Activity::idle && settings.undo())
        publish();
}

void MainComponent::redo()
{
    if (activity == Activity::idle && settings.redo())
        publish();
}

void MainComponent::exportProfile()
{
    if (!configuration)
        return;
    const auto name = mixerui::utf8(profileName(viewProfile));
    const auto fileName = viewProfile == DeviceProfile::ccUser ? "SMC-Mixer-CC-User.json" : "SMC-Mixer-DAW.json";
    const auto json = mixerui::utf8(configuration->toJson(viewProfile));
    fileChooser = std::make_unique<juce::FileChooser>(
        "Export the " + name + " profile",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(fileName), "*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<MainComponent>(this), json, name](const juce::FileChooser& chooser) {
                                 if (safe == nullptr)
                                     return;
                                 const auto file = chooser.getResult();
                                 safe->fileChooser.reset();
                                 if (file == juce::File())
                                     return;
                                 if (file.replaceWithText(json))
                                     safe->notify("Exported the " + name + " profile to " + file.getFileName() + ".",
                                                  mixerui::Tone::success);
                                 else
                                     safe->notify("Could not write the file. Check the location and permissions.",
                                                  mixerui::Tone::error);
                             });
}

void MainComponent::copyProfile()
{
    if (!configuration)
        return;
    juce::SystemClipboard::copyTextToClipboard(mixerui::utf8(configuration->toJson(viewProfile)));
    notify("Copied the " + mixerui::utf8(profileName(viewProfile)) + " profile as JSON.", mixerui::Tone::success);
}

void MainComponent::showAbout()
{
    auto box = juce::MessageBoxOptions::makeOptionsOk(
        juce::MessageBoxIconType::NoIcon, "SMC Mixer Tool",
        "Version " + juce::String(kSmcVersion)
            + "\n\nReads and edits the CC / User profile of the M-VAVE SMC-Mixer over USB-C or Bluetooth.",
        "OK", this);
    messageBox = juce::NativeMessageBox::showScopedAsync(box, nullptr);
}

void MainComponent::timerCallback()
{
    for (auto& event : controller.pollEvents())
        deliver(std::move(event));
}

void MainComponent::deliver(DeviceEvent event)
{
    switch (event.kind)
    {
        case DeviceEvent::Kind::snapshotReady:
        {
            snapshot = event.snapshot;
            diagnosticsView.setSnapshot(event.snapshot);
            headerBar.setDevice(mixerui::utf8(event.snapshot.version.deviceName), mixerui::utf8(event.snapshot.version.versionLabel));
            if (activity == Activity::refreshing)
            {
                // The same request reads the settings next.
                setActivity(Activity::readingSettings);
                headerBar.setStatus({ mixerui::Tone::busy, "Reading settings", "Reading 6,282 bytes. Keep the mixer connected.", {} });
            }
            else
            {
                setActivity(Activity::idle);
                showOnlineStatus();
            }
            break;
        }
        case DeviceEvent::Kind::settingsProgress:
        {
            headerBar.setProgress(event.progress);
            const auto percent = juce::String(juce::roundToInt(juce::jlimit(0.0, 1.0, event.progress) * 100.0)) + "%";
            if (event.writeOperation)
                headerBar.setStatus({ mixerui::Tone::busy, "Writing", "Writing, checking, and saving " + ellipsis + " " + percent, {} });
            else
                headerBar.setStatus({ mixerui::Tone::busy, "Reading settings", "Reading 6,282 bytes " + ellipsis + " " + percent, {} });
            break;
        }
        case DeviceEvent::Kind::settingsReady:
        {
            setActivity(Activity::idle);
            diagnosticsView.setSettings(event.settings);
            try
            {
                settings.load(event.settings.data);
                settingsProblem.clear();
                if (!selection)
                    selection = ControlSelection { ControlKind::knob, 0 };
                showOnlineStatus();
            }
            catch (const std::exception& error)
            {
                settingsProblem = mixerui::utf8(error.what());
                headerBar.setStatus({ mixerui::Tone::error, "Settings unreadable", settingsProblem, settingsProblem });
                diagnosticsView.setLastError(settingsProblem);
            }
            break;
        }
        case DeviceEvent::Kind::settingsWritten:
        {
            setActivity(Activity::idle);
            try
            {
                settings.noteVerified(event.written);
                diagnosticsView.adoptWorkingImage(settings.workingBytes());
                showOnlineStatus();
                notify("Wrote " + records(static_cast<int>(event.written.size()))
                           + ". The mixer read them back and saved them to flash.",
                       mixerui::Tone::success);
            }
            catch (const std::exception& error)
            {
                const auto detail = mixerui::utf8(error.what());
                headerBar.setStatus({ mixerui::Tone::error, "Write unclear",
                                      "The mixer saved the write, but it differs from the edits on screen. Read the settings again.",
                                      detail });
                diagnosticsView.setLastError(detail);
            }
            break;
        }
        case DeviceEvent::Kind::operationFailed:
        {
            setActivity(Activity::idle);
            const auto detail = mixerui::utf8(event.message);
            juce::String state;
            juce::String summary;
            if (event.writeOperation)
            {
                state = "Write failed";
                summary = "Your edits are kept, so Write can try again.";
            }
            else if (event.settingsOperation)
            {
                state = "Settings unreadable";
                summary = "Check the connection, then read the settings again.";
                settingsProblem = detail;
            }
            else
            {
                state = "Not connected";
                summary = transport == TransportKind::bluetooth
                    ? juce::String("Check the macOS Bluetooth permission and Audio MIDI Setup.")
                    : juce::String("Check the cable and that the mixer is switched on.");
                snapshot.reset();
                headerBar.setDevice("SMC-Mixer", {});
                diagnosticsView.clearSnapshot();
            }
            headerBar.setStatus({ mixerui::Tone::error, state, detail.isNotEmpty() ? detail : summary, summary + "\n" + detail });
            diagnosticsView.setLastError(state + ": " + detail);
            if (event.writeOperation)
                notify("Write failed. " + summary, mixerui::Tone::error);
            break;
        }
    }
    publish();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& ids)
{
    ids.addArray({ refreshCommand, readSettingsCommand, writeCommand, exportProfileCommand, copyProfileCommand,
                   exportImageCommand, undoCommand, redoCommand, revertControlCommand, revertAllCommand,
                   showCcUserCommand, showDawCommand, toggleDiagnosticsCommand, useUsbCommand, useBluetoothCommand,
                   aboutCommand });
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto idle = activity == Activity::idle;
    const auto loaded = configuration.has_value();
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    switch (id)
    {
        case refreshCommand:
            info.setInfo("Refresh", "Read the identity and settings again", "Device", 0);
            info.addDefaultKeypress('r', command);
            info.setActive(idle);
            break;
        case readSettingsCommand:
            info.setInfo("Read Settings", "Read the settings image again", "Device", 0);
            info.addDefaultKeypress('r', command | shift);
            info.setActive(idle);
            break;
        case writeCommand:
            info.setInfo("Write Changes" + ellipsis, "Write the changed records to the mixer", "Device", 0);
            info.addDefaultKeypress('s', command);
            info.setActive(idle && settings.isDirty());
            break;
        case useUsbCommand:
            info.setInfo("Use USB-C", "Connect over the USB-C cable", "Device", 0);
            info.setActive(idle);
            info.setTicked(transport == TransportKind::usbC);
            break;
        case useBluetoothCommand:
            info.setInfo("Use Bluetooth", "Connect over Bluetooth", "Device", 0);
            info.setActive(idle);
            info.setTicked(transport == TransportKind::bluetooth);
            break;
        case exportProfileCommand:
            info.setInfo("Export Profile" + ellipsis, "Save the profile on screen as JSON", "File", 0);
            info.addDefaultKeypress('e', command);
            info.setActive(loaded);
            break;
        case copyProfileCommand:
            info.setInfo("Copy Profile as JSON", "Copy the profile on screen as JSON", "File", 0);
            info.addDefaultKeypress('c', command | shift);
            info.setActive(loaded);
            break;
        case exportImageCommand:
            info.setInfo("Export Settings Image" + ellipsis, "Save the 6,282-byte settings image", "File", 0);
            info.setActive(idle && settings.loaded());
            break;
        case undoCommand:
            info.setInfo("Undo", "Undo the last edit", "Edit", 0);
            info.addDefaultKeypress('z', command);
            info.setActive(idle && settings.canUndo());
            break;
        case redoCommand:
            info.setInfo("Redo", "Redo the last undone edit", "Edit", 0);
            info.addDefaultKeypress('z', command | shift);
            info.setActive(idle && settings.canRedo());
            break;
        case revertControlCommand:
            info.setInfo("Revert Control", "Restore the selected control as read", "Edit", 0);
            info.addDefaultKeypress(juce::KeyPress::backspaceKey, command);
            info.setActive(selection.has_value() && editingEnabled() && settings.isRecordDirty(*selection, viewProfile));
            break;
        case revertAllCommand:
            info.setInfo("Revert All Changes", "Discard every unsaved edit", "Edit", 0);
            info.setActive(idle && settings.isDirty());
            break;
        case showCcUserCommand:
            info.setInfo("CC / User Profile", "Show the CC / User profile", "View", 0);
            info.addDefaultKeypress('1', command);
            info.setActive(loaded);
            info.setTicked(viewProfile == DeviceProfile::ccUser);
            break;
        case showDawCommand:
            info.setInfo("DAW Profile", "Show the DAW profile", "View", 0);
            info.addDefaultKeypress('2', command);
            info.setActive(loaded);
            info.setTicked(viewProfile == DeviceProfile::daw);
            break;
        case toggleDiagnosticsCommand:
            info.setInfo("Diagnostics", "Show the link report", "View", 0);
            info.addDefaultKeypress('d', command | shift);
            info.setTicked(showingDiagnostics);
            break;
        case aboutCommand:
            info.setInfo("About SMC Mixer Tool", "Show the version", "Help", 0);
            break;
        default:
            break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case refreshCommand: refresh(); return true;
        case readSettingsCommand: readSettings(); return true;
        case writeCommand: write(); return true;
        case useUsbCommand: chooseTransport(TransportKind::usbC); return true;
        case useBluetoothCommand: chooseTransport(TransportKind::bluetooth); return true;
        case exportProfileCommand: exportProfile(); return true;
        case copyProfileCommand: copyProfile(); return true;
        case exportImageCommand: diagnosticsView.exportSettings(); return true;
        case undoCommand: undo(); return true;
        case redoCommand: redo(); return true;
        case revertControlCommand: revertControl(); return true;
        case revertAllCommand: revertAll(); return true;
        case showCcUserCommand: chooseProfile(DeviceProfile::ccUser); return true;
        case showDawCommand: chooseProfile(DeviceProfile::daw); return true;
        case toggleDiagnosticsCommand: showDiagnostics(!showingDiagnostics); return true;
        case aboutCommand: showAbout(); return true;
        default: return false;
    }
}

juce::StringArray MainComponent::Menu::getMenuBarNames()
{
    return { "File", "Edit", "View", "Device" };
}

juce::PopupMenu MainComponent::Menu::getMenuForIndex(int index, const juce::String&)
{
    auto& manager = main.commandManager;
    juce::PopupMenu popup;
    switch (index)
    {
        case 0:
            popup.addCommandItem(&manager, exportProfileCommand);
            popup.addCommandItem(&manager, copyProfileCommand);
            popup.addSeparator();
            popup.addCommandItem(&manager, exportImageCommand);
            break;
        case 1:
            popup.addCommandItem(&manager, undoCommand);
            popup.addCommandItem(&manager, redoCommand);
            popup.addSeparator();
            popup.addCommandItem(&manager, revertControlCommand);
            popup.addCommandItem(&manager, revertAllCommand);
            break;
        case 2:
            popup.addCommandItem(&manager, showCcUserCommand);
            popup.addCommandItem(&manager, showDawCommand);
            popup.addSeparator();
            popup.addCommandItem(&manager, toggleDiagnosticsCommand);
            break;
        case 3:
            popup.addCommandItem(&manager, useUsbCommand);
            popup.addCommandItem(&manager, useBluetoothCommand);
            popup.addSeparator();
            popup.addCommandItem(&manager, refreshCommand);
            popup.addCommandItem(&manager, readSettingsCommand);
            popup.addCommandItem(&manager, writeCommand);
            break;
        default:
            break;
    }
    return popup;
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(mixerui::window);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    headerBar.setBounds(area.removeFromTop(HeaderComponent::preferredHeight));
    overviewView.setBounds(area);
    diagnosticsView.setBounds(area);
    if (toastView.isVisible())
        toastView.setBounds(toastView.preferredBounds(getLocalBounds()));
}
