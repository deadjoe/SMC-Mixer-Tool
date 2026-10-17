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
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name, mixerui::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(1120, 740, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
            setContentOwned(new MainComponent(), true);
            centreWithSize(1360, 880);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
            {
                if (main->hasUnsavedEdits())
                {
                    if (quitChoiceOpen)
                        return;
                    quitChoiceOpen = true;
                    // JUCE 9 leaves modal loops off, so the result arrives in the callback.
                    juce::AlertWindow::showOkCancelBox(
                        juce::MessageBoxIconType::WarningIcon,
                        "Unsaved edits",
                        "Quit and discard CC / User edits that have not been written?",
                        "Quit",
                        "Stay",
                        this,
                        juce::ModalCallbackFunction::create(
                            [safe = juce::Component::SafePointer<MainWindow>(this)](int result) {
                                if (safe == nullptr)
                                    return;
                                safe->quitChoiceOpen = false;
                                if (result == 1)
                                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
                            }));
                    return;
                }
            }
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        bool quitChoiceOpen = false;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SMCMixerApplication)
