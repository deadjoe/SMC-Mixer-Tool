#pragma once

#include "DeviceModel.h"
#include "MidiStream.h"

#include <CoreMIDI/CoreMIDI.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct MidiEndpointDescriptor
{
    MIDIEndpointRef endpoint = MIDIEndpointRef {};
    std::string endpointName;
    std::string entityName;
    std::string deviceName;

    std::string fullName() const;
};

// One CoreMIDI client and one pair of ports for the life of the app. Opening a link connects
// the mixer's source to the input port; closing disconnects it. Apple advises against
// disposing of a process's last client, since the MIDI server may then refuse new ones.
//
// Construct it on the message thread: CoreMIDI delivers setup notifications on the run loop
// of the thread that created the client.
class CoreMidiTransport
{
public:
    CoreMidiTransport();
    ~CoreMidiTransport();

    // Called on the message thread when a MIDI device appears or disappears.
    std::function<void()> onSetupChanged;

    CoreMidiTransport(const CoreMidiTransport&) = delete;
    CoreMidiTransport& operator=(const CoreMidiTransport&) = delete;

    static std::vector<MidiEndpointDescriptor> sources();
    static std::vector<MidiEndpointDescriptor> destinations();
    static bool findEndpoints(TransportKind transport,
                              MidiEndpointDescriptor& source,
                              MidiEndpointDescriptor& destination);

    Bytes query(TransportKind transport,
                const Bytes& message,
                std::chrono::milliseconds timeout = std::chrono::milliseconds { 2000 },
                const std::atomic<bool>* stop = nullptr);
    std::vector<Bytes> sendAndCapture(TransportKind transport,
                                      const Bytes& message,
                                      std::chrono::milliseconds duration = std::chrono::milliseconds { 2000 },
                                      const std::atomic<bool>* stop = nullptr,
                                      const std::function<bool(const std::vector<Bytes>&)>& ready = {},
                                      std::chrono::milliseconds quiet = std::chrono::milliseconds { 50 });

    const std::string& connectedSourceName() const noexcept { return selectedSourceName; }
    const std::string& connectedDestinationName() const noexcept { return selectedDestinationName; }

    void close();

private:
    static MidiEndpointDescriptor descriptor(MIDIEndpointRef endpoint);
    static std::string objectName(MIDIObjectRef object);
    static void midiReadCallback(const MIDIPacketList* packets, void* readContext, void* sourceContext);
    static void midiNotifyCallback(const MIDINotification* message, void* context);

    void open(TransportKind transport);
    void send(const Bytes& message);
    void receive(const MIDIPacketList* packets);
    void check(OSStatus status, const std::string& operation) const;

    MIDIClientRef client = MIDIClientRef {};
    MIDIPortRef inputPort = MIDIPortRef {};
    MIDIPortRef outputPort = MIDIPortRef {};
    MIDIEndpointRef source = MIDIEndpointRef {};
    MIDIEndpointRef destination = MIDIEndpointRef {};
    std::string setupError;
    bool isOpen = false;
    bool acceptingMidi = false;
    TransportKind openTransport = TransportKind::usbC;
    std::string selectedSourceName;
    std::string selectedDestinationName;

    std::mutex mutex;
    std::condition_variable condition;
    SysExAssembler assembler;
    std::optional<Bytes> completedResponse;
    std::vector<Bytes> capturedResponses;
    bool capturing = false;
};
