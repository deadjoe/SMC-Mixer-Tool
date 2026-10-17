#pragma once

#include "ControlInspectorComponent.h"
#include "DeviceDiagramComponent.h"
#include "Widgets.h"

#include <JuceHeader.h>

#include <functional>

// The main view: a card with the profile switch, the document actions, and the panel,
// then the inspector for the selected control.
class DeviceOverviewComponent final : public juce::Component
{
public:
    DeviceOverviewComponent();

    std::function<void(DeviceProfile)> onProfileChosen;
    std::function<void()> onWrite;
    std::function<void()> onRevertAll;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onExport;
    std::function<void()> onCopy;

    struct Actions
    {
        int changedRecords = 0;
        bool canWrite = false;
        bool canRevertAll = false;
        bool canUndo = false;
        bool canRedo = false;
        bool canExport = false;
        bool canSwitchProfile = false;
    };

    void setActions(const Actions& actions);
    void setProfileKind(DeviceProfile profile);

    DeviceDiagramComponent& diagram() noexcept { return panel; }
    ControlInspectorComponent& inspector() noexcept { return details; }
    juce::TextButton& writeButton() noexcept { return write; }
    juce::Rectangle<int> panelCard() const noexcept { return card; }

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    DeviceDiagramComponent panel;
    ControlInspectorComponent details;
    juce::TextButton ccUserButton { "CC / User" };
    juce::TextButton dawButton { "DAW" };
    juce::Label profileHint;
    juce::TextButton exportButton { juce::String::fromUTF8("Export\xe2\x80\xa6") };
    juce::TextButton copyButton { "Copy JSON" };
    IconButton undoButton { "Undo", mixerui::Icon::undo, true };
    IconButton redoButton { "Redo", mixerui::Icon::redo, true };
    juce::TextButton revertAllButton { "Revert all" };
    juce::TextButton write { "Write" };
    juce::Rectangle<int> card;
    int separatorX = 0;
};
