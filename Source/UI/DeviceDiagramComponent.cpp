#include "DeviceDiagramComponent.h"

#include "MixerLookAndFeel.h"
#include "Widgets.h"

#include <algorithm>

namespace
{
constexpr float arcStart = -juce::MathConstants<float>::pi * 0.75f;
constexpr float arcEnd = juce::MathConstants<float>::pi * 0.75f;

juce::Colour familyColour(ControlKind kind)
{
    switch (kind)
    {
        case ControlKind::knob: return mixerui::knob;
        case ControlKind::slider: return mixerui::fader;
        case ControlKind::trackButton: return mixerui::trackButton;
        case ControlKind::globalButton: return mixerui::globalButton;
    }
    return mixerui::ink;
}

float normalised(int value)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
}

juce::String accessibleName(ControlSelection control)
{
    return mixerui::utf8(controlDisplayName(control));
}
}

ControlView::ControlView(ControlSelection controlToShow)
    : juce::Button(accessibleName(controlToShow)), control(controlToShow)
{
    setTitle(accessibleName(controlToShow));
    setTooltip(accessibleName(controlToShow));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(false);
    setTriggeredOnMouseDown(true);
}

void ControlView::setSummary(const std::optional<ControlSummary>& newSummary, const juce::String& description)
{
    summary = newSummary;
    setTooltip(description.isNotEmpty() ? description : accessibleName(control));
    setDescription(description);
    repaint();
}

juce::Rectangle<float> ControlView::knobCircle() const
{
    const auto side = static_cast<float>(juce::jmin(getWidth(), getHeight() - knobCaptionHeight));
    return juce::Rectangle<float>(side, side).withCentre({ static_cast<float>(getWidth()) * 0.5f, side * 0.5f });
}

juce::Rectangle<float> ControlView::faderSlot() const
{
    const auto top = 10.0f;
    const auto bottom = static_cast<float>(getHeight() - faderCaptionHeight) - 10.0f;
    return { static_cast<float>(getWidth()) * 0.5f - 3.0f, top, 6.0f, juce::jmax(10.0f, bottom - top) };
}

juce::Rectangle<int> ControlView::markArea() const
{
    switch (control.kind)
    {
        case ControlKind::knob: return knobCircle().getSmallestIntegerContainer().translated(getX(), getY());
        case ControlKind::slider: return getBounds().withTrimmedBottom(faderCaptionHeight);
        case ControlKind::trackButton:
        case ControlKind::globalButton: return getBounds();
    }
    return getBounds();
}

void ControlView::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    switch (control.kind)
    {
        case ControlKind::knob: paintKnob(graphics, highlighted); break;
        case ControlKind::slider: paintFader(graphics, highlighted); break;
        case ControlKind::trackButton:
        case ControlKind::globalButton: paintKey(graphics, highlighted, down); break;
    }
}

void ControlView::paintKnob(juce::Graphics& graphics, bool highlighted)
{
    using namespace mixerui;
    const auto circle = knobCircle().reduced(2.0f);
    const auto centre = circle.getCentre();
    const auto radius = circle.getWidth() * 0.5f;
    const auto arcRadius = radius - 3.0f;
    const auto stroke = juce::PathStrokeType(juce::jmax(3.0f, radius * 0.1f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);

    // The track, then the stored range, or the whole arc for a relative knob.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, arcStart, arcEnd, true);
    graphics.setColour(slot);
    graphics.strokePath(track, stroke);
    if (summary && (summary->hasRange || summary->relative))
    {
        const auto from = summary->relative ? 0.0f : normalised(juce::jmin(summary->rangeMinimum, summary->rangeMaximum));
        const auto to = summary->relative ? 1.0f : normalised(juce::jmax(summary->rangeMinimum, summary->rangeMaximum));
        juce::Path range;
        range.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            arcStart + (arcEnd - arcStart) * from, arcStart + (arcEnd - arcStart) * juce::jmax(to, from + 0.01f), true);
        graphics.setColour(summary->relative ? knob.withAlpha(0.5f) : knob);
        graphics.strokePath(range, stroke);
    }

    // The cap: a soft disc with a pointer at twelve o'clock.
    const auto cap = circle.reduced(radius * 0.3f);
    graphics.setColour(juce::Colour(0x183b2a14));
    graphics.fillEllipse(cap.translated(0.0f, 1.5f));
    graphics.setGradientFill(juce::ColourGradient(field, cap.getX(), cap.getY(),
                                                  juce::Colour(0xffeee7dc), cap.getX(), cap.getBottom(), false));
    graphics.fillEllipse(cap);
    graphics.setColour(highlighted ? slot : hairlineStrong);
    graphics.drawEllipse(cap.reduced(0.5f), 1.0f);
    graphics.setColour(knobDeep);
    graphics.fillRoundedRectangle(juce::Rectangle<float>(2.0f, cap.getHeight() * 0.16f)
                                      .withCentre({ centre.x, cap.getY() + cap.getHeight() * 0.13f }), 1.0f);

    if (summary)
    {
        graphics.setColour(ink);
        graphics.setFont(font(juce::jlimit(10.0f, 13.0f, cap.getWidth() * 0.26f), Weight::semibold));
        graphics.drawFittedText(utf8(summary->primary), cap.reduced(3.0f).translated(0.0f, cap.getHeight() * 0.06f).toNearestInt(),
                                juce::Justification::centred, 1, 0.8f);
        graphics.setColour(muted);
        graphics.setFont(font(10.0f, Weight::medium));
        graphics.drawFittedText(utf8(summary->secondary),
                                juce::Rectangle<int>(0, getHeight() - knobCaptionHeight + 2, getWidth(), knobCaptionHeight - 2),
                                juce::Justification::centredTop, 1, 0.8f);
    }
}

void ControlView::paintFader(juce::Graphics& graphics, bool highlighted)
{
    using namespace mixerui;
    const auto slotArea = faderSlot();

    // Scale marks either side of the slot.
    graphics.setColour(faint.withAlpha(0.6f));
    for (int mark = 0; mark <= 8; ++mark)
    {
        const auto y = slotArea.getBottom() - slotArea.getHeight() * static_cast<float>(mark) / 8.0f;
        const auto length = mark % 4 == 0 ? 6.0f : 3.5f;
        graphics.fillRect(slotArea.getX() - 5.0f - length, y - 0.5f, length, 1.0f);
        graphics.fillRect(slotArea.getRight() + 5.0f, y - 0.5f, length, 1.0f);
    }

    graphics.setColour(slot);
    graphics.fillRoundedRectangle(slotArea, 3.0f);
    graphics.setColour(juce::Colour(0x223b2a14));
    graphics.fillRoundedRectangle(slotArea.withHeight(3.0f), 1.5f);

    auto capCentre = slotArea.getCentreY();
    if (summary && summary->hasRange)
    {
        const auto low = normalised(juce::jmin(summary->rangeMinimum, summary->rangeMaximum));
        const auto high = normalised(juce::jmax(summary->rangeMinimum, summary->rangeMaximum));
        const auto top = slotArea.getBottom() - slotArea.getHeight() * high;
        const auto bottom = slotArea.getBottom() - slotArea.getHeight() * low;
        graphics.setColour(fader);
        graphics.fillRoundedRectangle(slotArea.withY(top).withBottom(juce::jmax(bottom, top + 2.0f)), 3.0f);
        capCentre = top;
    }
    else if (summary)
    {
        graphics.setColour(fader.withAlpha(0.45f));
        graphics.fillRoundedRectangle(slotArea, 3.0f);
        capCentre = slotArea.getY();
    }

    const auto capWidth = juce::jmin(static_cast<float>(getWidth()) - 2.0f, 30.0f);
    const auto cap = juce::Rectangle<float>(capWidth, 15.0f).withCentre({ slotArea.getCentreX(), capCentre });
    graphics.setColour(juce::Colour(0x203b2a14));
    graphics.fillRoundedRectangle(cap.translated(0.0f, 1.5f), 4.0f);
    graphics.setGradientFill(juce::ColourGradient(field, cap.getX(), cap.getY(),
                                                  juce::Colour(0xffe9e1d5), cap.getX(), cap.getBottom(), false));
    graphics.fillRoundedRectangle(cap, 4.0f);
    graphics.setColour(highlighted ? slot : hairlineStrong);
    graphics.drawRoundedRectangle(cap.reduced(0.5f), 4.0f, 1.0f);
    graphics.setColour(fader);
    graphics.fillRect(cap.reduced(5.0f, 0.0f).withSizeKeepingCentre(cap.getWidth() - 10.0f, 2.0f));

    if (summary)
    {
        auto caption = getLocalBounds().removeFromBottom(faderCaptionHeight);
        graphics.setColour(ink);
        graphics.setFont(font(10.5f, Weight::semibold));
        graphics.drawFittedText(utf8(summary->primary), caption.removeFromTop(14), juce::Justification::centredBottom, 1, 0.75f);
        graphics.setColour(muted);
        graphics.setFont(font(9.5f, Weight::medium));
        graphics.drawFittedText(utf8(summary->secondary), caption, juce::Justification::centredTop, 1, 0.75f);
    }
}

void ControlView::paintKey(juce::Graphics& graphics, bool highlighted, bool down)
{
    using namespace mixerui;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto corner = juce::jmin(6.0f, bounds.getHeight() * 0.25f);
    auto colour = familyColour(control.kind);
    if (down)
        colour = colour.darker(0.15f);
    else if (highlighted)
        colour = colour.brighter(0.12f);

    graphics.setColour(juce::Colour(0x263b2a14));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 1.5f), corner);
    graphics.setColour(colour);
    graphics.fillRoundedRectangle(bounds, corner);
    graphics.setColour(juce::Colours::white.withAlpha(0.1f));
    graphics.fillRoundedRectangle(bounds.withHeight(juce::jmin(bounds.getHeight() * 0.5f, 10.0f)).reduced(1.0f, 1.0f), corner - 1.0f);

    auto content = bounds.reduced(4.0f, 3.0f);
    const auto showSecondLine = summary && content.getHeight() >= 30.0f && content.getWidth() >= 30.0f;
    auto subline = showSecondLine ? content.removeFromBottom(juce::jmin(13.0f, content.getHeight() * 0.38f)) : juce::Rectangle<float>();

    if (control.kind == ControlKind::globalButton)
    {
        const auto symbol = content.withSizeKeepingCentre(juce::jmin(content.getWidth(), 18.0f), juce::jmin(content.getHeight(), 15.0f));
        drawTransportSymbol(graphics, control.index, symbol, onDark);
    }
    else
    {
        graphics.setColour(onDark);
        graphics.setFont(font(juce::jlimit(10.5f, 12.5f, content.getHeight() * 0.55f), Weight::semibold));
        graphics.drawFittedText(utf8(trackButtonShortName(control.index % 4)), content.toNearestInt(),
                                juce::Justification::centred, 1, 0.8f);
    }

    if (showSecondLine)
    {
        graphics.setColour(onDark.withAlpha(0.82f));
        graphics.setFont(font(9.5f, Weight::medium));
        graphics.drawFittedText(utf8(summary->primary), subline.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
}

DeviceDiagramComponent::DeviceDiagramComponent()
{
    setWantsKeyboardFocus(true);
    setTitle("Mixer panel");

    const auto make = [this](ControlSelection control) {
        auto view = std::make_unique<ControlView>(control);
        view->onClick = [this, control] { select(control); };
        addAndMakeVisible(*view);
        return view;
    };
    for (int index = 0; index < static_cast<int>(layout::channelCount); ++index)
    {
        knobs[static_cast<std::size_t>(index)] = make({ ControlKind::knob, index });
        faders[static_cast<std::size_t>(index)] = make({ ControlKind::slider, index });
    }
    for (int index = 0; index < static_cast<int>(layout::trackButtonCount); ++index)
        trackButtons[static_cast<std::size_t>(index)] = make({ ControlKind::trackButton, index });
    for (int index = 0; index < static_cast<int>(layout::globalButtonCount); ++index)
        globalButtons[static_cast<std::size_t>(index)] = make({ ControlKind::globalButton, index });
}

ControlView& DeviceDiagramComponent::viewFor(ControlSelection control)
{
    const auto index = static_cast<std::size_t>(control.index);
    switch (control.kind)
    {
        case ControlKind::knob: return *knobs[index];
        case ControlKind::slider: return *faders[index];
        case ControlKind::trackButton: return *trackButtons[index];
        case ControlKind::globalButton: return *globalButtons[index];
    }
    return *knobs[0];
}

void DeviceDiagramComponent::setProfile(const ControlProfile* profile)
{
    for (const auto& control : allControls())
    {
        if (profile == nullptr)
            viewFor(control).setSummary(std::nullopt, {});
        else
            viewFor(control).setSummary(summarize(*profile, control), mixerui::utf8(describe(*profile, control)));
    }
}

void DeviceDiagramComponent::setDirtyControls(const std::vector<ControlSelection>& dirty)
{
    dirtyControls = dirty;
    repaint();
}

void DeviceDiagramComponent::setSelection(std::optional<ControlSelection> selection)
{
    if (selection && !isValidControl(*selection))
        selection.reset();
    selected = selection;
    repaint();
}

void DeviceDiagramComponent::select(ControlSelection control)
{
    selected = control;
    repaint();
    if (isShowing())
        grabKeyboardFocus();
    if (onControlSelected)
        onControlSelected(control);
}

void DeviceDiagramComponent::paint(juce::Graphics& graphics)
{
    using namespace mixerui;
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(well);
    graphics.fillRoundedRectangle(bounds, 12.0f);
    graphics.setColour(juce::Colour(0x0f3b2a14));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    graphics.setColour(muted);
    graphics.setFont(captionFont(10.0f));
    for (int index = 0; index < static_cast<int>(layout::channelCount); ++index)
        graphics.drawText(juce::String(index + 1), channelFrame(index).withHeight(14), juce::Justification::centred, false);

    // A hairline between the channel strips and the transport keys, as on the faceplate.
    graphics.setColour(hairlineStrong.withAlpha(0.7f));
    graphics.fillRect(juce::Rectangle<int>(strips.getX(), globalRow.getY() - 9, strips.getWidth(), 1));
}

void DeviceDiagramComponent::paintOverChildren(juce::Graphics& graphics)
{
    using namespace mixerui;
    // Drawn over the views, so neighbouring keys never cover the ring or the dot.
    if (selected)
    {
        const auto& view = viewFor(*selected);
        const auto area = view.markArea().toFloat();
        graphics.setColour(ink);
        if (selected->kind == ControlKind::knob)
            graphics.drawEllipse(area.expanded(3.0f), 2.0f);
        else
            graphics.drawRoundedRectangle(area.expanded(3.5f), 8.0f, 2.0f);
    }

    for (const auto& control : dirtyControls)
    {
        if (!isValidControl(control))
            continue;
        const auto area = viewFor(control).markArea().toFloat();
        const auto centre = control.kind == ControlKind::knob
            ? area.getCentre() + juce::Point<float>(area.getWidth() * 0.36f, -area.getHeight() * 0.36f)
            : juce::Point<float>(area.getRight() - 1.0f, area.getY() + 1.0f);
        graphics.setColour(well);
        graphics.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(centre));
        graphics.setColour(edited);
        graphics.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(centre));
    }
}

void DeviceDiagramComponent::resized()
{
    auto area = getLocalBounds().reduced(14, 12);
    const auto globalHeight = juce::jlimit(36, 52, area.getHeight() / 7);
    globalRow = area.removeFromBottom(globalHeight);
    area.removeFromBottom(18);
    strips = area;

    constexpr int channelGap = 10;
    const auto channelWidth = (strips.getWidth() - channelGap * 7) / 8;
    const auto spare = strips.getWidth() - (channelWidth * 8 + channelGap * 7);
    for (int index = 0; index < static_cast<int>(layout::channelCount); ++index)
    {
        const auto i = static_cast<std::size_t>(index);
        juce::Rectangle<int> frame { strips.getX() + spare / 2 + index * (channelWidth + channelGap), strips.getY(),
                                     channelWidth, strips.getHeight() };
        channelFrames[i] = frame;

        auto content = frame.withTrimmedTop(18);
        const auto knobSide = juce::jlimit(40, 76, juce::jmin(content.getWidth() - 20, content.getHeight() * 26 / 100));
        knobs[i]->setBounds(content.removeFromTop(knobSide + ControlView::knobCaptionHeight)
                                .withSizeKeepingCentre(juce::jmin(content.getWidth(), knobSide + 24),
                                                       knobSide + ControlView::knobCaptionHeight));
        content.removeFromTop(8);

        // The fader and its four keys sit together under the knob, centred in the channel.
        const auto keyWidth = juce::jlimit(30, 50, content.getWidth() * 40 / 100);
        const auto faderWidth = juce::jlimit(30, 42, content.getWidth() * 32 / 100);
        constexpr int pairGap = 8;
        const auto pairWidth = faderWidth + pairGap + keyWidth;
        auto pair = content.withSizeKeepingCentre(pairWidth, content.getHeight());
        faders[i]->setBounds(pair.removeFromLeft(faderWidth));
        pair.removeFromLeft(pairGap);

        const auto keyArea = pair.withTrimmedBottom(ControlView::faderCaptionHeight).withTrimmedTop(2);
        constexpr int keyGap = 6;
        const auto keyHeight = juce::jmin(48, (keyArea.getHeight() - keyGap * 3) / 4);
        const auto stackHeight = keyHeight * 4 + keyGap * 3;
        const auto stackTop = keyArea.getY() + (keyArea.getHeight() - stackHeight) / 2;
        for (int key = 0; key < 4; ++key)
            trackButtons[static_cast<std::size_t>(index * 4 + key)]->setBounds(
                keyArea.getX(), stackTop + key * (keyHeight + keyGap), keyArea.getWidth(), keyHeight);
    }

    constexpr int globalGap = 8;
    const auto count = static_cast<int>(layout::globalButtonCount);
    const auto keyWidth = (globalRow.getWidth() - globalGap * (count - 1)) / count;
    const auto rowSpare = globalRow.getWidth() - (keyWidth * count + globalGap * (count - 1));
    for (int index = 0; index < count; ++index)
        globalButtons[static_cast<std::size_t>(index)]->setBounds(
            globalRow.getX() + rowSpare / 2 + index * (keyWidth + globalGap), globalRow.getY(), keyWidth, globalRow.getHeight());
}

std::optional<ControlSelection> DeviceDiagramComponent::neighbour(ControlSelection from, int dx, int dy) const
{
    const auto channels = static_cast<int>(layout::channelCount);
    const auto globals = static_cast<int>(layout::globalButtonCount);

    // Rows down a channel: knob, fader, the four keys, then the transport row.
    int channel = 0;
    int row = 0;
    switch (from.kind)
    {
        case ControlKind::knob: channel = from.index; row = 0; break;
        case ControlKind::slider: channel = from.index; row = 1; break;
        case ControlKind::trackButton: channel = from.index / 4; row = 2 + from.index % 4; break;
        case ControlKind::globalButton:
            if (dx != 0)
                return ControlSelection { ControlKind::globalButton, juce::jlimit(0, globals - 1, from.index + dx) };
            if (dy > 0)
                return std::nullopt;
            return ControlSelection { ControlKind::trackButton, juce::jlimit(0, channels - 1, from.index * channels / globals) * 4 + 3 };
    }

    if (dx != 0)
        channel = juce::jlimit(0, channels - 1, channel + dx);
    row += dy;
    if (row < 0)
        return std::nullopt;
    if (row == 0)
        return ControlSelection { ControlKind::knob, channel };
    if (row == 1)
        return ControlSelection { ControlKind::slider, channel };
    if (row <= 5)
        return ControlSelection { ControlKind::trackButton, channel * 4 + row - 2 };
    return ControlSelection { ControlKind::globalButton, juce::jlimit(0, globals - 1, channel * globals / channels) };
}

bool DeviceDiagramComponent::keyPressed(const juce::KeyPress& key)
{
    int dx = 0;
    int dy = 0;
    if (key.isKeyCode(juce::KeyPress::leftKey)) dx = -1;
    else if (key.isKeyCode(juce::KeyPress::rightKey)) dx = 1;
    else if (key.isKeyCode(juce::KeyPress::upKey)) dy = -1;
    else if (key.isKeyCode(juce::KeyPress::downKey)) dy = 1;
    else return false;

    const auto next = selected ? neighbour(*selected, dx, dy) : std::optional<ControlSelection>({ ControlKind::knob, 0 });
    if (next)
        select(*next);
    return true;
}
