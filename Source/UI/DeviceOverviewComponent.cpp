#include "DeviceOverviewComponent.h"

namespace
{
constexpr int profileGroup = 1;
constexpr int cardPadding = 16;
constexpr int toolbarHeight = 32;

juce::String withShortcut(const char* text, const char* shortcut)
{
    return juce::String(text) + " (" + juce::String::fromUTF8(shortcut) + ")";
}
}

DeviceOverviewComponent::DeviceOverviewComponent()
{
    addAndMakeVisible(panel);
    addAndMakeVisible(details);

    for (auto* button : { &ccUserButton, &dawButton })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(profileGroup);
        button->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    ccUserButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    dawButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    ccUserButton.setTooltip(withShortcut("Show the stored CC / User profile, which you can edit", "\xe2\x8c\x98" "1"));
    dawButton.setTooltip(withShortcut("Show the stored DAW profile, read-only", "\xe2\x8c\x98" "2"));
    ccUserButton.onClick = [this] {
        if (ccUserButton.getToggleState() && onProfileChosen)
            onProfileChosen(DeviceProfile::ccUser);
    };
    dawButton.onClick = [this] {
        if (dawButton.getToggleState() && onProfileChosen)
            onProfileChosen(DeviceProfile::daw);
    };

    profileHint.setText("Stored profile on screen. The mixer's live mode does not change.", juce::dontSendNotification);
    profileHint.setFont(mixerui::font(11.5f));
    profileHint.setColour(juce::Label::textColourId, mixerui::muted);
    profileHint.setMinimumHorizontalScale(1.0f);
    profileHint.setTooltip("Hold Shift and press Left or Right on the mixer to switch its live mode.");
    addAndMakeVisible(profileHint);

    exportButton.setTooltip(withShortcut("Save the profile on screen as a JSON file", "\xe2\x8c\x98" "E"));
    copyButton.setTooltip(withShortcut("Copy the profile on screen to the clipboard as JSON", "\xe2\x87\xa7\xe2\x8c\x98" "C"));
    undoButton.setTooltip(withShortcut("Undo", "\xe2\x8c\x98Z"));
    redoButton.setTooltip(withShortcut("Redo", "\xe2\x87\xa7\xe2\x8c\x98Z"));
    revertAllButton.setTooltip("Discard every unsaved edit and show the settings as read.");
    write.setTooltip(withShortcut("Write the changed records, read them back, and save them on the mixer", "\xe2\x8c\x98S"));
    MixerLookAndFeel::setPrimary(write, true);

    exportButton.onClick = [this] { if (onExport) onExport(); };
    copyButton.onClick = [this] { if (onCopy) onCopy(); };
    undoButton.onClick = [this] { if (onUndo) onUndo(); };
    redoButton.onClick = [this] { if (onRedo) onRedo(); };
    revertAllButton.onClick = [this] { if (onRevertAll) onRevertAll(); };
    write.onClick = [this] { if (onWrite) onWrite(); };
    for (juce::Button* button : { static_cast<juce::Button*>(&exportButton), static_cast<juce::Button*>(&copyButton),
                                  static_cast<juce::Button*>(&undoButton), static_cast<juce::Button*>(&redoButton),
                                  static_cast<juce::Button*>(&revertAllButton), static_cast<juce::Button*>(&write) })
    {
        button->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    setProfileKind(DeviceProfile::ccUser);
    setActions({});
}

void DeviceOverviewComponent::setActions(const Actions& actions)
{
    write.setButtonText(actions.changedRecords == 0 ? juce::String("Write")
                        : actions.changedRecords == 1 ? juce::String("Write 1 change")
                                                      : "Write " + juce::String(actions.changedRecords) + " changes");
    write.setEnabled(actions.canWrite);
    MixerLookAndFeel::setPrimary(write, actions.canWrite);
    revertAllButton.setEnabled(actions.canRevertAll);
    undoButton.setEnabled(actions.canUndo);
    redoButton.setEnabled(actions.canRedo);
    exportButton.setEnabled(actions.canExport);
    copyButton.setEnabled(actions.canExport);
    ccUserButton.setEnabled(actions.canSwitchProfile);
    dawButton.setEnabled(actions.canSwitchProfile);
}

void DeviceOverviewComponent::setProfileKind(DeviceProfile profile)
{
    ccUserButton.setToggleState(profile == DeviceProfile::ccUser, juce::dontSendNotification);
    dawButton.setToggleState(profile == DeviceProfile::daw, juce::dontSendNotification);
}

void DeviceOverviewComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintCard(graphics, card.toFloat());
    if (separatorX > 0)
    {
        graphics.setColour(mixerui::hairline);
        graphics.fillRect(separatorX, card.getY() + cardPadding + 6, 1, toolbarHeight - 12);
    }
}

void DeviceOverviewComponent::resized()
{
    auto area = getLocalBounds().reduced(16, 14);
    details.setBounds(area.removeFromBottom(ControlInspectorComponent::preferredHeight));
    area.removeFromBottom(12);
    card = area;

    auto inner = card.reduced(cardPadding);
    auto toolbar = inner.removeFromTop(toolbarHeight);
    inner.removeFromTop(14);
    panel.setBounds(inner);

    ccUserButton.setBounds(toolbar.removeFromLeft(98));
    dawButton.setBounds(toolbar.removeFromLeft(68));
    toolbar.removeFromLeft(14);

    write.setBounds(toolbar.removeFromRight(136));
    toolbar.removeFromRight(8);
    revertAllButton.setBounds(toolbar.removeFromRight(92));
    toolbar.removeFromRight(8);
    redoButton.setBounds(toolbar.removeFromRight(32));
    toolbar.removeFromRight(6);
    undoButton.setBounds(toolbar.removeFromRight(32));
    toolbar.removeFromRight(12);
    separatorX = toolbar.getRight();
    toolbar.removeFromRight(13);
    copyButton.setBounds(toolbar.removeFromRight(92));
    toolbar.removeFromRight(8);
    exportButton.setBounds(toolbar.removeFromRight(82));
    toolbar.removeFromRight(12);

    const auto hintWidth = juce::GlyphArrangement::getStringWidthInt(profileHint.getFont(), profileHint.getText()) + 8;
    profileHint.setVisible(toolbar.getWidth() >= hintWidth);
    profileHint.setBounds(toolbar);
}
