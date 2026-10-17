#include "MixerLookAndFeel.h"

MixerLookAndFeel::MixerLookAndFeel()
{
    setColour(juce::Label::textColourId, mixerui::ink);
    setColour(juce::TextButton::buttonColourId, mixerui::keyIdle);
    setColour(juce::TextButton::buttonOnColourId, mixerui::keyDown);
    setColour(juce::TextButton::textColourOffId, mixerui::ink);
    setColour(juce::TextButton::textColourOnId, mixerui::onShell);
    setColour(juce::PopupMenu::backgroundColourId, mixerui::plate);
    setColour(juce::PopupMenu::textColourId, mixerui::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, mixerui::well);
    setColour(juce::PopupMenu::highlightedTextColourId, mixerui::ink);
    setColour(juce::TextEditor::backgroundColourId, mixerui::plate);
    setColour(juce::TextEditor::textColourId, mixerui::ink);
    setColour(juce::TextEditor::outlineColourId, mixerui::hairline);
    setColour(juce::TextEditor::focusedOutlineColourId, mixerui::ink);
    setColour(juce::TextEditor::highlightColourId, mixerui::knob.withAlpha(0.35f));
    setColour(juce::ProgressBar::backgroundColourId, mixerui::well);
    setColour(juce::ProgressBar::foregroundColourId, mixerui::knobOutline);
    setColour(juce::ScrollBar::thumbColourId, mixerui::slot);
    setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::thumbColourId, mixerui::fader);
    setColour(juce::Slider::trackColourId, mixerui::slot);
    setColour(juce::Slider::rotarySliderFillColourId, mixerui::knob);
    setColour(juce::Slider::rotarySliderOutlineColourId, mixerui::knobOutline);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, mixerui::plate);
    setColour(juce::TooltipWindow::textColourId, mixerui::ink);
    setColour(juce::TooltipWindow::outlineColourId, mixerui::hairline);
    setColour(juce::ComboBox::backgroundColourId, mixerui::shell);
    setColour(juce::ComboBox::textColourId, mixerui::ink);
    setColour(juce::ComboBox::outlineColourId, mixerui::hairline);
    setColour(juce::ComboBox::focusedOutlineColourId, mixerui::knobOutline);
    setColour(juce::ComboBox::arrowColourId, mixerui::muted);
    setColour(juce::Label::textWhenEditingColourId, mixerui::ink);
    setColour(juce::Label::backgroundWhenEditingColourId, mixerui::plate);
    setColour(juce::Label::outlineWhenEditingColourId, mixerui::knobOutline);
}

juce::Font MixerLookAndFeel::font(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font MixerLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return font(juce::jlimit(11.0f, 13.5f, static_cast<float>(buttonHeight) * 0.46f), true);
}

int MixerLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{
    if (slider.isVertical())
        return juce::jmin(14, slider.getWidth() / 2, slider.getHeight() / 2);
    return juce::LookAndFeel_V3::getSliderThumbRadius(slider);
}

namespace
{
constexpr int menuItemHeight = 28;
constexpr float menuCorner = 8.0f;
}

void MixerLookAndFeel::drawComboBox(juce::Graphics& graphics, int width, int height, bool isButtonDown,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                          .reduced(0.5f);
    const auto enabled = box.isEnabled();
    const auto active = enabled && (box.isMouseOver(true) || isButtonDown || box.isPopupActive());

    auto fill = box.findColour(juce::ComboBox::backgroundColourId);
    if (isButtonDown || box.isPopupActive())
        fill = mixerui::well;
    else if (active)
        fill = fill.brighter(0.04f);
    graphics.setColour(fill.withMultipliedAlpha(enabled ? 1.0f : 0.6f));
    graphics.fillRoundedRectangle(area, fieldCorner);

    const auto outline = box.hasKeyboardFocus(true) || box.isPopupActive()
        ? box.findColour(juce::ComboBox::focusedOutlineColourId)
        : active ? mixerui::slot : box.findColour(juce::ComboBox::outlineColourId);
    graphics.setColour(outline.withMultipliedAlpha(enabled ? 1.0f : 0.6f));
    graphics.drawRoundedRectangle(area, fieldCorner, 1.0f);

    // A small chevron, centred in a fixed-width slot on the right.
    const auto arrowZone = area.withLeft(area.getRight() - 26.0f);
    const auto centre = arrowZone.getCentre();
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 4.0f, centre.y - 2.0f);
    chevron.lineTo(centre.x, centre.y + 2.0f);
    chevron.lineTo(centre.x + 4.0f, centre.y - 2.0f);
    graphics.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(enabled ? 1.0f : 0.5f));
    graphics.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font MixerLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return font(13.0f, true);
}

void MixerLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(4, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::PopupMenu::Options MixerLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V3::getOptionsForComboBoxPopupMenu(box, label)
        .withStandardItemHeight(menuItemHeight)
        .withMinimumWidth(box.getWidth());
}

juce::Font MixerLookAndFeel::getPopupMenuFont()
{
    return font(13.0f);
}

int MixerLookAndFeel::getPopupMenuBorderSize()
{
    return 4;
}

void MixerLookAndFeel::drawPopupMenuBackground(juce::Graphics& graphics, int width, int height)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    graphics.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    graphics.setColour(mixerui::hairline);
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
    if (isSeparator || hasSubMenu || icon != nullptr || shortcutKeyText.isNotEmpty())
    {
        LookAndFeel_V3::drawPopupMenuItem(graphics, area, isSeparator, isActive, isHighlighted, isTicked,
                                          hasSubMenu, text, shortcutKeyText, icon, textColour);
        return;
    }

    auto row = area.reduced(4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        graphics.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        graphics.fillRoundedRectangle(row, menuCorner * 0.6f);
    }

    auto content = row.reduced(8.0f, 0.0f);
    const auto tickZone = content.removeFromLeft(16.0f);
    if (isTicked)
    {
        const auto centre = tickZone.getCentre();
        graphics.setColour(mixerui::knobOutline);
        graphics.fillEllipse(centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }

    auto colour = textColour != nullptr ? *textColour : findColour(juce::PopupMenu::textColourId);
    if (isHighlighted && isActive)
        colour = findColour(juce::PopupMenu::highlightedTextColourId);
    graphics.setColour(colour.withMultipliedAlpha(isActive ? 1.0f : 0.45f));
    graphics.setFont(isTicked ? font(13.0f, true) : getPopupMenuFont());
    graphics.drawFittedText(text, content.toNearestInt(), juce::Justification::centredLeft, 1);
}

juce::Label* MixerLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V3::createSliderTextBox(slider);
    label->setFont(font(13.0f, true));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

void MixerLookAndFeel::drawLabel(juce::Graphics& graphics, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*>(label.getParentComponent()) == nullptr)
    {
        LookAndFeel_V3::drawLabel(graphics, label);
        return;
    }

    // A slider's number box, drawn as the same rounded field as the combo box.
    const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
    const auto area = label.getLocalBounds().toFloat().reduced(0.5f);
    graphics.setColour(label.findColour(juce::Label::backgroundColourId).withMultipliedAlpha(alpha));
    graphics.fillRoundedRectangle(area, fieldCorner);
    graphics.setColour(label.findColour(juce::Label::outlineColourId).withMultipliedAlpha(alpha));
    graphics.drawRoundedRectangle(area, fieldCorner, 1.0f);
    if (label.isBeingEdited())
        return;

    graphics.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
    graphics.setFont(getLabelFont(label));
    graphics.drawFittedText(label.getText(), getLabelBorderSize(label).subtractedFrom(label.getLocalBounds()),
                            label.getJustificationType(), 1, label.getMinimumHorizontalScale());
}

void MixerLookAndFeel::fillTextEditorBackground(juce::Graphics& graphics, int width, int height,
                                                juce::TextEditor& editor)
{
    if (dynamic_cast<juce::AlertWindow*>(editor.getParentComponent()) != nullptr
        || editor.isMultiLine())
    {
        LookAndFeel_V3::fillTextEditorBackground(graphics, width, height, editor);
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
    if (dynamic_cast<juce::AlertWindow*>(editor.getParentComponent()) != nullptr
        || editor.isMultiLine())
    {
        LookAndFeel_V3::drawTextEditorOutline(graphics, width, height, editor);
        return;
    }
    const auto focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();
    graphics.setColour(focused ? mixerui::knobOutline : mixerui::hairline);
    graphics.drawRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                         static_cast<float>(height)).reduced(0.5f),
                                  fieldCorner, focused ? 1.5f : 1.0f);
}

void mixerui::paintPlate(juce::Graphics& graphics, juce::Rectangle<int> bounds, float corner)
{
    const auto area = bounds.toFloat();
    graphics.setColour(plate);
    graphics.fillRoundedRectangle(area, corner);
    graphics.setColour(hairline);
    graphics.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
}
