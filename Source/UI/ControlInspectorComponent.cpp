#include "ControlInspectorComponent.h"

#include "MixerLookAndFeel.h"

namespace
{
juce::String number(std::uint8_t value)
{
    return juce::String(static_cast<int>(value));
}

juce::String channel(std::uint8_t value)
{
    return "Channel " + juce::String(static_cast<int>(value) + 1);
}

juce::String namedValue(const char* name, std::uint8_t value)
{
    return name != nullptr ? juce::String(name) : number(value);
}

juce::String ccText(std::uint8_t value)
{
    juce::String description = "Undefined";
    switch (value)
    {
        case 0: description = "Bank Select"; break;
        case 1: description = "Modulation"; break;
        case 7: description = "Channel Volume"; break;
        case 10: description = "Pan"; break;
        case 11: description = "Expression"; break;
        case 40: description = "Balance LSB"; break;
        default: break;
    }
    return number(value) + " / " + description;
}

bool wholeNumber(const juce::String& text, int minimum, int maximum, int& parsed)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed.length() > 3 || !trimmed.containsOnly("0123456789"))
        return false;
    parsed = trimmed.getIntValue();
    return parsed >= minimum && parsed <= maximum;
}

juce::String hexBytes(const Bytes& bytes)
{
    juce::String text;
    for (const auto byte : bytes)
    {
        if (text.isNotEmpty())
            text += " ";
        text += juce::String::toHexString(static_cast<int>(byte)).paddedLeft('0', 2).toUpperCase();
    }
    return text;
}

constexpr int profileRadioGroupId = 1;

}

ControlInspectorComponent::ControlInspectorComponent()
{
    titleLabel.setText("SELECTED CONTROL", juce::dontSendNotification);
    titleLabel.setFont(MixerLookAndFeel::font(10.0f, true));
    titleLabel.setColour(juce::Label::textColourId, mixerui::muted);
    selectionLabel.setFont(MixerLookAndFeel::font(22.0f, true));
    selectionLabel.setColour(juce::Label::textColourId, mixerui::ink);
    statusLabel.setFont(MixerLookAndFeel::font(11.0f));
    statusLabel.setColour(juce::Label::textColourId, mixerui::muted);
    statusLabel.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(selectionLabel);
    addAndMakeVisible(statusLabel);

    const auto showProfile = [this](DeviceProfile profile) {
        if (activeProfile == profile)
            return;
        activeProfile = profile;
        updateProfileButtons();
        refreshFields();
    };
    ccUserButton.setClickingTogglesState(true);
    dawButton.setClickingTogglesState(true);
    ccUserButton.setRadioGroupId(profileRadioGroupId);
    dawButton.setRadioGroupId(profileRadioGroupId);
    ccUserButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    dawButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    ccUserButton.setMouseClickGrabsKeyboardFocus(false);
    dawButton.setMouseClickGrabsKeyboardFocus(false);
    const auto profileTooltip = juce::String("Shows the stored profile you are viewing. The mixer's live mode is unchanged.");
    ccUserButton.setTooltip(profileTooltip);
    dawButton.setTooltip(profileTooltip);
    ccUserButton.onClick = [showProfile] { showProfile(DeviceProfile::ccUser); };
    dawButton.onClick = [showProfile] { showProfile(DeviceProfile::daw); };
    revertButton.onClick = [this] {
        if (onRevertRequested && hasSelection)
            onRevertRequested(selected, activeProfile);
    };
    writeButton.onClick = [this] { if (onWriteRequested) onWriteRequested(); };
    exportButton.onClick = [this] { exportProfile(); };
    copyButton.onClick = [this] { copyProfile(); };
    revertButton.setTooltip("Restore this control in the profile you are viewing.");
    writeButton.setTooltip("Write changed CC / User records and read them back.");
    exportButton.setTooltip("Save the profile you are viewing as a JSON file.");
    copyButton.setTooltip("Copy the profile you are viewing to the clipboard as JSON.");
    addAndMakeVisible(ccUserButton);
    addAndMakeVisible(dawButton);
    addAndMakeVisible(revertButton);
    addAndMakeVisible(writeButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(copyButton);

    for (int index = 0; index < 6; ++index)
    {
        auto& name = fieldNames[static_cast<std::size_t>(index)];
        auto& value = fieldValues[static_cast<std::size_t>(index)];
        name.setFont(MixerLookAndFeel::font(10.0f, true));
        name.setColour(juce::Label::textColourId, mixerui::muted);
        value.setFont(MixerLookAndFeel::font(14.0f, true));
        value.setMinimumHorizontalScale(0.55f);
        addAndMakeVisible(name);
        addAndMakeVisible(value);

        auto& editor = editors[static_cast<std::size_t>(index)];
        editor.setSliderStyle(juce::Slider::LinearHorizontal);
        editor.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        editor.setScrollWheelEnabled(true);
        editor.setSliderSnapsToMousePosition(true);
        editor.setDoubleClickReturnValue(false, 0.0);
        editor.setPopupDisplayEnabled(false, false, nullptr);
        editor.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
        editor.setColour(juce::Slider::trackColourId, mixerui::slot);
        editor.setColour(juce::Slider::thumbColourId, mixerui::knobOutline);
        editor.setColour(juce::Slider::textBoxTextColourId, mixerui::ink);
        editor.setColour(juce::Slider::textBoxBackgroundColourId, mixerui::plate);
        editor.setColour(juce::Slider::textBoxOutlineColourId, mixerui::hairline);
        editor.setRange(0.0, 127.0, 1.0);
        editor.onValueChange = [this, index] { handleEditor(index); };
        editor.setVisible(false);
        addAndMakeVisible(editor);
    }

    choiceBox.setVisible(false);
    choiceBox.onChange = [this] {
        if (suppressChoiceCallbacks || !choiceField || !onControlEdited || !hasSelection)
            return;
        const auto selectedId = choiceBox.getSelectedId();
        if (selectedId <= 0)
            return;
        onControlEdited(selected, activeProfile, *choiceField, static_cast<std::uint8_t>(selectedId - 1));
    };
    addAndMakeVisible(choiceBox);

    updateProfileButtons();
    clearConfiguration();
}

void ControlInspectorComponent::updateProfileButtons()
{
    ccUserButton.setToggleState(activeProfile == DeviceProfile::ccUser, juce::dontSendNotification);
    dawButton.setToggleState(activeProfile == DeviceProfile::daw, juce::dontSendNotification);
}

void ControlInspectorComponent::showNotice(const juce::String& message, juce::Colour colour)
{
    statusLabel.setColour(juce::Label::textColourId, colour);
    statusLabel.setText(message, juce::dontSendNotification);
    statusLabel.setTooltip(message);
}

void ControlInspectorComponent::copyProfile()
{
    if (!configuration)
        return;
    const auto json = mixerui::utf8(configuration->toJson(activeProfile));
    juce::SystemClipboard::copyTextToClipboard(json);
    const juce::String name = activeProfile == DeviceProfile::ccUser ? "CC / User" : "DAW";
    showNotice("Copied the " + name + " profile to the clipboard.", mixerui::success);
}

void ControlInspectorComponent::exportProfile()
{
    if (!configuration)
        return;

    const juce::String name = activeProfile == DeviceProfile::ccUser ? "CC / User" : "DAW";
    const auto fileName = activeProfile == DeviceProfile::ccUser
        ? juce::String("SMC-Mixer-CC-User.json") : juce::String("SMC-Mixer-DAW.json");
    const auto initialFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(fileName);
    const auto json = mixerui::utf8(configuration->toJson(activeProfile));
    fileChooser = std::make_unique<juce::FileChooser>("Export " + juce::String(name) + " profile", initialFile, "*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<ControlInspectorComponent>(this), json, name](const juce::FileChooser& chooser) {
                                 if (safe == nullptr)
                                     return;
                                 const auto file = chooser.getResult();
                                 safe->fileChooser.reset();
                                 if (file == juce::File())
                                     return;
                                 if (file.replaceWithText(json))
                                 {
                                     safe->showNotice("Exported the " + name + " profile.", mixerui::success);
                                     safe->statusLabel.setTooltip(file.getFullPathName());
                                 }
                                 else
                                 {
                                     safe->showNotice("Could not write the file. Check the location and permissions.",
                                                      mixerui::error);
                                 }
                             });
}

void ControlInspectorComponent::selectControl(ControlSelection selection)
{
    selected = selection;
    hasSelection = true;
    refreshFields();

    if (!configuration && !loading && !writing && onSettingsRequired)
        onSettingsRequired();
}

void ControlInspectorComponent::setConfiguration(const ControlConfiguration& newConfiguration)
{
    configuration = newConfiguration;
    readError.clear();
    loading = false;
    writing = false;
    refreshFields();
}

void ControlInspectorComponent::cacheConfiguration(const ControlConfiguration& newConfiguration)
{
    configuration = newConfiguration;
    readError.clear();
    updateStatus();
    updateActionButtons();
}

void ControlInspectorComponent::setDirtyControls(const std::vector<ControlSelection>& dirty)
{
    dirtyControls = dirty;
    updateStatus();
    updateActionButtons();
}

void ControlInspectorComponent::setEditingEnabled(bool enabled)
{
    editingEnabled = enabled;
}

void ControlInspectorComponent::clearConfiguration()
{
    configuration.reset();
    dirtyControls.clear();
    readError.clear();
    loading = false;
    writing = false;
    loadProgress = 0.0;
    writeProgress = 0.0;
    refreshFields();
}

void ControlInspectorComponent::setLoading(bool shouldBeLoading, double progress)
{
    loading = shouldBeLoading;
    loadProgress = progress;
    if (shouldBeLoading)
    {
        writing = false;
        readError.clear();
    }
    refreshFields();
}

void ControlInspectorComponent::setWriting(bool shouldBeWriting, double progress)
{
    writeProgress = progress;
    const auto changed = writing != shouldBeWriting;
    writing = shouldBeWriting;
    if (shouldBeWriting)
    {
        loading = false;
        readError.clear();
    }
    if (changed)
        refreshFields();
    else
    {
        updateStatus();
        updateActionButtons();
    }
}

void ControlInspectorComponent::setReadError(const juce::String& message)
{
    loading = false;
    writing = false;
    loadProgress = 0.0;
    writeProgress = 0.0;
    readError = message;
    refreshFields();
}

void ControlInspectorComponent::hideFields()
{
    for (int index = 0; index < 6; ++index)
    {
        setField(index, {}, {}, false);
        editors[static_cast<std::size_t>(index)].setVisible(false);
        editorFields[static_cast<std::size_t>(index)].reset();
    }
    choiceBox.setVisible(false);
    choiceField.reset();
}

void ControlInspectorComponent::setField(int index, const juce::String& name, const juce::String& value, bool visible)
{
    auto& nameLabel = fieldNames[static_cast<std::size_t>(index)];
    auto& valueLabel = fieldValues[static_cast<std::size_t>(index)];
    nameLabel.setText(name, juce::dontSendNotification);
    valueLabel.setText(value, juce::dontSendNotification);
    nameLabel.setVisible(visible);
    valueLabel.setVisible(visible);
}

void ControlInspectorComponent::setChoice(int index,
                                          const juce::String& name,
                                          EditableField field,
                                          std::uint8_t value,
                                          const std::vector<std::uint8_t>& values,
                                          const juce::String& tooltip)
{
    auto& nameLabel = fieldNames[static_cast<std::size_t>(index)];
    auto& valueLabel = fieldValues[static_cast<std::size_t>(index)];
    nameLabel.setText(name, juce::dontSendNotification);
    nameLabel.setVisible(true);
    valueLabel.setVisible(false);
    editors[static_cast<std::size_t>(index)].setVisible(false);
    editorFields[static_cast<std::size_t>(index)].reset();

    suppressChoiceCallbacks = true;
    choiceBox.clear(juce::dontSendNotification);
    bool listed = false;
    for (const auto option : values)
    {
        const char* label = field == EditableField::mode ? buttonModeName(option)
            : selected.kind == ControlKind::slider ? sliderTypeName(option)
                                                    : knobTypeName(option);
        choiceBox.addItem(namedValue(label, option), static_cast<int>(option) + 1);
        listed = listed || option == value;
    }
    if (!listed)
        choiceBox.addItem(number(value), static_cast<int>(value) + 1);
    choiceBox.setSelectedId(static_cast<int>(value) + 1, juce::dontSendNotification);
    choiceBox.setTooltip(tooltip);
    choiceBox.setEnabled(!writing);
    choiceBox.setVisible(true);
    choiceField = field;
    choiceIndex = index;
    suppressChoiceCallbacks = false;
    choiceBox.setBounds(editors[static_cast<std::size_t>(index)].getBounds());
}

void ControlInspectorComponent::setEditor(int index,
                                          const juce::String& name,
                                          EditableField field,
                                          std::uint8_t value,
                                          int maximum,
                                          bool asChannel)
{
    auto& nameLabel = fieldNames[static_cast<std::size_t>(index)];
    auto& valueLabel = fieldValues[static_cast<std::size_t>(index)];
    auto& editor = editors[static_cast<std::size_t>(index)];
    nameLabel.setText(name, juce::dontSendNotification);
    nameLabel.setVisible(true);
    valueLabel.setVisible(false);
    editorFields[static_cast<std::size_t>(index)] = field;

    suppressEditorCallbacks = true;
    editor.setRange(0.0, static_cast<double>(maximum), 1.0);
    const auto low = asChannel ? 1 : 0;
    const auto high = asChannel ? maximum + 1 : maximum;
    const auto rangeText = juce::String(low) + "-" + juce::String(high);
    editor.setTitle(name + " (" + rangeText + ")");
    editor.setTooltip("Whole number from " + juce::String(low) + " to " + juce::String(high) + ".");
    if (asChannel)
    {
        editor.textFromValueFunction = [](double current) {
            return juce::String(juce::roundToInt(current) + 1);
        };
    }
    else
    {
        editor.textFromValueFunction = nullptr;
    }
    editor.valueFromTextFunction = [this, index, low, high, asChannel](const juce::String& text) {
        auto& currentEditor = editors[static_cast<std::size_t>(index)];
        int parsed = 0;
        if (!wholeNumber(text, low, high, parsed))
        {
            statusLabel.setColour(juce::Label::textColourId, mixerui::warning);
            statusLabel.setText("Enter a whole number from " + juce::String(low) + " to " + juce::String(high) + ".",
                                juce::dontSendNotification);
            return currentEditor.getValue();
        }
        return static_cast<double>(asChannel ? parsed - 1 : parsed);
    };
    editor.setValue(static_cast<double>(value), juce::dontSendNotification);
    editor.setEnabled(!writing);
    editor.setVisible(true);
    suppressEditorCallbacks = false;
}

void ControlInspectorComponent::handleEditor(int index)
{
    if (suppressEditorCallbacks || !onControlEdited || !hasSelection)
        return;
    const auto& field = editorFields[static_cast<std::size_t>(index)];
    if (!field)
        return;
    const auto value = static_cast<std::uint8_t>(juce::roundToInt(editors[static_cast<std::size_t>(index)].getValue()));
    onControlEdited(selected, activeProfile, *field, value);
}

bool ControlInspectorComponent::selectedRecordDirty() const
{
    return hasSelection && isRecordDirty && isRecordDirty(selected, activeProfile);
}

bool ControlInspectorComponent::anyDirty() const
{
    return !dirtyControls.empty();
}

void ControlInspectorComponent::updateActionButtons()
{
    const auto profileReady = configuration.has_value() && !loading && !writing;
    revertButton.setEnabled(selectedRecordDirty() && !loading && !writing);
    writeButton.setEnabled(anyDirty() && editingEnabled && profileReady);
    exportButton.setEnabled(profileReady);
    copyButton.setEnabled(profileReady);
    ccUserButton.setEnabled(profileReady);
    dawButton.setEnabled(profileReady);
}

void ControlInspectorComponent::updateStatus()
{
    const auto show = [this](const juce::String& text, juce::Colour colour) {
        statusLabel.setColour(juce::Label::textColourId, colour);
        statusLabel.setText(text, juce::dontSendNotification);
    };

    if (writing)
    {
        const auto percent = juce::roundToInt(juce::jlimit(0.0, 1.0, writeProgress) * 100.0);
        show("Writing changed records and reading them back... " + juce::String(percent) + "%", mixerui::ink);
        return;
    }
    if (loading)
    {
        const auto percent = juce::roundToInt(juce::jlimit(0.0, 1.0, loadProgress) * 100.0);
        show("Reading the 6,282-byte settings snapshot from the device... " + juce::String(percent) + "%",
             mixerui::ink);
        return;
    }
    if (!configuration)
    {
        show(readError.isNotEmpty()
                 ? readError
                 : hasSelection ? "Configuration is not loaded. Select the control again to retry."
                                : "Select any hardware control to read its configuration.",
             readError.isNotEmpty() ? mixerui::error : mixerui::muted);
        return;
    }
    if (readError.isNotEmpty())
    {
        show(readError, mixerui::error);
        return;
    }

    juce::String text;
    if (activeProfile == DeviceProfile::daw)
    {
        text = "Stored DAW profile. This view is read-only.";
        if (hasSelection && (selected.kind == ControlKind::trackButton || selected.kind == ControlKind::globalButton))
        {
            const auto buttonIndex = selected.kind == ControlKind::trackButton ? selected.index : selected.index + 32;
            const auto& message = configuration->profile(DeviceProfile::daw).buttons[static_cast<std::size_t>(buttonIndex)].dawMessage;
            if (!message.empty())
                text += " Message " + hexBytes(message) + ".";
        }
        if (anyDirty())
            text += " CC / User still has unsaved edits.";
    }
    else if (selectedRecordDirty())
    {
        text = "Unsaved edits on this control. Write sends changed records only.";
    }
    else if (anyDirty())
    {
        text = "Unsaved edits on other controls. Write sends changed records only.";
    }
    else
    {
        text = "Stored CC / User profile. Edits stay here until you write them.";
    }
    show(text, mixerui::muted);
}

void ControlInspectorComponent::refreshFields()
{
    hideFields();
    selectionLabel.setText(hasSelection ? mixerui::utf8(controlDisplayName(selected)) : "No control selected",
                           juce::dontSendNotification);
    updateStatus();
    updateActionButtons();

    if (loading || !configuration || !hasSelection)
        return;

    const auto showEditors = activeProfile == DeviceProfile::ccUser && editingEnabled && !writing;
    const auto& profile = configuration->profile(activeProfile);
    const std::vector<std::uint8_t> knobTypes { 0, 1, 2, 3 };
    const std::vector<std::uint8_t> sliderTypes { 0, 1, 2 };
    const std::vector<std::uint8_t> buttonModes { 0, 1, 2, 3, 4, 5 };
    const auto thumb = selected.kind == ControlKind::knob
        ? mixerui::knobOutline
        : selected.kind == ControlKind::slider ? mixerui::fader
        : selected.kind == ControlKind::trackButton ? mixerui::trackButton
                                                    : mixerui::globalButton;

    const auto showValue = [&](int index, const juce::String& name, const juce::String& value,
                               std::optional<EditableField> field, std::uint8_t raw, int maximum, bool asChannel) {
        if (showEditors && field)
        {
            auto& editor = editors[static_cast<std::size_t>(index)];
            editor.setColour(juce::Slider::thumbColourId, thumb);
            setEditor(index, name, *field, raw, raw > maximum ? 255 : maximum, asChannel);
            return;
        }
        setField(index, name, value);
    };

    switch (selected.kind)
    {
        case ControlKind::knob:
        {
            const auto& knob = profile.knobs[static_cast<std::size_t>(selected.index)];
            if (showEditors)
                setChoice(0, "TYPE", EditableField::type, knob.type, knobTypes, "CC, Aftertouch, Note, or CW.");
            else
                setField(0, "TYPE", namedValue(knobTypeName(knob.type), knob.type));
            setField(1, "SPEED", knob.speed == 2 ? juce::String("Normal") : number(knob.speed));
            showValue(2, "CHANNEL", channel(knob.channel), EditableField::channel, knob.channel, 15, true);
            showValue(3, "CC", ccText(knob.cc), EditableField::cc, knob.cc, 127, false);
            showValue(4, "MIN", number(knob.minimum), EditableField::minimum, knob.minimum, 127, false);
            showValue(5, "MAX", number(knob.maximum), EditableField::maximum, knob.maximum, 127, false);
            break;
        }
        case ControlKind::slider:
        {
            const auto& slider = profile.sliders[static_cast<std::size_t>(selected.index)];
            if (showEditors)
                setChoice(0, "TYPE", EditableField::type, slider.type, sliderTypes, "CC, Aftertouch, or DAW.");
            else
                setField(0, "TYPE", namedValue(sliderTypeName(slider.type), slider.type));
            showValue(1, "CHANNEL", channel(slider.channel), EditableField::channel, slider.channel, 15, true);
            showValue(2, "CC", ccText(slider.cc), EditableField::cc, slider.cc, 127, false);
            showValue(3, "MIN", number(slider.minimum), EditableField::minimum, slider.minimum, 127, false);
            showValue(4, "MAX", number(slider.maximum), EditableField::maximum, slider.maximum, 127, false);
            break;
        }
        case ControlKind::trackButton:
        case ControlKind::globalButton:
        {
            const auto buttonIndex = selected.kind == ControlKind::trackButton ? selected.index : selected.index + 32;
            const auto& button = profile.buttons[static_cast<std::size_t>(buttonIndex)];
            if (showEditors)
                setChoice(0, "MODE", EditableField::mode, button.mode, buttonModes,
                          "CC Single, CC Toggle, CC Push, Program, Custom, or Note. Value 1 and Value 2 stay on this button.");
            else
                setField(0, "MODE", namedValue(buttonModeName(button.mode), button.mode));
            showValue(1, "CHANNEL", channel(button.channel), EditableField::channel, button.channel, 15, true);
            showValue(2, "CC", ccText(button.cc), EditableField::cc, button.cc, 127, false);
            showValue(3, "VALUE 1", number(button.value1), EditableField::value1, button.value1, 127, false);
            showValue(4, "VALUE 2", number(button.value2), EditableField::value2, button.value2, 127, false);
            showValue(5, "LED", number(button.led), EditableField::led, button.led, 255, false);
            break;
        }
    }
}

void ControlInspectorComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintPlate(graphics, getLocalBounds());
    juce::Colour mark = mixerui::hairline;
    if (hasSelection)
    {
        switch (selected.kind)
        {
            case ControlKind::knob: mark = mixerui::knob; break;
            case ControlKind::slider: mark = mixerui::fader; break;
            case ControlKind::trackButton: mark = mixerui::trackButton; break;
            case ControlKind::globalButton: mark = mixerui::globalButton; break;
        }
    }
    graphics.setColour(mark);
    graphics.fillRoundedRectangle(16.0f, 16.0f, 8.0f, 28.0f, 3.0f);
}

void ControlInspectorComponent::resized()
{
    auto area = getLocalBounds().reduced(32, 14);
    titleLabel.setBounds(area.getX(), area.getY(), 220, 16);

    const int gap = 8;
    const int actionWidth = 84;
    const int copyWidth = 72;
    const int ccWidth = 118;
    const int dawWidth = 72;
    const int y = area.getY() + 8;
    int x = area.getRight();
    x -= dawWidth;
    dawButton.setBounds(x, y, dawWidth, 34);
    x -= ccWidth;
    ccUserButton.setBounds(x, y, ccWidth, 34);
    x -= gap + copyWidth;
    copyButton.setBounds(x, y, copyWidth, 34);
    x -= gap + actionWidth;
    exportButton.setBounds(x, y, actionWidth, 34);
    x -= gap + actionWidth;
    writeButton.setBounds(x, y, actionWidth, 34);
    x -= gap + actionWidth;
    revertButton.setBounds(x, y, actionWidth, 34);

    const auto textWidth = juce::jmax(120, x - gap - area.getX());
    selectionLabel.setBounds(area.getX(), area.getY() + 16, textWidth, 30);
    statusLabel.setBounds(area.getX(), area.getY() + 48, textWidth, 18);

    auto fields = area.removeFromBottom(78);
    const auto width = fields.getWidth() / 6;
    for (int index = 0; index < 6; ++index)
    {
        auto field = fields.withLeft(fields.getX() + index * width).withWidth(width).reduced(6, 0);
        fieldNames[static_cast<std::size_t>(index)].setBounds(field.removeFromTop(16));
        field.removeFromTop(4);
        fieldValues[static_cast<std::size_t>(index)].setBounds(field);
        editors[static_cast<std::size_t>(index)].setBounds(field);
        if (choiceBox.isVisible() && choiceIndex == index)
            choiceBox.setBounds(field);
    }
}
