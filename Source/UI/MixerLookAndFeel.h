#pragma once

#include <JuceHeader.h>

namespace mixerui
{
// Warm milk-white shell, the colour of a hardware faceplate.
inline const juce::Colour shell { 0xfff4efe6 };
inline const juce::Colour well { 0xffe4daca };
inline const juce::Colour plate { 0xfff7f3ec };
inline const juce::Colour ink { 0xff2c2824 };
inline const juce::Colour muted { 0xff7a7268 };
inline const juce::Colour hairline { 0xffd4cbbd };
inline const juce::Colour slot { 0xffb3a58f };
inline const juce::Colour onShell { 0xfff6f1e8 };

// One colour for each family of assignable controls.
inline const juce::Colour knob { 0xffc6a15a };
inline const juce::Colour knobOutline { 0xff7a5e38 };
inline const juce::Colour fader { 0xffa33b32 };
inline const juce::Colour trackButton { 0xff4f6b58 };
inline const juce::Colour globalButton { 0xff314256 };

inline const juce::Colour keyIdle { 0xffe7dfd2 };
inline const juce::Colour keyDown { 0xff2c2824 };

inline const juce::Colour success { 0xff3e6b4a };
inline const juce::Colour warning { 0xff8a5a24 };
inline const juce::Colour error { 0xff8e3a32 };

inline const juce::Colour background = shell;
inline const juce::Colour text = ink;

inline juce::String utf8(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

void paintPlate(juce::Graphics& graphics, juce::Rectangle<int> bounds, float corner = 14.0f);
}

// JUCE's own beveled buttons, glass fader caps, and rotary knobs (LookAndFeel_V3 / V2).
class MixerLookAndFeel final : public juce::LookAndFeel_V3
{
public:
    MixerLookAndFeel();

    static juce::Font font(float height, bool bold = false);
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    int getSliderThumbRadius(juce::Slider& slider) override;

    // Combo boxes and their menus: a flat faceplate field and a compact list,
    // sized by the font rather than by the height of the box.
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    // Number boxes beside the edit sliders match the combo box field.
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    static constexpr int fieldHeight = 30;
    static constexpr float fieldCorner = 6.0f;
};
