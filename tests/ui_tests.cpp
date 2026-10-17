// Builds the app's components without a window and checks what the person would see and do.
// Nothing here opens a MIDI port or a Bluetooth connection: the main component is created
// without connecting, and device events are delivered by hand.
//
//   SMCMixerUITests                 runs the checks
//   SMCMixerUITests --render <dir>  also saves a PNG of each state

#include "MainComponent.h"

#include <JuceHeader.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL " << message << std::endl;
}

std::string text(juce::Rectangle<int> r)
{
    return r.toString().toStdString();
}

// A CC / User profile like a person might set up, and a Mackie-style DAW profile.
Bytes demoImage()
{
    Bytes image(layout::snapshotSize);
    for (std::size_t profile = 0; profile < layout::profileCount; ++profile)
    {
        const auto base = profile * layout::profileSize;
        const auto user = profile == 1;
        for (std::size_t index = 0; index < layout::buttonCount; ++index)
        {
            auto* record = image.data() + base + index * layout::buttonSize;
            record[0] = user ? static_cast<std::uint8_t>(index % 3) : 5;
            record[1] = 0;
            record[2] = static_cast<std::uint8_t>(user ? 20 + index : index);
            record[3] = 0;
            record[4] = 127;
            record[layout::buttonSize - 1] = 1;
        }
        for (std::size_t index = 0; index < layout::knobCount; ++index)
        {
            auto* record = image.data() + base + layout::knobsOffset + index * layout::knobSize;
            const auto relative = !user || index == 1;
            record[0] = relative ? knobTypeRelative : 0;
            record[1] = 2;
            record[2] = 0;
            record[3] = static_cast<std::uint8_t>(user ? (index == 1 ? 102 : 16 + index) : 16 + index);
            record[4] = relative ? 65 : static_cast<std::uint8_t>(index * 6);
            record[5] = relative ? 1 : static_cast<std::uint8_t>(127 - index * 4);
        }
        for (std::size_t index = 0; index < layout::sliderCount; ++index)
        {
            auto* record = image.data() + base + layout::slidersOffset + index * layout::sliderSize;
            record[0] = user ? 0 : 2;
            record[1] = static_cast<std::uint8_t>(user ? 0 : index);
            record[2] = static_cast<std::uint8_t>(user ? 7 : 0);
            record[3] = 0;
            record[4] = 127;
        }
    }
    // Knob 1 sends CC 16 between 10 and 100.
    image[layout::profileSize + layout::knobsOffset + 4] = 10;
    image[layout::profileSize + layout::knobsOffset + 5] = 100;
    return image;
}

DeviceSnapshot usbSnapshot()
{
    DeviceSnapshot snapshot;
    snapshot.transport = TransportKind::usbC;
    snapshot.version.deviceName = "SMC-Mixer";
    snapshot.version.versionNumber = 13;
    snapshot.version.versionLabel = "V013";
    snapshot.sourceName = "SMC-Mixer -Private";
    snapshot.destinationName = "SMC-Mixer -Private";
    return snapshot;
}

DeviceSnapshot bluetoothSnapshot()
{
    auto snapshot = usbSnapshot();
    snapshot.transport = TransportKind::bluetooth;
    snapshot.hasHealth = true;
    snapshot.health.battery = 82;
    snapshot.health.rssi = -58;
    snapshot.health.manufacturer = "M-VAVE";
    snapshot.health.model = "SMC-Mixer";
    return snapshot;
}

void connect(MainComponent& main, const DeviceSnapshot& snapshot = usbSnapshot(), const Bytes& image = demoImage())
{
    DeviceEvent identity;
    identity.kind = DeviceEvent::Kind::snapshotReady;
    identity.transport = snapshot.transport;
    identity.snapshot = snapshot;
    main.deliver(identity);

    DeviceEvent settings;
    settings.kind = DeviceEvent::Kind::settingsReady;
    settings.transport = snapshot.transport;
    settings.settingsOperation = true;
    settings.settings.data = image;
    settings.settings.chunkCount = 7;
    settings.settings.transport = snapshot.transport;
    main.deliver(settings);
}

std::unique_ptr<MainComponent> makeMain(int width = 1360, int height = 900)
{
    auto main = std::make_unique<MainComponent>(MainComponent::Options { false });
    main->setSize(width, height);
    main->setVisible(true);
    return main;
}

std::uint8_t userByte(const MainComponent& main, std::size_t offset)
{
    return main.document().workingBytes()[layout::profileSize + offset];
}

// Visible children must stay inside their parent and must not overlap one another.
void expectTidy(juce::Component& parent, const std::string& name, bool checkOverlap = true)
{
    std::vector<juce::Component*> shown;
    for (auto* child : parent.getChildren())
        if (child->isVisible() && !child->getBounds().isEmpty() && dynamic_cast<juce::TooltipWindow*>(child) == nullptr
            && dynamic_cast<Toast*>(child) == nullptr)
            shown.push_back(child);

    for (auto* child : shown)
        expect(parent.getLocalBounds().contains(child->getBounds()),
               name + ": " + child->getName().toStdString() + " " + text(child->getBounds()) + " leaves "
                   + text(parent.getLocalBounds()));
    if (!checkOverlap)
        return;
    for (std::size_t a = 0; a < shown.size(); ++a)
        for (std::size_t b = a + 1; b < shown.size(); ++b)
            expect(!shown[a]->getBounds().intersects(shown[b]->getBounds()),
                   name + ": " + text(shown[a]->getBounds()) + " overlaps " + text(shown[b]->getBounds()));
}

void testLayout()
{
    const std::pair<int, int> sizes[] { { MainComponent::minimumWidth, MainComponent::minimumHeight }, { 1360, 900 }, { 1920, 1200 } };
    for (const auto& [width, height] : sizes)
    {
        const auto label = std::to_string(width) + "x" + std::to_string(height);
        auto main = makeMain(width, height);
        connect(*main);
        main->selectControl({ ControlKind::trackButton, 0 });

        expectTidy(main->header(), label + " header");
        expectTidy(main->overview(), label + " overview", false);
        expectTidy(main->overview().inspector(), label + " inspector");

        auto& diagram = main->overview().diagram();
        expectTidy(diagram, label + " panel");
        for (const auto& control : allControls())
        {
            const auto bounds = diagram.viewFor(control).getBounds();
            if (control.kind == ControlKind::globalButton)
            {
                expect(diagram.globalArea().contains(bounds), label + " transport key in its row");
                expect(bounds.getHeight() >= 34, label + " transport key height " + std::to_string(bounds.getHeight()));
                continue;
            }
            const auto channel = control.kind == ControlKind::trackButton ? control.index / 4 : control.index;
            expect(diagram.channelFrame(channel).contains(bounds), label + " control inside its channel " + text(bounds));
            expect(bounds.getBottom() <= diagram.globalArea().getY(), label + " strip clear of the transport row");
            if (control.kind == ControlKind::trackButton)
                expect(bounds.getHeight() >= 24 && bounds.getWidth() >= 30, label + " key size " + text(bounds));
            if (control.kind == ControlKind::slider)
                expect(bounds.getHeight() >= 110, label + " fader travel " + text(bounds));
            if (control.kind == ControlKind::knob)
                expect(bounds.getWidth() >= 40, label + " knob size " + text(bounds));
        }

        // The knob sits over the middle of its fader and keys.
        for (int channel = 0; channel < static_cast<int>(layout::channelCount); ++channel)
        {
            const auto knob = diagram.viewFor({ ControlKind::knob, channel }).getBounds();
            const auto fader = diagram.viewFor({ ControlKind::slider, channel }).getBounds();
            const auto key = diagram.viewFor({ ControlKind::trackButton, channel * 4 }).getBounds();
            const auto pairCentre = (fader.getX() + key.getRight()) / 2;
            expect(std::abs(knob.getCentreX() - pairCentre) <= 1, label + " knob centred over its strip");
            const auto frame = diagram.channelFrame(channel);
            expect(std::abs((fader.getX() - frame.getX()) - (frame.getRight() - key.getRight())) <= 1,
                   label + " strip centred in its channel");
        }
    }
}

bool isInk(const juce::Image& image, juce::Point<float> point)
{
    const auto x = juce::roundToInt(point.x);
    const auto y = juce::roundToInt(point.y);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (image.getPixelAt(x + dx, y + dy).getPerceivedBrightness() < 0.3f)
                return true;
    return false;
}

void testSelectionRing()
{
    // The ring is drawn over the controls, so it shows on all four sides of a key in a stack.
    auto main = makeMain(MainComponent::minimumWidth, MainComponent::minimumHeight);
    connect(*main);
    auto& diagram = main->overview().diagram();
    for (const auto control : { ControlSelection { ControlKind::trackButton, 1 }, ControlSelection { ControlKind::trackButton, 30 },
                                ControlSelection { ControlKind::globalButton, 5 }, ControlSelection { ControlKind::slider, 3 } })
    {
        main->selectControl(control);
        const auto image = diagram.createComponentSnapshot(diagram.getLocalBounds(), true, 1.0f);
        const auto ring = diagram.viewFor(control).markArea().toFloat().expanded(3.5f);
        const auto name = controlDisplayName(control);
        expect(isInk(image, { ring.getCentreX(), ring.getY() }), name + " ring top");
        expect(isInk(image, { ring.getCentreX(), ring.getBottom() }), name + " ring bottom");
        expect(isInk(image, { ring.getX(), ring.getCentreY() }), name + " ring left");
        expect(isInk(image, { ring.getRight(), ring.getCentreY() }), name + " ring right");
    }
}

void testMinimumAndMaximum()
{
    auto main = makeMain();
    connect(*main);
    main->selectControl({ ControlKind::knob, 0 });
    auto& inspector = main->overview().inspector();
    auto* minimum = inspector.numberFieldFor(EditableField::minimum);
    auto* maximum = inspector.numberFieldFor(EditableField::maximum);
    expect(minimum != nullptr && maximum != nullptr, "knob shows min and max");
    if (minimum == nullptr || maximum == nullptr)
        return;
    expect(minimum->getMaximum() == 100, "min cannot be set above max");

    // Dragging Min past Max stops at Max, and the field is never rebuilt under the pointer.
    for (int value = 60; value <= 140; value += 20)
        minimum->setValueFromUser(value, value > 60);
    expect(inspector.numberFieldFor(EditableField::minimum) == minimum, "the same min field stays in place");
    expect(minimum->isVisible() && minimum->getValue() == 100, "min stops at max");
    expect(userByte(*main, layout::knobsOffset + 4) == 100, "document min is 100");
    expect(maximum->getMinimum() == 100, "max cannot go below the new min");
    expect(!main->toast().isVisible(), "no refusal is shown for a clamped drag");

    // The drag was one gesture, so one undo restores the value before it.
    main->commands().invokeDirectly(MainComponent::undoCommand, false);
    expect(userByte(*main, layout::knobsOffset + 4) == 10, "one undo restores the min before the drag");
    expect(inspector.numberFieldFor(EditableField::minimum)->getValue() == 10, "field shows the restored min");
    main->commands().invokeDirectly(MainComponent::redoCommand, false);
    expect(userByte(*main, layout::knobsOffset + 4) == 100, "redo reapplies the drag");
}

void testLeavingRelative()
{
    auto main = makeMain();
    connect(*main);
    main->selectControl({ ControlKind::knob, 1 });
    auto& inspector = main->overview().inspector();
    expect(inspector.numberFieldFor(EditableField::minimum)->getValue() == 65, "relative knob shows CCW 65");
    inspector.choiceBox().setSelectedId(1, juce::sendNotificationSync);
    const auto knob = layout::knobsOffset + layout::knobSize;
    expect(userByte(*main, knob) == 0, "knob is CC Abs");
    expect(userByte(*main, knob + 4) == 1 && userByte(*main, knob + 5) == 65, "min and max swapped into a range");
    expect(main->toast().getMessage().contains("swapped"), "the swap is explained");
    auto* minimum = inspector.numberFieldFor(EditableField::minimum);
    expect(minimum != nullptr && minimum->getMaximum() == 65, "min can move up to max");
    minimum->setValueFromUser(30);
    expect(userByte(*main, knob + 4) == 30, "min is editable after leaving CC Rel");
}

void testStatusTooltip()
{
    auto main = makeMain();
    DeviceEvent failure;
    failure.kind = DeviceEvent::Kind::operationFailed;
    failure.message = "SMC-Mixer USB-C port not found. Check the cable and power.";
    main->deliver(failure);
    const auto failed = main->header().getStatus();
    expect(failed.tone == mixerui::Tone::error && failed.tooltip.contains("port not found"), "failure explained");

    connect(*main);
    const auto online = main->header().getStatus();
    expect(online.tone == mixerui::Tone::success, "online after connecting");
    expect(!online.tooltip.contains("port not found"), "the old error tooltip is gone");
    expect(online.detail.contains("USB-C") && online.detail.contains("Private"), "online facts");

    auto bluetooth = makeMain();
    connect(*bluetooth, bluetoothSnapshot());
    expect(bluetooth->header().getStatus().detail.contains("Battery 82%"), "bluetooth battery shown");
    expect(bluetooth->header().getStatus().tooltip.isEmpty(), "bluetooth status has no stale tooltip");
}

void testQuit()
{
    auto main = makeMain();
    connect(*main);
    int asked = 0;
    bool answer = false;
    main->confirm = [&](const MainComponent::Confirmation&, std::function<void(bool)> reply) {
        ++asked;
        reply(answer);
    };

    int quits = 0;
    main->requestQuit([&] { ++quits; });
    expect(asked == 0 && quits == 1, "a clean document quits at once");

    main->selectControl({ ControlKind::knob, 0 });
    main->overview().inspector().numberFieldFor(EditableField::cc)->setValueFromUser(40);
    expect(main->hasUnsavedEdits(), "edit made");
    main->requestQuit([&] { ++quits; });
    expect(asked == 1 && quits == 1, "unsaved edits ask before quitting, and Keep editing stays");
    answer = true;
    main->requestQuit([&] { ++quits; });
    expect(asked == 2 && quits == 2, "Discard edits quits");
}

void testWriteButton()
{
    auto main = makeMain();
    connect(*main);
    auto& write = main->overview().writeButton();
    expect(!write.isEnabled() && write.getButtonText() == "Write", "nothing to write");
    main->selectControl({ ControlKind::knob, 0 });
    main->overview().inspector().numberFieldFor(EditableField::cc)->setValueFromUser(40);
    main->selectControl({ ControlKind::globalButton, 0 });
    main->overview().inspector().numberFieldFor(EditableField::value2)->setValueFromUser(64);
    expect(write.isEnabled() && write.getButtonText() == "Write 2 changes", "write counts the changed records");
    expect(MixerLookAndFeel::isPrimary(write), "write is the primary action");

    juce::ApplicationCommandInfo info(MainComponent::writeCommand);
    main->getCommandInfo(MainComponent::writeCommand, info);
    expect((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0, "Write Changes is enabled in the menu");
    expect(main->commands().getKeyMappings()->containsMapping(MainComponent::writeCommand,
                                                               juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0)),
           "Cmd+S writes");

    main->commands().invokeDirectly(MainComponent::revertAllCommand, false);
    expect(!main->hasUnsavedEdits() && !write.isEnabled(), "revert all clears every edit");
    main->commands().invokeDirectly(MainComponent::undoCommand, false);
    expect(main->hasUnsavedEdits(), "revert all can be undone");
}

void testProfilesAndPanel()
{
    auto main = makeMain();
    connect(*main);
    auto& diagram = main->overview().diagram();
    expect(diagram.viewFor({ ControlKind::knob, 0 }).getSummary()->primary == "16", "panel shows knob CC");
    expect(diagram.viewFor({ ControlKind::knob, 1 }).getSummary()->relative, "panel marks the relative knob");
    expect(diagram.viewFor({ ControlKind::slider, 0 }).getSummary()->primary == "CC 7", "panel shows fader CC");

    main->selectControl({ ControlKind::knob, 2 });
    main->overview().inspector().numberFieldFor(EditableField::cc)->setValueFromUser(74);
    main->selectControl({ ControlKind::trackButton, 4 });
    main->commands().invokeDirectly(MainComponent::showDawCommand, false);
    expect(diagram.getDirtyControls().empty(), "no edit dots in the DAW profile");
    auto* channel = main->overview().inspector().numberFieldFor(EditableField::channel);
    expect(channel != nullptr && !channel->isEditable(), "DAW profile is read-only");
    expect(diagram.viewFor({ ControlKind::slider, 0 }).getSummary()->primary == "PB", "panel follows the DAW profile");
    main->commands().invokeDirectly(MainComponent::showCcUserCommand, false);
    expect(main->overview().inspector().numberFieldFor(EditableField::channel)->isEditable(), "CC / User is editable");
    expect(diagram.getDirtyControls() == std::vector<ControlSelection>({ { ControlKind::knob, 2 } }), "edit dot in CC / User");

    // Arrow keys move the selection around the panel.
    main->selectControl({ ControlKind::knob, 0 });
    diagram.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    expect(diagram.getSelection() == ControlSelection { ControlKind::knob, 1 }, "right moves to the next channel");
    diagram.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    diagram.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    expect(diagram.getSelection() == ControlSelection { ControlKind::trackButton, 4 }, "down walks the strip");

    // Typed text outside the field's range is refused with a message.
    main->selectControl({ ControlKind::knob, 0 });
    expect(main->overview().inspector().numberFieldFor(EditableField::cc)->getMaximum() == 127, "cc range");
    expect(main->overview().inspector().numberFieldFor(EditableField::channel)->getMaximum() == 15, "channel range");
}

void testLongNote()
{
    // A Custom key with the longest stored message: 64 bytes, 191 characters of hex.
    auto image = demoImage();
    auto* record = image.data() + layout::profileSize;
    record[0] = buttonModeCustom;
    record[5] = static_cast<std::uint8_t>(layout::buttonMessageCapacity);
    for (std::size_t index = 0; index < layout::buttonMessageCapacity; ++index)
        record[6 + index] = static_cast<std::uint8_t>(index == 0 ? 0xf0 : index + 1 == layout::buttonMessageCapacity ? 0xf7 : 0x7f);

    auto main = makeMain(MainComponent::minimumWidth, MainComponent::minimumHeight);
    connect(*main, usbSnapshot(), image);
    main->selectControl({ ControlKind::trackButton, 0 });
    auto& note = main->overview().inspector().noteLabel();
    expect(note.getText().startsWith("Each press sends F0 7F") && note.getText().endsWith("7F F7."), "custom message note");
    expect(note.getTooltip() == note.getText(), "the note's tooltip holds the whole sentence");

    const auto lineHeight = note.getFont().getHeight();
    const auto width = static_cast<float>(note.getWidth() - note.getBorderSize().getLeftAndRight());
    const auto textWidth = juce::GlyphArrangement::getStringWidth(note.getFont(), note.getText());
    expect(static_cast<float>(note.getHeight()) >= lineHeight * 2.0f, "the note has room for two lines");
    expect(textWidth <= width * 2.0f * 0.92f, "the longest note fits in two lines at the smallest window: "
                                                + std::to_string(textWidth) + " in 2 x " + std::to_string(width));
}

void testContrast()
{
    const auto ratio = [](juce::Colour a, juce::Colour b) {
        const auto luminance = [](juce::Colour colour) {
            const auto channel = [](float value) {
                return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * channel(colour.getFloatRed()) + 0.7152f * channel(colour.getFloatGreen())
                 + 0.0722f * channel(colour.getFloatBlue());
        };
        const auto first = luminance(a);
        const auto second = luminance(b);
        return (juce::jmax(first, second) + 0.05f) / (juce::jmin(first, second) + 0.05f);
    };
    using namespace mixerui;
    for (const auto foreground : { ink, inkSoft, muted, success, warning, error })
        for (const auto background : { window, surface, well, field })
            expect(ratio(foreground, background) >= 4.5f, "text contrast " + foreground.toString().toStdString()
                                                              + " on " + background.toString().toStdString());
    for (const auto background : { trackButton, globalButton, fader, ink })
        expect(ratio(onDark, background) >= 4.5f, "key text contrast on " + background.toString().toStdString());
    expect(ratio(edited, well) >= 3.0f, "edit dot stands out from the panel");
}

void render(const juce::File& folder)
{
    folder.createDirectory();
    const auto save = [&folder](juce::Component& component, const juce::String& name) {
        const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream stream(folder.getChildFile(name + ".png"));
        stream.setPosition(0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream(image, stream);
        std::cout << "rendered " << folder.getChildFile(name + ".png").getFullPathName() << std::endl;
    };

    {
        auto main = makeMain();
        connect(*main);
        main->selectControl({ ControlKind::knob, 2 });
        main->overview().inspector().numberFieldFor(EditableField::cc)->setValueFromUser(74);
        main->selectControl({ ControlKind::knob, 1 });
        save(*main, "main-usb");
        main->selectControl({ ControlKind::trackButton, 5 });
        save(*main, "main-key");
        main->selectControl({ ControlKind::globalButton, 0 });
        main->commands().invokeDirectly(MainComponent::showDawCommand, false);
        save(*main, "main-daw");
        main->commands().invokeDirectly(MainComponent::toggleDiagnosticsCommand, false);
        save(*main, "diagnostics");
    }
    {
        auto main = makeMain(MainComponent::minimumWidth, MainComponent::minimumHeight);
        connect(*main);
        main->selectControl({ ControlKind::slider, 0 });
        save(*main, "main-minimum");
    }
    {
        auto main = makeMain(1920, 1200);
        connect(*main, bluetoothSnapshot());
        save(*main, "main-bluetooth-large");
    }
    {
        auto main = makeMain();
        DeviceEvent failure;
        failure.kind = DeviceEvent::Kind::operationFailed;
        failure.message = "SMC-Mixer USB-C port not found. Check the cable and power.";
        main->deliver(failure);
        save(*main, "main-disconnected");
    }
}
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    testLayout();
    testSelectionRing();
    testMinimumAndMaximum();
    testLeavingRelative();
    testStatusTooltip();
    testQuit();
    testWriteButton();
    testProfilesAndPanel();
    testLongNote();
    testContrast();

    for (int index = 1; index + 1 < argc; ++index)
        if (juce::String(argv[index]) == "--render")
            render(juce::File::getCurrentWorkingDirectory().getChildFile(argv[index + 1]));

    if (failures != 0)
    {
        std::cerr << failures << " failed" << std::endl;
        return 1;
    }
    std::cout << "ui-tests-ok" << std::endl;
    return 0;
}
