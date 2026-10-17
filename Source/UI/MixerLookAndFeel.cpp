#include "MixerLookAndFeel.h"

namespace
{
constexpr int menuItemHeight = 28;
const juce::Identifier primaryProperty { "smcPrimary" };

juce::String styleName(mixerui::Weight weight)
{
    switch (weight)
    {
        case mixerui::Weight::regular: return "Regular";
        case mixerui::Weight::medium: return "Medium";
        case mixerui::Weight::semibold: return "Semibold";
        case mixerui::Weight::bold: return "Bold";
    }
    return "Regular";
}

juce::TextLayout tooltipLayout(const juce::String& text)
{
    juce::AttributedString string;
    string.setJustification(juce::Justification::topLeft);
    string.append(text, mixerui::font(11.5f), mixerui::onDark);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(string, 320.0f);
    return layout;
}
}

juce::String mixerui::systemFontName()
{
   #if JUCE_MAC
    // CoreText resolves this private family name to SF in every weight.
    return ".AppleSystemUIFont";
   #elif JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

juce::Font mixerui::font(float points, Weight weight)
{
    return juce::Font(juce::FontOptions(systemFontName(), styleName(weight), 12.0f).withPointHeight(points));
}

juce::Font mixerui::captionFont(float points)
{
    return juce::Font(juce::FontOptions(systemFontName(), styleName(Weight::semibold), 12.0f)
                          .withPointHeight(points)
                          .withKerningFactor(0.06f));
}

juce::Colour mixerui::toneColour(Tone tone)
{
    switch (tone)
    {
        case Tone::neutral: return muted;
        case Tone::busy: return ink;
        case Tone::success: return success;
        case Tone::warning: return warning;
        case Tone::error: return error;
    }
    return muted;
}

void mixerui::paintCard(juce::Graphics& graphics, juce::Rectangle<float> bounds, float corner)
{
    // A soft two-step shadow, then the card and its hairline.
    graphics.setColour(juce::Colour(0x0a3b2a14));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 2.0f).expanded(0.5f), corner + 0.5f);
    graphics.setColour(juce::Colour(0x063b2a14));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 6.0f).reduced(4.0f, 0.0f), corner);
    graphics.setColour(surface);
    graphics.fillRoundedRectangle(bounds, corner);
    graphics.setColour(hairline);
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
}

MixerLookAndFeel::MixerLookAndFeel()
    : juce::LookAndFeel_V4(juce::LookAndFeel_V4::getLightColourScheme())
{
    setDefaultSansSerifTypefaceName(mixerui::systemFontName());

    using namespace mixerui;
    setColour(juce::ResizableWindow::backgroundColourId, window);
    setColour(juce::DocumentWindow::backgroundColourId, window);
    setColour(juce::Label::textColourId, ink);
    setColour(juce::TextButton::buttonColourId, field);
    setColour(juce::TextButton::buttonOnColourId, ink);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, onDark);
    setColour(juce::ComboBox::backgroundColourId, field);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, hairlineStrong);
    setColour(juce::ComboBox::focusedOutlineColourId, knobDeep);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, field);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, well);
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::TextEditor::backgroundColourId, field);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, hairlineStrong);
    setColour(juce::TextEditor::focusedOutlineColourId, knobDeep);
    setColour(juce::TextEditor::highlightColourId, knob.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, ink);
    setColour(juce::CaretComponent::caretColourId, ink);
    setColour(juce::Label::textWhenEditingColourId, ink);
    setColour(juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, slot);
    setColour(juce::TooltipWindow::backgroundColourId, ink);
    setColour(juce::TooltipWindow::textColourId, onDark);
    setColour(juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);
}

void MixerLookAndFeel::setPrimary(juce::TextButton& button, bool primary)
{
    button.getProperties().set(primaryProperty, primary);
    button.repaint();
}

bool MixerLookAndFeel::isPrimary(const juce::Component& component)
{
    return static_cast<bool>(component.getProperties().getWithDefault(primaryProperty, false));
}

juce::Font MixerLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return mixerui::font(juce::jlimit(11.0f, 13.0f, static_cast<float>(buttonHeight) * 0.42f), mixerui::Weight::medium);
}

void MixerLookAndFeel::drawButtonBackground(juce::Graphics& graphics, juce::Button& button,
                                            const juce::Colour& backgroundColour, bool highlighted, bool down)
{
    using namespace mixerui;
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto enabled = button.isEnabled();
    const auto on = button.getToggleState();
    const auto primary = isPrimary(button) && enabled;

    auto fill = primary ? ink : on ? button.findColour(juce::TextButton::buttonOnColourId) : backgroundColour;
    if (enabled && down)
        fill = fill.getPerceivedBrightness() > 0.5f ? fill.darker(0.08f) : fill.brighter(0.18f);
    else if (enabled && highlighted)
        fill = fill.getPerceivedBrightness() > 0.5f ? fill.darker(0.035f) : fill.brighter(0.1f);

    // Segmented controls join their buttons along the connected edges.
    const auto left = !button.isConnectedOnLeft();
    const auto right = !button.isConnectedOnRight();
    juce::Path shape;
    shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                              buttonCorner, buttonCorner, left, right, left, right);

    if (enabled && !on && !primary && fill.getPerceivedBrightness() > 0.5f)
    {
        graphics.setColour(juce::Colour(0x0d3b2a14));
        graphics.fillPath(shape, juce::AffineTransform::translation(0.0f, 1.0f));
    }
    // A selected segment that cannot be changed keeps a quiet fill rather than a grey ink block.
    graphics.setColour(enabled ? fill : on ? well.darker(0.04f) : fill.withMultipliedAlpha(0.55f));
    graphics.fillPath(shape);

    if (!enabled && on)
    {
        graphics.setColour(hairlineStrong);
        graphics.strokePath(shape, juce::PathStrokeType(1.0f));
    }
    else if (!on && !primary)
    {
        graphics.setColour(enabled ? hairlineStrong : hairline);
        graphics.strokePath(shape, juce::PathStrokeType(1.0f));
    }
    if (button.hasKeyboardFocus(false) && button.isShowing() && button.getWantsKeyboardFocus())
    {
        graphics.setColour(knob.withAlpha(0.8f));
        graphics.strokePath(shape, juce::PathStrokeType(2.0f));
    }
}

void MixerLookAndFeel::drawButtonText(juce::Graphics& graphics, juce::TextButton& button, bool, bool)
{
    const auto primary = isPrimary(button) && button.isEnabled();
    auto colour = primary ? mixerui::onDark
                          : button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                      : juce::TextButton::textColourOffId);
    if (!button.isEnabled())
        colour = button.getToggleState() ? mixerui::muted : mixerui::faint;
    graphics.setColour(colour);
    graphics.setFont(getTextButtonFont(button, button.getHeight()));
    graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(10, 2),
                            juce::Justification::centred, 1, 0.8f);
}

void MixerLookAndFeel::drawComboBox(juce::Graphics& graphics, int width, int height, bool isButtonDown,
                                    int, int, int, int, juce::ComboBox& box)
{
    using namespace mixerui;
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                          .reduced(0.5f);
    const auto enabled = box.isEnabled();
    const auto hot = enabled && (box.isMouseOver(true) || isButtonDown || box.isPopupActive());

    graphics.setColour(juce::Colour(0x0a3b2a14));
    graphics.fillRoundedRectangle(area.translated(0.0f, 1.0f), fieldCorner);
    auto fill = box.findColour(juce::ComboBox::backgroundColourId);
    if (hot)
        fill = fill.darker(0.025f);
    graphics.setColour(enabled ? fill : well);
    graphics.fillRoundedRectangle(area, fieldCorner);

    const auto focused = box.hasKeyboardFocus(true) || box.isPopupActive();
    graphics.setColour(focused ? box.findColour(juce::ComboBox::focusedOutlineColourId) : hot ? slot : hairlineStrong);
    graphics.drawRoundedRectangle(area, fieldCorner, focused ? 1.5f : 1.0f);

    // Up and down chevrons, as on a macOS pop-up button.
    const auto centre = area.withLeft(area.getRight() - 24.0f).getCentre();
    juce::Path chevrons;
    chevrons.startNewSubPath(centre.x - 3.5f, centre.y - 2.0f);
    chevrons.lineTo(centre.x, centre.y - 5.5f);
    chevrons.lineTo(centre.x + 3.5f, centre.y - 2.0f);
    chevrons.startNewSubPath(centre.x - 3.5f, centre.y + 2.0f);
    chevrons.lineTo(centre.x, centre.y + 5.5f);
    chevrons.lineTo(centre.x + 3.5f, centre.y + 2.0f);
    graphics.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(enabled ? 1.0f : 0.5f));
    graphics.strokePath(chevrons, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font MixerLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return mixerui::font(13.0f, mixerui::Weight::medium);
}

void MixerLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, box.isEnabled() ? mixerui::ink : mixerui::muted);
}

juce::PopupMenu::Options MixerLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label)
        .withStandardItemHeight(menuItemHeight)
        .withMinimumWidth(box.getWidth());
}

juce::Font MixerLookAndFeel::getPopupMenuFont()
{
    return mixerui::font(13.0f);
}

int MixerLookAndFeel::getPopupMenuBorderSize()
{
    return 5;
}

void MixerLookAndFeel::drawPopupMenuBackground(juce::Graphics& graphics, int width, int height)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    graphics.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    graphics.setColour(mixerui::hairlineStrong);
    graphics.drawRect(area, 1.0f);
}

void MixerLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int,
                                                 int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }
    idealHeight = menuItemHeight;
    idealWidth = juce::GlyphArrangement::getStringWidthInt(getPopupMenuFont(), text) + 48;
}

void MixerLookAndFeel::drawPopupMenuItem(juce::Graphics& graphics, const juce::Rectangle<int>& area,
                                         bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                         bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                         const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (hasSubMenu || icon != nullptr)
    {
        LookAndFeel_V4::drawPopupMenuItem(graphics, area, isSeparator, isActive, isHighlighted, isTicked,
                                          hasSubMenu, text, shortcutKeyText, icon, textColour);
        return;
    }
    if (isSeparator)
    {
        graphics.setColour(mixerui::hairline);
        graphics.fillRect(area.reduced(10, 0).withSizeKeepingCentre(area.getWidth() - 20, 1));
        return;
    }

    auto row = area.reduced(4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        graphics.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        graphics.fillRoundedRectangle(row, 5.0f);
    }

    auto content = row.reduced(8.0f, 0.0f);
    const auto tickZone = content.removeFromLeft(16.0f);
    if (isTicked)
    {
        const auto centre = tickZone.getCentre();
        juce::Path tick;
        tick.startNewSubPath(centre.x - 4.0f, centre.y);
        tick.lineTo(centre.x - 1.0f, centre.y + 3.0f);
        tick.lineTo(centre.x + 4.5f, centre.y - 3.5f);
        graphics.setColour(mixerui::ink);
        graphics.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto colour = textColour != nullptr ? *textColour : findColour(juce::PopupMenu::textColourId);
    graphics.setColour(colour.withMultipliedAlpha(isActive ? 1.0f : 0.45f));
    graphics.setFont(getPopupMenuFont());
    if (shortcutKeyText.isNotEmpty())
    {
        graphics.setColour(mixerui::muted);
        graphics.drawText(shortcutKeyText, content.toNearestInt(), juce::Justification::centredRight, false);
        graphics.setColour(colour.withMultipliedAlpha(isActive ? 1.0f : 0.45f));
    }
    graphics.drawFittedText(text, content.toNearestInt(), juce::Justification::centredLeft, 1);
}

void MixerLookAndFeel::drawPopupMenuColumnSeparatorWithOptions(juce::Graphics& graphics,
                                                               const juce::Rectangle<int>& bounds,
                                                               const juce::PopupMenu::Options&)
{
    graphics.setColour(mixerui::hairline);
    graphics.fillRect(bounds.withSizeKeepingCentre(1, bounds.getHeight() - 8));
}

juce::Rectangle<int> MixerLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout(tipText);
    const auto width = static_cast<int>(layout.getWidth() + 22.0f);
    const auto height = static_cast<int>(layout.getHeight() + 14.0f);
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (width + 12) : screenPos.x + 18,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (height + 6) : screenPos.y + 8,
                                width, height)
        .constrainedWithin(parentArea);
}

void MixerLookAndFeel::drawTooltip(juce::Graphics& graphics, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    graphics.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    graphics.fillRoundedRectangle(bounds, 6.0f);
    tooltipLayout(text).draw(graphics, bounds.reduced(11.0f, 7.0f));
}

void MixerLookAndFeel::fillTextEditorBackground(juce::Graphics& graphics, int width, int height,
                                                juce::TextEditor& editor)
{
    if (editor.isMultiLine())
    {
        graphics.fillAll(editor.findColour(juce::TextEditor::backgroundColourId));
        return;
    }
    graphics.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    graphics.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                         static_cast<float>(height)).reduced(0.5f),
                                  fieldCorner);
}

void MixerLookAndFeel::drawTextEditorOutline(juce::Graphics& graphics, int width, int height,
                                             juce::TextEditor& editor)
{
    if (editor.isMultiLine())
        return;
    const auto focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();
    graphics.setColour(focused ? editor.findColour(juce::TextEditor::focusedOutlineColourId)
                               : editor.findColour(juce::TextEditor::outlineColourId));
    graphics.drawRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                         static_cast<float>(height)).reduced(0.5f),
                                  fieldCorner, focused ? 1.5f : 1.0f);
}

void MixerLookAndFeel::drawScrollbar(juce::Graphics& graphics, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                     bool isMouseOver, bool isMouseDown)
{
    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height);
    thumb = thumb.reduced(isScrollbarVertical ? 3 : 2, isScrollbarVertical ? 2 : 3);
    graphics.setColour(mixerui::slot.withAlpha(isMouseDown ? 1.0f : isMouseOver ? 0.9f : 0.7f));
    graphics.fillRoundedRectangle(thumb.toFloat(), static_cast<float>(juce::jmin(thumb.getWidth(), thumb.getHeight())) * 0.5f);
}

int MixerLookAndFeel::getDefaultScrollbarWidth()
{
    return 11;
}
