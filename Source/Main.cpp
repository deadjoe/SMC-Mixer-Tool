#include "MainComponent.h"

#include <JuceHeader.h>

#include <limits>
#include <memory>

class SMCMixerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SMC Mixer Tool"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
       #if JUCE_MAC
        auto& main = mainWindow->main();
        juce::PopupMenu appleItems;
        appleItems.addCommandItem(&main.commands(), MainComponent::aboutCommand);
        juce::MenuBarModel::setMacMainMenu(&main.menuModel(), &appleItems);
       #endif
    }

    void shutdown() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
       #endif
        mainWindow.reset();
    }

    // Cmd+Q, the Dock menu, and logging out all arrive here, so they ask first, as the
    // window's close button does.
    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->requestClose();
        else
            quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name, mixerui::window, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(MainComponent::Options {}), true);
            setResizable(true, true);
            setResizeLimits(MainComponent::minimumWidth, MainComponent::minimumHeight,
                            std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
            centreWithSize(1360, 900);
            setVisible(true);
        }

        MainComponent& main() { return *static_cast<MainComponent*>(getContentComponent()); }

        void requestClose()
        {
            main().requestQuit([] { juce::JUCEApplication::quit(); });
        }

        void closeButtonPressed() override
        {
            requestClose();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SMCMixerApplication)
