#pragma once

#include "../ControlConfiguration.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class ReadOnlySlider final : public juce::Slider
{
public:
    std::function<void()> onInspect;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override {}
    void mouseDoubleClick(const juce::MouseEvent&) override {}
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
};

class DeviceDiagramComponent final : public juce::Component
{
public:
    DeviceDiagramComponent();

    std::function<void(ControlSelection)> onControlSelected;

    void setDirtyControls(const std::vector<ControlSelection>& dirty);
    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;

private:
    void selectControl(ControlSelection selection);
    void styleSlider(ReadOnlySlider& slider, bool rotary) const;
    juce::Rectangle<int> selectionBounds() const;

    std::array<ReadOnlySlider, 8> knobs;
    std::array<ReadOnlySlider, 8> faders;
    std::array<std::unique_ptr<juce::TextButton>, 32> trackButtons;
    std::array<std::unique_ptr<juce::TextButton>, 11> globalButtons;
    std::array<juce::Rectangle<int>, 8> channelFrames;
    juce::Rectangle<int> deckBounds;
    juce::Rectangle<int> globalRowBounds;
    ControlSelection selected;
    bool hasSelection = false;
    std::array<bool, 8> dirtyKnobs {};
    std::array<bool, 8> dirtyFaders {};
    std::array<bool, 32> dirtyTrackButtons {};
    std::array<bool, 11> dirtyGlobalButtons {};
};
