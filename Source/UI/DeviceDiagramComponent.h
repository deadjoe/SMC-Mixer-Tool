#pragma once

#include "../ControlDescription.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// One control on the panel. It selects that control when clicked and draws what is stored
// for it: the CC or type, the channel, and the Min to Max range. It never sends MIDI.
class ControlView final : public juce::Button
{
public:
    explicit ControlView(ControlSelection control);

    const ControlSelection control;

    void setSummary(const std::optional<ControlSummary>& summary, const juce::String& description);
    const std::optional<ControlSummary>& getSummary() const noexcept { return summary; }
    // The part of the view that the selection ring and the edit dot belong to.
    juce::Rectangle<int> markArea() const;

    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

    static constexpr int knobCaptionHeight = 18;
    static constexpr int faderCaptionHeight = 28;

private:
    void paintKnob(juce::Graphics& graphics, bool highlighted);
    void paintFader(juce::Graphics& graphics, bool highlighted);
    void paintKey(juce::Graphics& graphics, bool highlighted, bool down);
    juce::Rectangle<float> knobCircle() const;
    juce::Rectangle<float> faderSlot() const;

    std::optional<ControlSummary> summary;
};

class DeviceDiagramComponent final : public juce::Component
{
public:
    DeviceDiagramComponent();

    std::function<void(ControlSelection)> onControlSelected;

    // nullptr shows the panel before the settings are read.
    void setProfile(const ControlProfile* profile);
    void setDirtyControls(const std::vector<ControlSelection>& dirty);
    const std::vector<ControlSelection>& getDirtyControls() const noexcept { return dirtyControls; }
    void setSelection(std::optional<ControlSelection> selection);
    std::optional<ControlSelection> getSelection() const noexcept { return selected; }

    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    ControlView& viewFor(ControlSelection control);
    juce::Rectangle<int> stripsArea() const noexcept { return strips; }
    juce::Rectangle<int> globalArea() const noexcept { return globalRow; }
    juce::Rectangle<int> channelFrame(int channel) const { return channelFrames[static_cast<std::size_t>(channel)]; }

private:
    void select(ControlSelection control);
    std::optional<ControlSelection> neighbour(ControlSelection from, int dx, int dy) const;

    std::array<std::unique_ptr<ControlView>, layout::knobCount> knobs;
    std::array<std::unique_ptr<ControlView>, layout::sliderCount> faders;
    std::array<std::unique_ptr<ControlView>, layout::trackButtonCount> trackButtons;
    std::array<std::unique_ptr<ControlView>, layout::globalButtonCount> globalButtons;
    std::array<juce::Rectangle<int>, layout::channelCount> channelFrames;
    juce::Rectangle<int> strips;
    juce::Rectangle<int> globalRow;
    std::optional<ControlSelection> selected;
    std::vector<ControlSelection> dirtyControls;
};
