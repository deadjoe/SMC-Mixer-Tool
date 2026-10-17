#pragma once

#include <JuceHeader.h>

#include <string>

namespace mixerui
{
// A warm faceplate: off-white surfaces, near-black ink, and one colour per control family.
// Every text colour here meets WCAG AA (4.5:1) on window, surface, well, and field.
inline const juce::Colour window { 0xfff2eee7 };
inline const juce::Colour surface { 0xfffbf9f5 };
inline const juce::Colour well { 0xffece6dc };
inline const juce::Colour field { 0xfffffefb };
inline const juce::Colour slot { 0xffd5ccbe };
inline const juce::Colour hairline { 0xffe2dace };
inline const juce::Colour hairlineStrong { 0xffcdc3b4 };

inline const juce::Colour ink { 0xff22201d };
inline const juce::Colour inkSoft { 0xff4a443d };
inline const juce::Colour muted { 0xff655d53 };
inline const juce::Colour faint { 0xffa39a8e }; // decoration only, never text that must be read
inline const juce::Colour onDark { 0xfffffdf9 };

// One colour for each family of assignable controls.
inline const juce::Colour knob { 0xffc09855 };
inline const juce::Colour knobDeep { 0xff7a5c33 };
inline const juce::Colour fader { 0xffb4483c };
inline const juce::Colour trackButton { 0xff4d6a57 };
inline const juce::Colour globalButton { 0xff33465b };

inline const juce::Colour edited { 0xffc65f1c }; // the dot on a control with an unsaved edit
inline const juce::Colour success { 0xff2f6141 };
inline const juce::Colour warning { 0xff8a5410 };
inline const juce::Colour error { 0xffa13328 };

enum class Weight
{
    regular,
    medium,
    semibold,
    bold
};

enum class Tone
{
    neutral,
    busy,
    success,
    warning,
    error
};

juce::Colour toneColour(Tone tone);

// The platform UI font at a point size: SF on macOS, Segoe UI on Windows.
juce::Font font(float points, Weight weight = Weight::regular);
// Small uppercase captions get a little extra tracking.
juce::Font captionFont(float points = 10.0f);
juce::String systemFontName();

inline juce::String utf8(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

void paintCard(juce::Graphics& graphics, juce::Rectangle<float> bounds, float corner = 12.0f);
}

// Flat controls drawn to match the faceplate: rounded fields, quiet buttons, and one dark
// primary button. The panel's knobs, faders, and keys are drawn by DeviceDiagramComponent.
class MixerLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MixerLookAndFeel();

    static constexpr int fieldHeight = 32;
    static constexpr float fieldCorner = 7.0f;
    static constexpr float buttonCorner = 7.0f;

    // Marks a TextButton as the primary action: dark fill, light text.
    static void setPrimary(juce::TextButton& button, bool primary);
    static bool isPrimary(const juce::Component& component);

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

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
    void drawPopupMenuColumnSeparatorWithOptions(juce::Graphics&, const juce::Rectangle<int>& bounds,
                                                 const juce::PopupMenu::Options&) override;

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override;
};
