#pragma once

#include "MixerLookAndFeel.h"

#include <JuceHeader.h>

#include <functional>

namespace mixerui
{
enum class Icon
{
    refresh,
    gear,
    undo,
    redo,
    minus,
    plus,
    list
};

void drawIcon(juce::Graphics& graphics, Icon icon, juce::Rectangle<float> area, juce::Colour colour);
// The symbol printed on each of the mixer's 11 transport and navigation keys.
void drawTransportSymbol(juce::Graphics& graphics, int globalIndex, juce::Rectangle<float> area, juce::Colour colour);
}

// A small borderless button that draws one icon. Framed icon buttons sit in toolbars.
class IconButton final : public juce::Button
{
public:
    IconButton(const juce::String& name, mixerui::Icon icon, bool framed);
    void setIcon(mixerui::Icon newIcon);
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

private:
    mixerui::Icon icon;
    bool framed;
};

// A whole-number field: type a value, drag up or down on it, scroll, use the arrow keys,
// or step with the buttons. With item names it also offers a list of every value.
// Values are stored units; a display offset shows channels 0-15 as 1-16.
class NumberField final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Label::Listener
{
public:
    NumberField();
    ~NumberField() override;

    // The new value, and whether it continues the gesture of the previous change.
    std::function<void(int value, bool continuing)> onValueChange;
    std::function<void(const juce::String& message)> onInvalidInput;
    std::function<juce::String(int value)> itemName;

    void setRange(int minimum, int maximum);
    void setDisplayOffset(int offset);
    void setValue(int value);
    int getValue() const noexcept { return value; }
    int getMinimum() const noexcept { return minimum; }
    int getMaximum() const noexcept { return maximum; }
    void setEditable(bool shouldBeEditable);
    bool isEditable() const noexcept { return editable; }
    void setShowsList(bool shouldShowList);
    void setAccent(juce::Colour colour);

    // Applies a value as if the person had entered it: clamped to the range, then reported.
    void setValueFromUser(int newValue, bool continuing = false);
    void showList();

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void enablementChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void labelTextChanged(juce::Label* label) override;
    void editorHidden(juce::Label* label, juce::TextEditor& editor) override;
    void updateText();
    void beginTyping();
    void step(int amount, const void* source);
    juce::Rectangle<int> numberArea() const;

    juce::Label text;
    IconButton decrement { "Decrease", mixerui::Icon::minus, false };
    IconButton increment { "Increase", mixerui::Icon::plus, false };
    IconButton list { "Choose from a list", mixerui::Icon::list, false };
    juce::Colour accent = mixerui::knobDeep;
    int value = 0;
    int minimum = 0;
    int maximum = 127;
    int displayOffset = 0;
    bool editable = true;
    bool showsList = false;
    int dragStartValue = 0;
    bool dragged = false;
    float wheelAccumulator = 0.0f;
    const void* lastStepSource = nullptr;
    juce::uint32 lastStepTime = 0;
};

// A brief message that floats over the bottom of the window, then fades.
class Toast final : public juce::Component,
                    private juce::Timer
{
public:
    Toast();
    void show(const juce::String& message, mixerui::Tone tone);
    const juce::String& getMessage() const noexcept { return message; }
    // The size the toast wants for its message, within a maximum width.
    juce::Rectangle<int> preferredBounds(juce::Rectangle<int> parentArea) const;
    std::function<void()> onLayoutChange;

    void paint(juce::Graphics& graphics) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::String message;
    mixerui::Tone tone = mixerui::Tone::neutral;
    juce::uint32 shownAt = 0;
    juce::uint32 lifetime = 0;
    float opacity = 0.0f;
};
