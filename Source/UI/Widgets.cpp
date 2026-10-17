#include "Widgets.h"

namespace
{
constexpr int dragPixelsPerStep = 4;
constexpr juce::uint32 gestureWindowMs = 600;

juce::Path triangle(juce::Point<float> a, juce::Point<float> b, juce::Point<float> c)
{
    juce::Path path;
    path.addTriangle(a, b, c);
    return path;
}

juce::Path roundedTriangle(juce::Point<float> a, juce::Point<float> b, juce::Point<float> c)
{
    return triangle(a, b, c).createPathWithRoundedCorners(1.2f);
}

class NumberValue final : public juce::AccessibilityRangedNumericValueInterface
{
public:
    explicit NumberValue(NumberField& owner) : field(owner) {}
    bool isReadOnly() const override { return !field.isEditable() || !field.isEnabled(); }
    double getCurrentValue() const override { return field.getValue(); }
    void setValue(double newValue) override { field.setValueFromUser(juce::roundToInt(newValue)); }
    AccessibleValueRange getRange() const override
    {
        if (field.getMinimum() >= field.getMaximum())
            return {};
        return { { static_cast<double>(field.getMinimum()), static_cast<double>(field.getMaximum()) }, 1.0 };
    }

private:
    NumberField& field;
};
}

void mixerui::drawIcon(juce::Graphics& graphics, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto side = juce::jmin(area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre(side, side);
    const auto centre = box.getCentre();
    const auto stroke = juce::PathStrokeType(juce::jmax(1.3f, side * 0.1f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);
    graphics.setColour(colour);
    juce::Path path;

    switch (icon)
    {
        case Icon::refresh:
        {
            const auto radius = side * 0.34f;
            path.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.35f, true);
            graphics.strokePath(path, stroke);
            const auto tip = centre.getPointOnCircumference(radius, juce::MathConstants<float>::twoPi - 0.35f);
            const auto head = side * 0.16f;
            graphics.fillPath(roundedTriangle({ tip.x - head, tip.y - head * 0.9f }, { tip.x + head * 0.7f, tip.y - head * 0.2f },
                                              { tip.x - head * 0.1f, tip.y + head * 0.8f }));
            break;
        }
        case Icon::gear:
        {
            const auto outer = side * 0.44f;
            const auto inner = outer * 0.7f;
            for (int index = 0; index < 8; ++index)
            {
                juce::Rectangle<float> tooth(-side * 0.075f, -outer, side * 0.15f, outer - inner + side * 0.08f);
                path.addRoundedRectangle(tooth, side * 0.03f);
                path.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::twoPi / 8.0f));
            }
            path.addEllipse(-inner, -inner, inner * 2.0f, inner * 2.0f);
            path.setUsingNonZeroWinding(true);
            graphics.fillPath(path, juce::AffineTransform::translation(centre));
            break;
        }
        case Icon::undo:
        case Icon::redo:
        {
            // A hook: back along the top to an arrowhead, round on the right. Redo mirrors it.
            const auto radius = side * 0.2f;
            const auto top = centre.y - side * 0.16f;
            const auto turn = juce::Point<float>(centre.x + side * 0.1f, top + radius);
            path.startNewSubPath(centre.x - side * 0.12f, top + radius * 2.0f);
            path.lineTo(turn.x, top + radius * 2.0f);
            path.addCentredArc(turn.x, turn.y, radius, radius, 0.0f, juce::MathConstants<float>::pi, 0.0f, false);
            path.lineTo(centre.x - side * 0.2f, top);
            const auto head = side * 0.15f;
            auto arrow = roundedTriangle({ centre.x - side * 0.38f, top }, { centre.x - side * 0.2f, top - head },
                                         { centre.x - side * 0.2f, top + head });
            if (icon == Icon::redo)
            {
                const auto mirror = juce::AffineTransform::scale(-1.0f, 1.0f, centre.x, centre.y);
                path.applyTransform(mirror);
                arrow.applyTransform(mirror);
            }
            graphics.strokePath(path, stroke);
            graphics.fillPath(arrow);
            break;
        }
        case Icon::minus:
        case Icon::plus:
        {
            const auto half = side * 0.24f;
            path.startNewSubPath(centre.x - half, centre.y);
            path.lineTo(centre.x + half, centre.y);
            if (icon == Icon::plus)
            {
                path.startNewSubPath(centre.x, centre.y - half);
                path.lineTo(centre.x, centre.y + half);
            }
            graphics.strokePath(path, stroke);
            break;
        }
        case Icon::list:
        {
            const auto half = side * 0.2f;
            path.startNewSubPath(centre.x - half, centre.y - half * 0.45f);
            path.lineTo(centre.x, centre.y + half * 0.55f);
            path.lineTo(centre.x + half, centre.y - half * 0.45f);
            graphics.strokePath(path, stroke);
            break;
        }
    }
}

void mixerui::drawTransportSymbol(juce::Graphics& graphics, int globalIndex, juce::Rectangle<float> area,
                                  juce::Colour colour)
{
    const auto side = juce::jmin(area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre(side, side);
    const auto c = box.getCentre();
    const auto s = side * 0.5f;
    graphics.setColour(colour);
    const auto stroke = juce::PathStrokeType(juce::jmax(1.4f, side * 0.11f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);

    const auto chevron = [&](float x, float direction) {
        juce::Path path;
        path.startNewSubPath(x - direction * s * 0.28f, c.y - s * 0.5f);
        path.lineTo(x + direction * s * 0.22f, c.y);
        path.lineTo(x - direction * s * 0.28f, c.y + s * 0.5f);
        graphics.strokePath(path, stroke);
    };
    const auto outlineTriangle = [&](juce::Point<float> a, juce::Point<float> b, juce::Point<float> d) {
        graphics.strokePath(roundedTriangle(a, b, d), juce::PathStrokeType(juce::jmax(1.3f, side * 0.09f)));
    };

    switch (globalIndex)
    {
        case 0: // Play
            graphics.fillPath(roundedTriangle({ c.x - s * 0.42f, c.y - s * 0.55f }, { c.x + s * 0.58f, c.y },
                                              { c.x - s * 0.42f, c.y + s * 0.55f }));
            break;
        case 1: // Stop, printed as a pause sign
            graphics.fillRoundedRectangle(c.x - s * 0.42f, c.y - s * 0.5f, s * 0.3f, s, s * 0.08f);
            graphics.fillRoundedRectangle(c.x + s * 0.12f, c.y - s * 0.5f, s * 0.3f, s, s * 0.08f);
            break;
        case 2: // Record
            graphics.fillEllipse(c.x - s * 0.45f, c.y - s * 0.45f, s * 0.9f, s * 0.9f);
            break;
        case 3: // Rewind
        case 4: // Fast forward
        {
            const auto direction = globalIndex == 3 ? -1.0f : 1.0f;
            for (const auto offset : { -0.36f, 0.36f })
            {
                const auto x = c.x + offset * s;
                graphics.fillPath(roundedTriangle({ x - direction * s * 0.38f, c.y - s * 0.46f },
                                                  { x + direction * s * 0.38f, c.y },
                                                  { x - direction * s * 0.38f, c.y + s * 0.46f }));
            }
            break;
        }
        case 5: // Bank previous
            chevron(c.x - s * 0.22f, -1.0f);
            chevron(c.x + s * 0.28f, -1.0f);
            break;
        case 6: // Bank next
            chevron(c.x - s * 0.28f, 1.0f);
            chevron(c.x + s * 0.22f, 1.0f);
            break;
        case 7: // Up
            outlineTriangle({ c.x, c.y - s * 0.45f }, { c.x + s * 0.52f, c.y + s * 0.4f }, { c.x - s * 0.52f, c.y + s * 0.4f });
            break;
        case 8: // Down
            outlineTriangle({ c.x, c.y + s * 0.45f }, { c.x + s * 0.52f, c.y - s * 0.4f }, { c.x - s * 0.52f, c.y - s * 0.4f });
            break;
        case 9: // Left
            outlineTriangle({ c.x - s * 0.45f, c.y }, { c.x + s * 0.4f, c.y - s * 0.52f }, { c.x + s * 0.4f, c.y + s * 0.52f });
            break;
        case 10: // Right
            outlineTriangle({ c.x + s * 0.45f, c.y }, { c.x - s * 0.4f, c.y - s * 0.52f }, { c.x - s * 0.4f, c.y + s * 0.52f });
            break;
        default:
            break;
    }
}

IconButton::IconButton(const juce::String& name, mixerui::Icon iconToDraw, bool hasFrame)
    : juce::Button(name), icon(iconToDraw), framed(hasFrame)
{
    setTooltip(name);
    setTitle(name);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon(mixerui::Icon newIcon)
{
    icon = newIcon;
    repaint();
}

void IconButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    using namespace mixerui;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto enabled = isEnabled();
    const auto on = getToggleState();

    if (framed)
    {
        auto fill = on ? ink : field;
        if (enabled && down)
            fill = on ? fill.brighter(0.18f) : fill.darker(0.08f);
        else if (enabled && highlighted)
            fill = on ? fill.brighter(0.1f) : fill.darker(0.035f);
        if (!on && enabled)
        {
            graphics.setColour(juce::Colour(0x0d3b2a14));
            graphics.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), MixerLookAndFeel::buttonCorner);
        }
        graphics.setColour(enabled ? fill : fill.withMultipliedAlpha(0.55f));
        graphics.fillRoundedRectangle(bounds, MixerLookAndFeel::buttonCorner);
        if (!on)
        {
            graphics.setColour(enabled ? hairlineStrong : hairline);
            graphics.drawRoundedRectangle(bounds, MixerLookAndFeel::buttonCorner, 1.0f);
        }
    }
    else if (enabled && (highlighted || down))
    {
        graphics.setColour(ink.withAlpha(down ? 0.12f : 0.06f));
        graphics.fillRoundedRectangle(bounds, 5.0f);
    }

    const auto colour = !enabled ? faint : on && framed ? onDark : highlighted ? ink : inkSoft;
    const auto inset = framed ? bounds.getHeight() * 0.27f : bounds.getHeight() * 0.18f;
    drawIcon(graphics, icon, bounds.reduced(inset), colour);
    if (icon == Icon::gear)
    {
        // The hole in the middle shows the button underneath.
        const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight()) - inset * 2.0f;
        const auto hole = side * 0.44f * 0.32f;
        const auto centre = bounds.getCentre();
        graphics.setColour(framed ? (on ? ink : field) : surface);
        graphics.fillEllipse(centre.x - hole, centre.y - hole, hole * 2.0f, hole * 2.0f);
    }
}

NumberField::NumberField()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);

    text.setJustificationType(juce::Justification::centred);
    text.setFont(mixerui::font(14.0f, mixerui::Weight::semibold));
    text.setColour(juce::Label::textColourId, mixerui::ink);
    text.setInterceptsMouseClicks(false, false);
    text.addListener(this);
    addAndMakeVisible(text);

    for (auto* button : { &decrement, &increment, &list })
    {
        button->setWantsKeyboardFocus(false);
        button->setRepeatSpeed(380, 60);
        addChildComponent(*button);
    }
    list.setRepeatSpeed(0, 0);
    decrement.onClick = [this] { step(-1, &decrement); };
    increment.onClick = [this] { step(1, &increment); };
    list.onClick = [this] { showList(); };
    setEditable(true);
}

NumberField::~NumberField()
{
    text.removeListener(this);
}

void NumberField::setRange(int newMinimum, int newMaximum)
{
    minimum = newMinimum;
    maximum = juce::jmax(newMinimum, newMaximum);
    repaint();
}

void NumberField::setDisplayOffset(int offset)
{
    displayOffset = offset;
    updateText();
}

void NumberField::setValue(int newValue)
{
    value = newValue;
    updateText();
}

void NumberField::setEditable(bool shouldBeEditable)
{
    editable = shouldBeEditable;
    decrement.setVisible(editable);
    increment.setVisible(editable);
    list.setVisible(editable && showsList);
    setMouseCursor(editable ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(editable);
    text.setColour(juce::Label::textColourId, editable ? mixerui::ink : mixerui::inkSoft);
    resized();
    repaint();
}

void NumberField::setShowsList(bool shouldShowList)
{
    showsList = shouldShowList;
    list.setVisible(editable && showsList);
    resized();
}

void NumberField::setAccent(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void NumberField::updateText()
{
    text.setText(juce::String(value + displayOffset), juce::dontSendNotification);
    repaint();
}

void NumberField::setValueFromUser(int newValue, bool continuing)
{
    if (!editable || !isEnabled())
        return;
    const auto clamped = juce::jlimit(minimum, maximum, newValue);
    if (clamped == value)
    {
        updateText();
        return;
    }
    value = clamped;
    updateText();
    if (onValueChange)
        onValueChange(value, continuing);
}

void NumberField::step(int amount, const void* source)
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto continuing = source == lastStepSource && now - lastStepTime < gestureWindowMs;
    lastStepSource = source;
    lastStepTime = now;
    setValueFromUser(value + amount, continuing);
}

void NumberField::showList()
{
    if (!editable || !isEnabled())
        return;
    juce::PopupMenu menu;
    const auto count = maximum - minimum + 1;
    for (int option = minimum; option <= maximum; ++option)
    {
        auto label = juce::String(option + displayOffset);
        if (itemName)
            label << "  " << itemName(option);
        menu.addItem(option - minimum + 1, label, true, option == value);
    }
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(this)
                       .withMinimumWidth(getWidth())
                       .withStandardItemHeight(24)
                       .withMaximumNumColumns(count > 32 ? 4 : 1);
    menu.showMenuAsync(options, [safe = juce::Component::SafePointer<NumberField>(this)](int result) {
        if (safe != nullptr && result > 0)
            safe->setValueFromUser(safe->minimum + result - 1);
    });
}

juce::Rectangle<int> NumberField::numberArea() const
{
    auto area = getLocalBounds();
    if (editable)
    {
        area.removeFromRight(showsList ? 66 : 46);
        area.removeFromLeft(4);
    }
    return area;
}

void NumberField::resized()
{
    auto area = getLocalBounds().reduced(3);
    if (editable)
    {
        if (showsList)
            list.setBounds(area.removeFromRight(20));
        increment.setBounds(area.removeFromRight(22));
        decrement.setBounds(area.removeFromRight(22));
    }
    text.setBounds(numberArea());
}

void NumberField::paint(juce::Graphics& graphics)
{
    using namespace mixerui;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto enabled = isEnabled();
    const auto focused = hasKeyboardFocus(true) || text.isBeingEdited();
    const auto outside = value < minimum || value > maximum;

    if (editable)
    {
        graphics.setColour(juce::Colour(0x0a3b2a14));
        graphics.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), MixerLookAndFeel::fieldCorner);
    }
    graphics.setColour(editable && enabled ? field : well);
    graphics.fillRoundedRectangle(bounds, MixerLookAndFeel::fieldCorner);
    graphics.setColour(outside ? warning : focused ? accent : editable ? hairlineStrong : hairline);
    graphics.drawRoundedRectangle(bounds, MixerLookAndFeel::fieldCorner, focused || outside ? 1.5f : 1.0f);

    if (editable)
    {
        // A hairline divides the number from its step buttons.
        const auto x = static_cast<float>(numberArea().getRight()) + 1.5f;
        graphics.setColour(hairline);
        graphics.fillRect(x, bounds.getY() + 7.0f, 1.0f, bounds.getHeight() - 14.0f);
    }
}

void NumberField::mouseDown(const juce::MouseEvent&)
{
    dragStartValue = value;
    dragged = false;
}

void NumberField::mouseDrag(const juce::MouseEvent& event)
{
    if (!editable || !numberArea().contains(event.getMouseDownPosition()))
        return;
    const auto steps = -event.getDistanceFromDragStartY() / dragPixelsPerStep;
    if (steps == 0 && !dragged)
        return;
    setValueFromUser(dragStartValue + steps, dragged);
    dragged = true;
}

void NumberField::mouseUp(const juce::MouseEvent& event)
{
    if (!dragged && numberArea().contains(event.getPosition()))
        beginTyping();
}

void NumberField::beginTyping()
{
    if (!editable || !isEnabled())
        return;
    text.setEditable(true, false, false);
    text.showEditor();
    if (auto* editor = text.getCurrentTextEditor())
    {
        editor->setJustification(juce::Justification::centred);
        editor->setInputRestrictions(3, "0123456789");
        editor->selectAll();
    }
}

void NumberField::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (!editable)
        return;
    wheelAccumulator += (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
    const auto steps = static_cast<int>(wheelAccumulator / 0.12f);
    if (steps == 0)
        return;
    wheelAccumulator -= static_cast<float>(steps) * 0.12f;
    step(steps, &wheelAccumulator);
}

bool NumberField::keyPressed(const juce::KeyPress& key)
{
    if (!editable)
        return false;
    const auto amount = key.getModifiers().isShiftDown() ? 10 : 1;
    if (key.isKeyCode(juce::KeyPress::upKey))
    {
        step(amount, &text);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::downKey))
    {
        step(-amount, &text);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey))
    {
        beginTyping();
        return true;
    }
    return false;
}

void NumberField::focusGained(FocusChangeType)
{
    repaint();
}

void NumberField::focusLost(FocusChangeType)
{
    repaint();
}

void NumberField::enablementChanged()
{
    text.setColour(juce::Label::textColourId, isEnabled() ? mixerui::ink : mixerui::muted);
    repaint();
}

void NumberField::labelTextChanged(juce::Label*)
{
    const auto entered = text.getText().trim();
    const auto low = minimum + displayOffset;
    const auto high = maximum + displayOffset;
    const auto parsed = entered.getIntValue();
    if (entered.isEmpty() || !entered.containsOnly("0123456789") || parsed < low || parsed > high)
    {
        updateText();
        if (onInvalidInput)
            onInvalidInput("Enter a whole number from " + juce::String(low) + " to " + juce::String(high) + ".");
        return;
    }
    setValueFromUser(parsed - displayOffset);
}

void NumberField::editorHidden(juce::Label*, juce::TextEditor&)
{
    text.setEditable(false, false, false);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> NumberField::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider,
                                                        juce::AccessibilityActions {},
                                                        juce::AccessibilityHandler::Interfaces {
                                                            std::make_unique<NumberValue>(*this) });
}

Toast::Toast()
{
    setInterceptsMouseClicks(true, false);
    setAlwaysOnTop(true);
    setVisible(false);
}

void Toast::show(const juce::String& newMessage, mixerui::Tone newTone)
{
    message = newMessage;
    tone = newTone;
    shownAt = juce::Time::getMillisecondCounter();
    // Long enough to read: a base time plus time per word. Errors stay longer.
    const auto words = static_cast<juce::uint32>(juce::StringArray::fromTokens(message, true).size());
    lifetime = juce::jlimit<juce::uint32>(2600, 12000, 1800 + words * 260) + (tone == mixerui::Tone::error ? 3000 : 0);
    setVisible(true);
    if (onLayoutChange)
        onLayoutChange();
    startTimerHz(60);
    repaint();
}

juce::Rectangle<int> Toast::preferredBounds(juce::Rectangle<int> parentArea) const
{
    const auto maxWidth = juce::jmin(560, parentArea.getWidth() - 48);
    const auto textWidth = juce::GlyphArrangement::getStringWidthInt(mixerui::font(12.5f, mixerui::Weight::medium), message);
    const auto width = juce::jlimit(200, maxWidth, textWidth + 64);
    const auto lines = textWidth + 64 > maxWidth ? 2 : 1;
    const auto height = lines == 1 ? 40 : 56;
    return { parentArea.getCentreX() - width / 2, parentArea.getBottom() - height - 24, width, height };
}

void Toast::paint(juce::Graphics& graphics)
{
    using namespace mixerui;
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colours::black.withAlpha(0.12f * opacity));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), 10.0f);
    graphics.setColour(ink.withAlpha(0.96f * opacity));
    graphics.fillRoundedRectangle(bounds, 10.0f);

    auto content = bounds.reduced(16.0f, 0.0f);
    const auto dotArea = content.removeFromLeft(10.0f);
    const auto dotColour = tone == Tone::success ? juce::Colour(0xff7fc497)
                         : tone == Tone::warning ? juce::Colour(0xffe8b45c)
                         : tone == Tone::error ? juce::Colour(0xffff8a7a)
                                               : juce::Colour(0xffd8cfc2);
    graphics.setColour(dotColour.withAlpha(opacity));
    graphics.fillEllipse(dotArea.withSizeKeepingCentre(8.0f, 8.0f));
    content.removeFromLeft(10.0f);
    graphics.setColour(onDark.withAlpha(opacity));
    graphics.setFont(font(12.5f, Weight::medium));
    graphics.drawFittedText(message, content.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
}

void Toast::mouseUp(const juce::MouseEvent&)
{
    lifetime = juce::jmin(lifetime, juce::Time::getMillisecondCounter() - shownAt);
}

void Toast::timerCallback()
{
    const auto age = juce::Time::getMillisecondCounter() - shownAt;
    constexpr float fade = 180.0f;
    const auto target = age < lifetime ? 1.0f : 0.0f;
    opacity = target > opacity ? juce::jmin(1.0f, opacity + 1000.0f / 60.0f / fade)
                               : juce::jmax(0.0f, opacity - 1000.0f / 60.0f / fade);
    repaint();
    if (target == 0.0f && opacity <= 0.0f)
    {
        stopTimer();
        setVisible(false);
    }
}
