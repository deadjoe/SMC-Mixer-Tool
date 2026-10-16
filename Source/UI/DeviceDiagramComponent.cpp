#include "DeviceDiagramComponent.h"

#include "MixerLookAndFeel.h"

void ReadOnlySlider::mouseDown(const juce::MouseEvent&)
{
    if (onInspect)
        onInspect();
}

DeviceDiagramComponent::DeviceDiagramComponent()
{
    for (int index = 0; index < 8; ++index)
    {
        auto& knob = knobs[static_cast<std::size_t>(index)];
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange(0.0, 1.0);
        knob.setValue(0.62, juce::dontSendNotification);
        knob.setPopupDisplayEnabled(false, false, nullptr);
        styleSlider(knob, true);
        knob.setTitle("Knob " + juce::String(index + 1));
        knob.setTooltip("Knob " + juce::String(index + 1));
        knob.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        knob.onInspect = [this, index] { selectControl({ ControlKind::knob, index }); };
        addAndMakeVisible(knob);

        auto& fader = faders[static_cast<std::size_t>(index)];
        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange(0.0, 1.0);
        fader.setValue(0.72, juce::dontSendNotification);
        fader.setPopupDisplayEnabled(false, false, nullptr);
        styleSlider(fader, false);
        fader.setTitle("Fader " + juce::String(index + 1));
        fader.setTooltip("Fader " + juce::String(index + 1));
        fader.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        fader.onInspect = [this, index] { selectControl({ ControlKind::slider, index }); };
        addAndMakeVisible(fader);
    }

    for (int index = 0; index < 32; ++index)
    {
        const auto track = index / 4 + 1;
        const auto slot = index % 4;
        auto button = std::make_unique<juce::TextButton>(trackButtonShortName(slot));
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(false);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->setColour(juce::TextButton::buttonColourId, mixerui::trackButton);
        button->setColour(juce::TextButton::buttonOnColourId, mixerui::trackButton);
        button->setColour(juce::TextButton::textColourOffId, mixerui::onShell);
        button->setColour(juce::TextButton::textColourOnId, mixerui::onShell);
        button->setTitle("Track " + juce::String(track) + " " + mixerui::utf8(trackButtonFullName(slot)));
        button->setTooltip("Track " + juce::String(track) + "  " + mixerui::utf8(trackButtonFullName(slot)));
        button->onClick = [this, index] { selectControl({ ControlKind::trackButton, index }); };
        addAndMakeVisible(*button);
        trackButtons[static_cast<std::size_t>(index)] = std::move(button);
    }

    for (int index = 0; index < 11; ++index)
    {
        auto button = std::make_unique<juce::TextButton>(globalButtonShortName(index));
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(false);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->setColour(juce::TextButton::buttonColourId, mixerui::globalButton);
        button->setColour(juce::TextButton::buttonOnColourId, mixerui::globalButton);
        button->setColour(juce::TextButton::textColourOffId, mixerui::onShell);
        button->setColour(juce::TextButton::textColourOnId, mixerui::onShell);
        button->setTitle(mixerui::utf8(globalButtonFullName(index)));
        button->setTooltip(mixerui::utf8(globalButtonFullName(index)));
        button->onClick = [this, index] { selectControl({ ControlKind::globalButton, index }); };
        addAndMakeVisible(*button);
        globalButtons[static_cast<std::size_t>(index)] = std::move(button);
    }
}

void DeviceDiagramComponent::styleSlider(ReadOnlySlider& slider, bool rotary) const
{
    slider.setOpaque(false);
    slider.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    if (rotary)
    {
        slider.setColour(juce::Slider::rotarySliderFillColourId, mixerui::knob);
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, mixerui::knobOutline);
        slider.setColour(juce::Slider::thumbColourId, mixerui::knobOutline);
        return;
    }

    slider.setColour(juce::Slider::thumbColourId, mixerui::fader);
    slider.setColour(juce::Slider::trackColourId, mixerui::slot);
    slider.setColour(juce::Slider::rotarySliderFillColourId, mixerui::fader);
}

void DeviceDiagramComponent::setDirtyControls(const std::vector<ControlSelection>& dirty)
{
    dirtyKnobs.fill(false);
    dirtyFaders.fill(false);
    dirtyTrackButtons.fill(false);
    dirtyGlobalButtons.fill(false);
    for (const auto& control : dirty)
    {
        if (control.index < 0)
            continue;
        switch (control.kind)
        {
            case ControlKind::knob:
                if (control.index < static_cast<int>(dirtyKnobs.size()))
                    dirtyKnobs[static_cast<std::size_t>(control.index)] = true;
                break;
            case ControlKind::slider:
                if (control.index < static_cast<int>(dirtyFaders.size()))
                    dirtyFaders[static_cast<std::size_t>(control.index)] = true;
                break;
            case ControlKind::trackButton:
                if (control.index < static_cast<int>(dirtyTrackButtons.size()))
                    dirtyTrackButtons[static_cast<std::size_t>(control.index)] = true;
                break;
            case ControlKind::globalButton:
                if (control.index < static_cast<int>(dirtyGlobalButtons.size()))
                    dirtyGlobalButtons[static_cast<std::size_t>(control.index)] = true;
                break;
        }
    }
    repaint();
}

void DeviceDiagramComponent::selectControl(ControlSelection selection)
{
    selected = selection;
    hasSelection = true;
    repaint();
    if (onControlSelected)
        onControlSelected(selection);
}

juce::Rectangle<int> DeviceDiagramComponent::selectionBounds() const
{
    if (!hasSelection)
        return {};

    switch (selected.kind)
    {
        case ControlKind::knob:
            return knobs[static_cast<std::size_t>(selected.index)].getBounds();
        case ControlKind::slider:
            return faders[static_cast<std::size_t>(selected.index)].getBounds();
        case ControlKind::trackButton:
            return trackButtons[static_cast<std::size_t>(selected.index)]->getBounds();
        case ControlKind::globalButton:
            return globalButtons[static_cast<std::size_t>(selected.index)]->getBounds();
    }
    return {};
}

void DeviceDiagramComponent::paint(juce::Graphics& graphics)
{
    mixerui::paintPlate(graphics, getLocalBounds());

    graphics.setColour(mixerui::ink);
    graphics.setFont(MixerLookAndFeel::font(16.0f, true));
    graphics.drawText("Channels", 20, 12, 140, 22, juce::Justification::centredLeft, false);
    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(11.0f));
    graphics.drawText("Click a control. A dot marks an unsaved edit", 20, 34, 360, 16,
                      juce::Justification::centredLeft, false);

    const char* keyNames[] { "Knob", "Fader", "Track", "Global" };
    const juce::Colour keyColours[] { mixerui::knob, mixerui::fader, mixerui::trackButton, mixerui::globalButton };
    auto key = getLocalBounds().reduced(18, 14).removeFromTop(28).removeFromRight(280);
    for (int index = 3; index >= 0; --index)
    {
        auto item = key.removeFromRight(68);
        auto swatch = item.removeFromLeft(12).withSizeKeepingCentre(12, 12).translated(0, 1);
        graphics.setColour(keyColours[index]);
        graphics.fillRoundedRectangle(swatch.toFloat(), 3.0f);
        graphics.setColour(mixerui::muted);
        graphics.setFont(MixerLookAndFeel::font(11.0f));
        graphics.drawText(keyNames[index], item.withTrimmedLeft(4), juce::Justification::centredLeft, false);
    }

    graphics.setColour(mixerui::well);
    graphics.fillRoundedRectangle(deckBounds.toFloat(), 12.0f);

    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(11.0f, true));
    for (int index = 0; index < 8; ++index)
    {
        const auto frame = channelFrames[static_cast<std::size_t>(index)];
        graphics.drawText(juce::String(index + 1), frame.withHeight(18), juce::Justification::centred, false);
    }

    graphics.setColour(mixerui::muted);
    graphics.setFont(MixerLookAndFeel::font(11.0f, true));
    graphics.drawText("GLOBAL", globalRowBounds.withHeight(16), juce::Justification::centredLeft, false);

    if (hasSelection)
    {
        const auto bounds = selectionBounds().toFloat().expanded(4.0f);
        graphics.setColour(mixerui::ink.withAlpha(0.85f));
        graphics.drawRoundedRectangle(bounds, 8.0f, 2.0f);
    }
}

void DeviceDiagramComponent::paintOverChildren(juce::Graphics& graphics)
{
    const auto drawDot = [&graphics](juce::Rectangle<int> bounds) {
        const auto x = static_cast<float>(bounds.getRight() - 10);
        const auto y = static_cast<float>(bounds.getY() - 1);
        graphics.setColour(mixerui::plate);
        graphics.fillEllipse(x - 1.5f, y - 1.5f, 11.0f, 11.0f);
        graphics.setColour(mixerui::ink);
        graphics.fillEllipse(x, y, 8.0f, 8.0f);
    };

    for (int index = 0; index < 8; ++index)
    {
        if (dirtyKnobs[static_cast<std::size_t>(index)])
            drawDot(knobs[static_cast<std::size_t>(index)].getBounds());
        if (dirtyFaders[static_cast<std::size_t>(index)])
            drawDot(faders[static_cast<std::size_t>(index)].getBounds());
    }
    for (int index = 0; index < 32; ++index)
        if (dirtyTrackButtons[static_cast<std::size_t>(index)])
            drawDot(trackButtons[static_cast<std::size_t>(index)]->getBounds());
    for (int index = 0; index < 11; ++index)
        if (dirtyGlobalButtons[static_cast<std::size_t>(index)])
            drawDot(globalButtons[static_cast<std::size_t>(index)]->getBounds());
}

void DeviceDiagramComponent::resized()
{
    auto area = getLocalBounds().reduced(14, 12);
    area.removeFromTop(44);

    globalRowBounds = area.removeFromBottom(62);
    area.removeFromBottom(12);
    deckBounds = area;

    const auto gap = 8;
    const auto channelWidth = (deckBounds.getWidth() - gap * 7) / 8;
    for (int index = 0; index < 8; ++index)
    {
        const auto width = index == 7
            ? deckBounds.getRight() - (deckBounds.getX() + index * (channelWidth + gap))
            : channelWidth;
        juce::Rectangle<int> frame { deckBounds.getX() + index * (channelWidth + gap),
                                     deckBounds.getY() + 6,
                                     width,
                                     deckBounds.getHeight() - 12 };
        channelFrames[static_cast<std::size_t>(index)] = frame;

        auto content = frame;
        content.removeFromTop(18);
        const auto knobSide = juce::jlimit(48, 74, content.getWidth() - 8);
        auto knobArea = content.removeFromTop(knobSide + 4);
        knobs[static_cast<std::size_t>(index)].setBounds(knobArea.withSizeKeepingCentre(knobSide, knobSide));

        auto column = content.reduced(2, 4);
        auto buttons = column.removeFromRight(juce::jmin(46, juce::jmax(28, column.getWidth() * 2 / 5)));
        column.removeFromRight(6);
        faders[static_cast<std::size_t>(index)].setBounds(column);

        const auto buttonGap = 4;
        const auto buttonHeight = juce::jmax(22, (buttons.getHeight() - buttonGap * 3) / 4);
        for (int button = 0; button < 4; ++button)
        {
            trackButtons[static_cast<std::size_t>(index * 4 + button)]->setBounds(
                buttons.getX(),
                buttons.getY() + button * (buttonHeight + buttonGap),
                buttons.getWidth(),
                buttonHeight);
        }
    }

    auto globalButtonsArea = globalRowBounds.withTrimmedTop(18);
    const auto globalGap = 6;
    const auto globalWidth = (globalButtonsArea.getWidth() - globalGap * 10) / 11;
    const auto globalHeight = juce::jmin(40, globalButtonsArea.getHeight());
    for (int index = 0; index < 11; ++index)
    {
        const auto width = index == 10
            ? globalButtonsArea.getRight() - (globalButtonsArea.getX() + index * (globalWidth + globalGap))
            : globalWidth;
        globalButtons[static_cast<std::size_t>(index)]->setBounds(
            globalButtonsArea.getX() + index * (globalWidth + globalGap),
            globalButtonsArea.getY(),
            width,
            globalHeight);
    }
}
