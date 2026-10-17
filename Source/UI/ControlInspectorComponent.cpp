#include "ControlInspectorComponent.h"

namespace
{
juce::Colour familyColour(std::optional<ControlSelection> selection)
{
    if (!selection)
        return mixerui::hairlineStrong;
    switch (selection->kind)
    {
        case ControlKind::knob: return mixerui::knob;
        case ControlKind::slider: return mixerui::fader;
        case ControlKind::trackButton: return mixerui::trackButton;
        case ControlKind::globalButton: return mixerui::globalButton;
    }
    return mixerui::hairlineStrong;
}

juce::String kindCaption(ControlSelection selection)
{
    switch (selection.kind)
    {
        case ControlKind::knob: return "Knob";
        case ControlKind::slider: return "Fader";
        case ControlKind::trackButton: return "Track key";
        case ControlKind::globalButton: return "Transport key";
    }
    return {};
}

void styleCaption(juce::Label& label)
{
    label.setFont(mixerui::captionFont(9.5f));
    label.setColour(juce::Label::textColourId, mixerui::muted);
    label.setBorderSize({ 0, 2, 0, 2 });
    label.setMinimumHorizontalScale(0.8f);
}

void styleHelper(juce::Label& label)
{
    label.setFont(mixerui::font(10.5f));
    label.setColour(juce::Label::textColourId, mixerui::muted);
    label.setBorderSize({ 0, 2, 0, 2 });
    label.setMinimumHorizontalScale(0.8f);
}
}

ControlInspectorComponent::ControlInspectorComponent()
{
    using namespace mixerui;
    kindLabel.setFont(captionFont(9.5f));
    kindLabel.setColour(juce::Label::textColourId, muted);
    titleLabel.setFont(font(19.0f, Weight::semibold));
    titleLabel.setColour(juce::Label::textColourId, ink);
    note.setFont(font(12.0f));
    note.setColour(juce::Label::textColourId, inkSoft);
    note.setJustificationType(juce::Justification::topLeft);
    note.setMinimumHorizontalScale(1.0f);
    readOnlyBadge.setText("READ ONLY", juce::dontSendNotification);
    readOnlyBadge.setFont(captionFont(9.0f));
    readOnlyBadge.setColour(juce::Label::textColourId, inkSoft);
    readOnlyBadge.setJustificationType(juce::Justification::centred);
    for (auto* label : { &kindLabel, &titleLabel, &note, &readOnlyBadge })
    {
        label->setBorderSize({});
        addAndMakeVisible(*label);
    }
    readOnlyBadge.setTooltip("The DAW profile is shown as stored. Switch to CC / User to edit.");

    revert.setTooltip(juce::String::fromUTF8("Restore this control as it was read from the mixer (\xe2\x8c\x98\xe2\x8c\xab)."));
    revert.onClick = [this] {
        if (onRevertRequested && current.selection)
            onRevertRequested(*current.selection, current.profileKind);
    };
    addAndMakeVisible(revert);
    readButton.onClick = [this] { if (onReadRequested) onReadRequested(); };
    addChildComponent(readButton);

    styleCaption(choiceCaption);
    styleHelper(choiceHelper);
    addAndMakeVisible(choiceCaption);
    addAndMakeVisible(choiceHelper);
    choice.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    choice.onChange = [this] {
        if (updating || !current.selection || choice.getSelectedId() <= 0)
            return;
        edit(isButton(current.selection->kind) ? EditableField::mode : EditableField::type, choice.getSelectedId() - 1, false);
    };
    addAndMakeVisible(choice);

    for (auto& entry : slots)
    {
        styleCaption(entry.caption);
        styleHelper(entry.helper);
        addAndMakeVisible(entry.caption);
        addAndMakeVisible(entry.helper);
        addAndMakeVisible(entry.number);
        entry.number.onValueChange = [this, &entry](int value, bool continuing) {
            if (!updating && entry.field)
                edit(*entry.field, value, continuing);
        };
        entry.number.onInvalidInput = [this](const juce::String& message) {
            if (onNotice)
                onNotice(message, mixerui::Tone::warning);
        };
    }

    show({});
}

void ControlInspectorComponent::edit(EditableField field, int value, bool continuing)
{
    if (onControlEdited && current.selection)
        onControlEdited(*current.selection, current.profileKind, field, static_cast<std::uint8_t>(value), continuing);
}

NumberField* ControlInspectorComponent::numberFieldFor(EditableField field)
{
    for (auto& slot : slots)
        if (slot.visible && slot.field == field)
            return &slot.number;
    return nullptr;
}

void ControlInspectorComponent::configureChoice(ControlKind kind)
{
    // Track and global keys share their modes, so their list only changes with the family.
    const auto family = isButton(kind) ? ControlKind::trackButton : kind;
    if (choiceKind == family)
        return;
    choiceKind = family;
    choice.clear(juce::dontSendNotification);
    if (family == ControlKind::knob)
        for (std::uint8_t type = 0; type < knobTypeCount; ++type)
            choice.addItem(knobTypeName(type), type + 1);
    else if (family == ControlKind::slider)
        for (std::uint8_t type = 0; type < sliderTypeCount; ++type)
            choice.addItem(sliderTypeName(type), type + 1);
    else
        for (std::uint8_t mode = 0; mode < buttonModeCount; ++mode)
            choice.addItem(buttonModeName(mode), mode + 1);
}

void ControlInspectorComponent::hideSlot(int index)
{
    auto& slot = slots[static_cast<std::size_t>(index)];
    slot.visible = false;
    slot.field.reset();
    slot.caption.setVisible(false);
    slot.helper.setVisible(false);
    slot.number.setVisible(false);
}

void ControlInspectorComponent::showNumber(int index, EditableField field, const juce::String& caption,
                                           const juce::String& helper, bool used, int displayOffset, bool listed)
{
    auto& slot = slots[static_cast<std::size_t>(index)];
    const auto& profile = *current.profile;
    const auto selection = *current.selection;
    const auto value = static_cast<int>(fieldValue(profile, selection, field));
    auto range = allowedRange(profile, selection, field);
    // A stored value outside the limits is shown as it is, with a warning outline.
    range.minimum = juce::jmin(range.minimum, value);
    range.maximum = juce::jmax(range.maximum, value);

    slot.visible = true;
    slot.field = field;
    slot.caption.setText(caption.toUpperCase(), juce::dontSendNotification);
    slot.caption.setColour(juce::Label::textColourId, used ? mixerui::muted : mixerui::faint);
    slot.helper.setText(helper, juce::dontSendNotification);
    slot.number.setRange(range.minimum, range.maximum);
    slot.number.setDisplayOffset(displayOffset);
    slot.number.setValue(value);
    slot.number.setEditable(current.editable);
    slot.number.setEnabled(used || !current.editable);
    slot.number.setShowsList(listed);
    slot.number.setAccent(familyColour(current.selection).darker(0.2f));
    slot.number.setTitle(caption);
    slot.number.setTooltip({});
    if (field == EditableField::cc && listed)
        slot.number.itemName = [](int cc) { return juce::String(controlChangeName(static_cast<std::uint8_t>(cc))); };
    else
        slot.number.itemName = nullptr;
    slot.caption.setVisible(true);
    slot.helper.setVisible(true);
    slot.number.setVisible(true);
}

void ControlInspectorComponent::showReadOnly(int index, const juce::String& caption, int value, const juce::String& helper)
{
    auto& slot = slots[static_cast<std::size_t>(index)];
    slot.visible = true;
    slot.field.reset();
    slot.caption.setText(caption.toUpperCase(), juce::dontSendNotification);
    slot.caption.setColour(juce::Label::textColourId, mixerui::muted);
    slot.helper.setText(helper, juce::dontSendNotification);
    slot.number.itemName = nullptr;
    slot.number.setShowsList(false);
    slot.number.setDisplayOffset(0);
    slot.number.setRange(value, value);
    slot.number.setValue(value);
    slot.number.setEditable(false);
    slot.number.setEnabled(true);
    slot.number.setTitle(caption);
    slot.caption.setVisible(true);
    slot.helper.setVisible(true);
    slot.number.setVisible(true);
}

void ControlInspectorComponent::show(const View& view)
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    current = view;

    const auto hasSelection = view.selection.has_value() && isValidControl(*view.selection);
    const auto ready = hasSelection && view.profile != nullptr;
    kindLabel.setText(hasSelection ? kindCaption(*view.selection).toUpperCase() : juce::String("SELECTED CONTROL"),
                      juce::dontSendNotification);
    titleLabel.setText(hasSelection ? mixerui::utf8(controlDisplayName(*view.selection)) : juce::String("Select a control"),
                       juce::dontSendNotification);
    readOnlyBadge.setVisible(ready && view.profileKind == DeviceProfile::daw);
    revert.setVisible(ready && view.profileKind == DeviceProfile::ccUser);
    revert.setEnabled(view.selectionDirty && view.editable);
    readButton.setVisible(view.profile == nullptr && view.offerRead);

    if (view.profile == nullptr)
        note.setText(view.emptyMessage, juce::dontSendNotification);
    else if (!hasSelection)
        note.setText("Click a knob, fader, or key on the panel, or move between them with the arrow keys.",
                     juce::dontSendNotification);
    else
        note.setText(mixerui::utf8(controlNote(*view.profile, *view.selection)), juce::dontSendNotification);
    note.setTooltip(note.getText());

    choice.setVisible(ready);
    choiceCaption.setVisible(ready);
    choiceHelper.setVisible(ready);
    if (!ready)
    {
        for (int index = 0; index < slotCount - 1; ++index)
            hideSlot(index);
        resized();
        repaint();
        return;
    }

    const auto& profile = *view.profile;
    const auto selection = *view.selection;
    const auto meaning = meaningFor(profile, selection);
    const auto button = isButton(selection.kind);
    const auto code = fieldValue(profile, selection, button ? EditableField::mode : EditableField::type);

    configureChoice(selection.kind);
    choiceCaption.setText(button ? "MODE" : "TYPE", juce::dontSendNotification);
    if (choice.indexOfItemId(code + 1) < 0)
        choice.addItem("Unknown (" + juce::String(code) + ")", code + 1);
    choice.setSelectedId(code + 1, juce::dontSendNotification);
    choice.setEnabled(view.editable);
    choice.setTooltip(button ? "What the key sends. Value 1 and Value 2 stay on the key when the mode changes."
                             : "What the control sends.");
    choiceHelper.setText(selection.kind == ControlKind::knob && code == knobTypeRelative ? juce::String("Midi Suite calls it CW")
                         : selection.kind == ControlKind::knob && code == 0           ? juce::String("Midi Suite calls it CC")
                                                                                       : juce::String(),
                         juce::dontSendNotification);

    const auto cc = fieldValue(profile, selection, EditableField::cc);
    const auto ccHelper = meaning.ccIsController ? juce::String(controlChangeName(cc))
                        : meaning.ccUsed         ? juce::String()
                                                 : juce::String("Not sent");
    showNumber(0, EditableField::channel, "Channel", {}, true, 1, true);
    showNumber(1, EditableField::cc, meaning.ccUsed ? mixerui::utf8(meaning.ccName) : juce::String("CC"), ccHelper,
               meaning.ccUsed, 0, meaning.ccIsController);

    if (button)
    {
        showNumber(2, EditableField::value1, mixerui::utf8(meaning.value1Name),
                   meaning.value1Used ? juce::String() : juce::String("Not sent"), meaning.value1Used, 0, false);
        showNumber(3, EditableField::value2, mixerui::utf8(meaning.value2Name),
                   meaning.value2Used ? juce::String() : juce::String("Not sent"), meaning.value2Used, 0, false);
        showNumber(4, EditableField::led, "LED", "Raw byte, not measured", true, 0, false);
        slots[4].number.setTooltip("The stored LED byte, 0 to 255. What each value does has not been measured yet.");
    }
    else
    {
        const auto relative = isRelativeKnob(profile, selection);
        const auto rangeHelper = !meaning.rangeUsed ? juce::String("Not sent")
                               : relative           ? juce::String("Sent on each step")
                                                    : juce::String();
        showNumber(2, EditableField::minimum, mixerui::utf8(meaning.minimumName), rangeHelper, meaning.rangeUsed, 0, false);
        showNumber(3, EditableField::maximum, mixerui::utf8(meaning.maximumName), rangeHelper, meaning.rangeUsed, 0, false);
        if (selection.kind == ControlKind::knob)
        {
            showReadOnly(4, "Speed", profile.knobs[static_cast<std::size_t>(selection.index)].speed, "Not measured, read-only");
            slots[4].number.setTooltip("The stored speed byte. What it changes has not been measured, so it is shown read-only.");
        }
        else
        {
            hideSlot(4);
        }
    }

    resized();
    repaint();
}

void ControlInspectorComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintCard(graphics, getLocalBounds().toFloat().withTrimmedBottom(6.0f));
    graphics.setColour(familyColour(current.selection));
    graphics.fillRoundedRectangle(juce::Rectangle<float>(20.0f, 20.0f, 4.0f, 40.0f), 2.0f);
    if (readOnlyBadge.isVisible())
    {
        graphics.setColour(mixerui::well);
        graphics.fillRoundedRectangle(readOnlyBadge.getBounds().toFloat(), 4.0f);
    }
}

void ControlInspectorComponent::resized()
{
    auto area = getLocalBounds().withTrimmedBottom(6).reduced(36, 18);
    auto top = area.removeFromTop(78);

    auto actions = top.removeFromRight(240);
    if (revert.isVisible())
        revert.setBounds(actions.removeFromRight(96).withHeight(32));
    if (readButton.isVisible())
        readButton.setBounds(actions.removeFromRight(130).withHeight(32));
    top.removeFromRight(16);

    kindLabel.setBounds(top.removeFromTop(14));
    auto titleRow = top.removeFromTop(26);
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt(titleLabel.getFont(), titleLabel.getText()) + 4;
    titleLabel.setBounds(titleRow.removeFromLeft(juce::jmin(titleWidth, titleRow.getWidth())));
    if (readOnlyBadge.isVisible())
    {
        titleRow.removeFromLeft(10);
        readOnlyBadge.setBounds(titleRow.removeFromLeft(74).withSizeKeepingCentre(74, 18));
    }
    // Two lines, so the longest note, a Custom key's 64-byte message, still fits; the
    // tooltip holds the whole sentence if a narrow window cuts it short.
    note.setBounds(top.withTrimmedTop(4));

    area.removeFromTop(12);
    const auto fields = area.withHeight(16 + 6 + MixerLookAndFeel::fieldHeight + 4 + 14);
    constexpr int gap = 14;
    const auto width = (fields.getWidth() - gap * (slotCount - 1)) / slotCount;
    const auto place = [&](int column, juce::Label& caption, juce::Component& control, juce::Label& helper) {
        auto cell = fields.withX(fields.getX() + column * (width + gap)).withWidth(width);
        caption.setBounds(cell.removeFromTop(16));
        cell.removeFromTop(6);
        control.setBounds(cell.removeFromTop(MixerLookAndFeel::fieldHeight));
        cell.removeFromTop(4);
        helper.setBounds(cell.removeFromTop(14));
    };
    place(0, choiceCaption, choice, choiceHelper);
    for (int index = 0; index < slotCount - 1; ++index)
    {
        auto& slot = slots[static_cast<std::size_t>(index)];
        place(index + 1, slot.caption, slot.number, slot.helper);
    }
}
