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

class CoreMidiTransport
{
public:
    CoreMidiTransport() = default;
    ~CoreMidiTransport();

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

    void open(TransportKind transport);
    void send(const Bytes& message);
    void receive(const MIDIPacketList* packets);
    void check(OSStatus status, const std::string& operation) const;

    MIDIClientRef client = MIDIClientRef {};
    MIDIPortRef inputPort = MIDIPortRef {};
    MIDIPortRef outputPort = MIDIPortRef {};
    MIDIEndpointRef source = MIDIEndpointRef {};
    MIDIEndpointRef destination = MIDIEndpointRef {};
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
