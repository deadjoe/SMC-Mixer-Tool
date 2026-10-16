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

void mixerui::paintPlate(juce::Graphics& graphics, juce::Rectangle<int> bounds, float corner)
{
    const auto area = bounds.toFloat();
    graphics.setColour(plate);
    graphics.fillRoundedRectangle(area, corner);
    graphics.setColour(hairline);
    graphics.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
}
