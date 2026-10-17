#pragma once

#include "DeviceController.h"
#include "SettingsDocument.h"
#include "UI/DeviceOverviewComponent.h"
#include "UI/DiagnosticsComponent.h"
#include "UI/HeaderComponent.h"
#include "UI/MixerLookAndFeel.h"
#include "UI/Widgets.h"

#include <JuceHeader.h>

#include <functional>
#include <optional>

class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            private juce::Timer
{
public:
    struct Options
    {
        // The link to the mixer. Empty means the real one, DeviceController.
        std::unique_ptr<DeviceLink> link;
        bool connectOnStart = true;
    };

    explicit MainComponent(Options options);
    ~MainComponent() override;

    enum CommandIDs
    {
        refreshCommand = 0x2001,
        readSettingsCommand,
        writeCommand,
        exportProfileCommand,
        copyProfileCommand,
        exportImageCommand,
        undoCommand,
        redoCommand,
        revertControlCommand,
        revertAllCommand,
        showCcUserCommand,
        showDawCommand,
        toggleDiagnosticsCommand,
        useUsbCommand,
        useBluetoothCommand,
        importCommand,
        aboutCommand
    };

    static constexpr int minimumWidth = 1100;
    static constexpr int minimumHeight = 740;

    bool hasUnsavedEdits() const;
    // Asks first when edits would be lost or a write would be cut short, then calls quit.
    void requestQuit(std::function<void()> quit);

    struct Confirmation
    {
        juce::String title;
        juce::String message;
        juce::String confirmText;
        juce::String cancelText;
    };
    // Shows a confirmation and reports the answer. The default uses the system dialog;
    // tests answer directly.
    std::function<void(const Confirmation&, std::function<void(bool)>)> confirm;

    // Handles one worker event. The timer delivers them; tests may too.
    void deliver(DeviceEvent event);
    // Takes the link's events and connection changes. The timer calls it; tests may too.
    void pollLink();
    void selectControl(ControlSelection control);
    // Imports a profile JSON file or a settings image into CC / User as unsaved edits.
    void importFile(const juce::File& file);

    juce::ApplicationCommandManager& commands() noexcept { return commandManager; }
    juce::MenuBarModel& menuModel() noexcept { return menu; }
    HeaderComponent& header() noexcept { return headerBar; }
    DeviceOverviewComponent& overview() noexcept { return overviewView; }
    DiagnosticsComponent& diagnostics() noexcept { return diagnosticsView; }
    Toast& toast() noexcept { return toastView; }
    const SettingsDocument& document() const noexcept { return settings; }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class Menu final : public juce::MenuBarModel
    {
    public:
        explicit Menu(MainComponent& owner) : main(owner) {}
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
        void menuItemSelected(int, int) override {}

    private:
        MainComponent& main;
    };

    enum class Activity
    {
        idle,
        refreshing,          // reading the identity; the settings follow
        readingSettings,
        writing
    };

    void timerCallback() override;
    void refresh();
    // keepDocument reconnects without reading the settings, so unsaved edits survive a replug.
    void refreshNow(bool keepDocument = false);
    void handleConnectionChange();
    void chooseImportFile();
    void readSettings();
    void readSettingsNow();
    void write();
    void startWrite();
    void chooseTransport(TransportKind transport);
    void chooseProfile(DeviceProfile profile);
    void showDiagnostics(bool show);
    void editControl(ControlSelection control, DeviceProfile profile, EditableField field, std::uint8_t value, bool continuing);
    void revertControl();
    void revertAll();
    void undo();
    void redo();
    void exportProfile();
    void copyProfile();
    void showAbout();
    void notify(const juce::String& message, mixerui::Tone tone);
    void ask(const Confirmation& confirmation, std::function<void()> onConfirmed);
    void askBeforeDiscarding(const juce::String& action, std::function<void()> onConfirmed);
    void setActivity(Activity activity);
    void showOnlineStatus();
    void publish();
    bool editingEnabled() const;
    juce::String linkName() const;

    MixerLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ApplicationCommandManager commandManager;
    Menu menu { *this };
    HeaderComponent headerBar;
    DeviceOverviewComponent overviewView;
    DiagnosticsComponent diagnosticsView;
    Toast toastView;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ScopedMessageBox messageBox;
    bool choicePending = false;

    TransportKind transport = TransportKind::usbC;
    DeviceProfile viewProfile = DeviceProfile::ccUser;
    std::optional<ControlSelection> selection;
    std::optional<ControlConfiguration> configuration;
    Activity activity = Activity::idle;
    bool showingDiagnostics = false;
    std::optional<DeviceSnapshot> snapshot;
    juce::String settingsProblem;
    SettingsDocument settings;
    bool settingsFollow = false;
    std::unique_ptr<DeviceLink> link;
};
