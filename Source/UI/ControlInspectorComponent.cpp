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

    ccProfileButton.onClick = [this] {
        activeProfile = DeviceProfile::ccUser;
        updateProfileButtons();
        refreshFields();
    };
    dawProfileButton.onClick = [this] {
        activeProfile = DeviceProfile::daw;
        updateProfileButtons();
        refreshFields();
    };
    addAndMakeVisible(ccProfileButton);
    addAndMakeVisible(dawProfileButton);

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
    }

    updateProfileButtons();
    clearConfiguration();
}

void ControlInspectorComponent::updateProfileButtons()
{
    const auto style = [](juce::TextButton& button, bool isProfileSelected) {
        button.setColour(juce::TextButton::buttonColourId, isProfileSelected ? mixerui::keyDown : mixerui::keyIdle);
        button.setColour(juce::TextButton::textColourOffId, isProfileSelected ? mixerui::onShell : mixerui::ink);
        button.repaint();
    };
    style(ccProfileButton, activeProfile == DeviceProfile::ccUser);
    style(dawProfileButton, activeProfile == DeviceProfile::daw);
}

void ControlInspectorComponent::selectControl(ControlSelection selection)
{
    selected = selection;
    hasSelection = true;
    refreshFields();

    if (!configuration && !loading && onSettingsRequired)
        onSettingsRequired();
}

void ControlInspectorComponent::setConfiguration(const ControlConfiguration& newConfiguration)
{
    configuration = newConfiguration;
    readError.clear();
    loading = false;
    refreshFields();
}

void ControlInspectorComponent::clearConfiguration()
{
    configuration.reset();
    readError.clear();
    loading = false;
    loadProgress = 0.0;
    refreshFields();
}

void ControlInspectorComponent::setLoading(bool shouldBeLoading, double progress)
{
    loading = shouldBeLoading;
    loadProgress = progress;
    if (shouldBeLoading)
        readError.clear();
    refreshFields();
}

void ControlInspectorComponent::setReadError(const juce::String& message)
{
    loading = false;
    loadProgress = 0.0;
    readError = message;
    refreshFields();
}

void ControlInspectorComponent::hideFields()
{
    for (int index = 0; index < 6; ++index)
        setField(index, {}, {}, false);
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

void ControlInspectorComponent::refreshFields()
{
    hideFields();
    selectionLabel.setText(hasSelection ? mixerui::utf8(controlDisplayName(selected)) : "No control selected",
                           juce::dontSendNotification);

    if (loading)
    {
        const auto percent = juce::roundToInt(juce::jlimit(0.0, 1.0, loadProgress) * 100.0);
        statusLabel.setText("Reading the 6,282-byte settings snapshot from the device... "
                                + juce::String(percent) + "%",
                            juce::dontSendNotification);
        return;
    }
    if (!configuration)
    {
        statusLabel.setText(readError.isNotEmpty()
                                ? readError
                                : hasSelection ? "Configuration is not loaded. Select the control again to retry."
                                               : "Select any hardware control to read its configuration.",
                            juce::dontSendNotification);
        return;
    }

    statusLabel.setText(readError.isNotEmpty()
                            ? readError
                            : activeProfile == DeviceProfile::ccUser
                                ? "Stored CC / User profile. The snapshot also keeps a DAW profile."
                                : "Stored DAW profile. Unknown type values stay as raw numbers.",
                        juce::dontSendNotification);
    if (!hasSelection)
        return;

    const auto& profile = configuration->profile(activeProfile);
    const auto typeText = [this](std::uint8_t type) {
        return activeProfile == DeviceProfile::ccUser && type == 0
            ? juce::String("CC") : number(type);
    };

    switch (selected.kind)
    {
        case ControlKind::knob:
        {
            const auto& knob = profile.knobs[static_cast<std::size_t>(selected.index)];
            setField(0, "TYPE", typeText(knob.type));
            setField(1, "SPEED", knob.speed == 2 ? juce::String("Normal") : number(knob.speed));
            setField(2, "CHANNEL", channel(knob.channel));
            setField(3, "CC", ccText(knob.cc));
            setField(4, "MIN", number(knob.minimum));
            setField(5, "MAX", number(knob.maximum));
            break;
        }
        case ControlKind::slider:
        {
            const auto& slider = profile.sliders[static_cast<std::size_t>(selected.index)];
            setField(0, "TYPE", typeText(slider.type));
            setField(1, "CHANNEL", channel(slider.channel));
            setField(2, "CC", ccText(slider.cc));
            setField(3, "MIN", number(slider.minimum));
            setField(4, "MAX", number(slider.maximum));
            break;
        }
        case ControlKind::trackButton:
        case ControlKind::globalButton:
        {
            const auto buttonIndex = selected.kind == ControlKind::trackButton ? selected.index : selected.index + 32;
            const auto& button = profile.buttons[static_cast<std::size_t>(buttonIndex)];
            setField(0, "MODE", activeProfile == DeviceProfile::ccUser && button.mode == 2
                                    ? juce::String("CC Push") : number(button.mode));
            setField(1, "CHANNEL", channel(button.channel));
            setField(2, "CC", ccText(button.cc));
            setField(3, "VALUE 1", number(button.value1));
            setField(4, "VALUE 2", number(button.value2));
            setField(5, "LED", number(button.led));
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
    selectionLabel.setBounds(area.getX(), area.getY() + 16, area.getWidth() - 280, 30);
    statusLabel.setBounds(area.getX(), area.getY() + 48, area.getWidth() - 280, 18);

    dawProfileButton.setBounds(area.getRight() - 96, area.getY() + 8, 96, 34);
    ccProfileButton.setBounds(area.getRight() - 214, area.getY() + 8, 110, 34);

    auto fields = area.removeFromBottom(57);
    const auto width = fields.getWidth() / 6;
    for (int index = 0; index < 6; ++index)
    {
        auto field = fields.withLeft(fields.getX() + index * width).withWidth(width).reduced(6, 0);
        fieldNames[static_cast<std::size_t>(index)].setBounds(field.removeFromTop(17));
        fieldValues[static_cast<std::size_t>(index)].setBounds(field);
    }
}
