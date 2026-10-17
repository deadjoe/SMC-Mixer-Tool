#include "CoreMidiTransport.h"

#include "MidiStream.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

std::string MidiEndpointDescriptor::fullName() const
{
    return midiname::join({ deviceName, entityName, endpointName });
}

CoreMidiTransport::~CoreMidiTransport()
{
    close();
}

MidiEndpointDescriptor CoreMidiTransport::descriptor(MIDIEndpointRef endpoint)
{
    MIDIEntityRef entity = MIDIEntityRef {};
    MIDIEndpointGetEntity(endpoint, &entity);
    MIDIDeviceRef device = MIDIDeviceRef {};
    if (entity != MIDIEntityRef {})
        MIDIEntityGetDevice(entity, &device);

    return {
        endpoint,
        objectName(endpoint),
        entity == MIDIEntityRef {} ? "(none)" : objectName(entity),
        device == MIDIDeviceRef {} ? "(none)" : objectName(device)
    };
}

std::string CoreMidiTransport::objectName(MIDIObjectRef object)
{
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(object, kMIDIPropertyName, &name) != noErr || name == nullptr)
        return "(unknown)";

    const auto maximumSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(name), kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<std::size_t>(maximumSize), '\0');
    if (!CFStringGetCString(name, result.data(), maximumSize, kCFStringEncodingUTF8))
        result = "(unknown)";
    else
        result.resize(std::strlen(result.c_str()));
    CFRelease(name);
    return result;
}

std::vector<MidiEndpointDescriptor> CoreMidiTransport::sources()
{
    std::vector<MidiEndpointDescriptor> result;
    const auto count = MIDIGetNumberOfSources();
    result.reserve(static_cast<std::size_t>(count));
    for (ItemCount index = 0; index < count; ++index)
        result.push_back(descriptor(MIDIGetSource(index)));
    return result;
}

std::vector<MidiEndpointDescriptor> CoreMidiTransport::destinations()
{
    std::vector<MidiEndpointDescriptor> result;
    const auto count = MIDIGetNumberOfDestinations();
    result.reserve(static_cast<std::size_t>(count));
    for (ItemCount index = 0; index < count; ++index)
        result.push_back(descriptor(MIDIGetDestination(index)));
    return result;
}

bool CoreMidiTransport::findEndpoints(TransportKind transport,
                                      MidiEndpointDescriptor& selectedSource,
                                      MidiEndpointDescriptor& selectedDestination)
{
    const auto* endpointKind = transport == TransportKind::usbC ? "Private" : "Bluetooth";
    const auto matches = [endpointKind](const MidiEndpointDescriptor& item) {
        const auto name = item.fullName();
        return midiname::containsIgnoringCase(name, "SMC-Mixer") && midiname::containsIgnoringCase(name, endpointKind);
    };

    const auto sourceList = sources();
    const auto destinationList = destinations();
    const auto sourceMatch = std::find_if(sourceList.begin(), sourceList.end(), matches);
    const auto destinationMatch = std::find_if(destinationList.begin(), destinationList.end(), matches);
    if (sourceMatch == sourceList.end() || destinationMatch == destinationList.end())
        return false;

    selectedSource = *sourceMatch;
    selectedDestination = *destinationMatch;
    return true;
}

Bytes CoreMidiTransport::query(TransportKind transport,
                              const Bytes& message,
                              std::chrono::milliseconds timeout,
                              const std::atomic<bool>* stop)
{
    open(transport);
    {
        std::lock_guard<std::mutex> lock(mutex);
        assembler.reset();
        completedResponse.reset();
        capturedResponses.clear();
        capturing = false;
    }
    send(message);

    std::unique_lock<std::mutex> lock(mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool received = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (stop != nullptr && stop->load())
            throw std::runtime_error("Cancelled");
        received = condition.wait_for(lock, std::chrono::milliseconds(200), [this] {
            return completedResponse.has_value();
        });
        if (received)
            break;
    }
    if (stop != nullptr && stop->load())
        throw std::runtime_error("Cancelled");
    if (!received)
        throw std::runtime_error("The mixer did not answer on this link. Check the connection and try again.");
    if (!completedResponse || completedResponse->empty())
        throw std::runtime_error("The mixer returned an empty reply");
    return *completedResponse;
}

std::vector<Bytes> CoreMidiTransport::sendAndCapture(TransportKind transport,
                                                     const Bytes& message,
                                                     std::chrono::milliseconds duration,
                                                     const std::atomic<bool>* stop,
                                                     const std::function<bool(const std::vector<Bytes>&)>& ready,
                                                     std::chrono::milliseconds quiet)
{
    open(transport);
    {
        std::lock_guard<std::mutex> lock(mutex);
        assembler.reset();
        completedResponse.reset();
        capturedResponses.clear();
        capturing = true;
    }

    try
    {
        send(message);
        std::unique_lock<std::mutex> lock(mutex);
        const auto deadline = std::chrono::steady_clock::now() + duration;
        std::size_t seen = 0;
        auto quietDeadline = std::chrono::steady_clock::time_point::max();
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (stop != nullptr && stop->load())
                throw std::runtime_error("Cancelled");

            const auto now = std::chrono::steady_clock::now();
            const auto enough = static_cast<bool>(ready) && ready(capturedResponses);
            if (enough)
            {
                if (quiet.count() <= 0)
                    break;
                if (capturedResponses.size() != seen
                    || quietDeadline == std::chrono::steady_clock::time_point::max())
                {
                    seen = capturedResponses.size();
                    quietDeadline = now + quiet;
                }
                if (now >= quietDeadline)
                    break;
                condition.wait_until(lock, std::min(deadline, quietDeadline));
                continue;
            }

            quietDeadline = std::chrono::steady_clock::time_point::max();
            seen = capturedResponses.size();
            condition.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(200)), [&] {
                return capturedResponses.size() != seen || (stop != nullptr && stop->load());
            });
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(mutex);
        capturing = false;
        throw;
    }

    std::lock_guard<std::mutex> lock(mutex);
    capturing = false;
    assembler.reset();
    return capturedResponses;
}

void CoreMidiTransport::open(TransportKind transport)
{
    if (isOpen && openTransport == transport)
        return;
    close();

    MidiEndpointDescriptor selectedSource;
    MidiEndpointDescriptor selectedDestination;
    if (!findEndpoints(transport, selectedSource, selectedDestination))
    {
        if (transport == TransportKind::usbC)
            throw std::runtime_error("SMC-Mixer USB-C port not found. Check the cable and power.");
        throw std::runtime_error("SMC-Mixer Bluetooth MIDI port not found. Connect it in Audio MIDI Setup.");
    }

    openTransport = transport;
    source = selectedSource.endpoint;
    destination = selectedDestination.endpoint;
    selectedSourceName = selectedSource.fullName();
    selectedDestinationName = selectedDestination.fullName();

    try
    {
        check(MIDIClientCreate(CFSTR("SMC Mixer Tool"), nullptr, nullptr, &client), "MIDIClientCreate");
        check(MIDIInputPortCreate(client, CFSTR("SMC Response Input"), midiReadCallback, this, &inputPort),
              "MIDIInputPortCreate");
        check(MIDIOutputPortCreate(client, CFSTR("SMC Query Output"), &outputPort), "MIDIOutputPortCreate");
        {
            std::lock_guard<std::mutex> lock(mutex);
            acceptingMidi = true;
            assembler.reset();
            completedResponse.reset();
            capturedResponses.clear();
            capturing = false;
        }
        check(MIDIPortConnectSource(inputPort, source, nullptr), "MIDIPortConnectSource");
        isOpen = true;
    }
    catch (...)
    {
        close();
        throw;
    }
}

void CoreMidiTransport::send(const Bytes& message)
{
    if (message.size() > 4096)
        throw std::runtime_error("MIDI message is longer than 4096 bytes");

    const auto bufferSize = sizeof(MIDIPacketList) + message.size() + 16;
    auto* packets = static_cast<MIDIPacketList*>(std::calloc(1, bufferSize));
    if (packets == nullptr)
        throw std::runtime_error("Could not allocate the MIDI send buffer");

    auto* packet = MIDIPacketListInit(packets);
    packet = MIDIPacketListAdd(packets, bufferSize, packet, 0, message.size(), message.data());
    if (packet == nullptr)
    {
        std::free(packets);
        throw std::runtime_error("MIDIPacketListAdd failed");
    }

    const auto status = MIDISend(outputPort, destination, packets);
    std::free(packets);
    check(status, "MIDISend");
}

void CoreMidiTransport::midiReadCallback(const MIDIPacketList* packets, void* readContext, void*)
{
    if (readContext != nullptr)
        static_cast<CoreMidiTransport*>(readContext)->receive(packets);
}

void CoreMidiTransport::receive(const MIDIPacketList* packets)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!acceptingMidi || packets == nullptr)
        return;

    std::vector<Bytes> completed;
    auto* packet = &packets->packet[0];
    for (UInt32 index = 0; index < packets->numPackets; ++index)
    {
        assembler.feed(packet->data, packet->length, completed);
        packet = MIDIPacketNext(packet);
    }
    if (completed.empty())
        return;

    completedResponse = completed.back();
    if (capturing)
        capturedResponses.insert(capturedResponses.end(), completed.begin(), completed.end());
    condition.notify_all();
}

void CoreMidiTransport::check(OSStatus status, const std::string& operation) const
{
    if (status != noErr)
        throw std::runtime_error(operation + " failed, OSStatus " + std::to_string(status));
}

void CoreMidiTransport::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        acceptingMidi = false;
    }

    if (inputPort != MIDIPortRef {} && source != MIDIEndpointRef {})
        MIDIPortDisconnectSource(inputPort, source);
    if (inputPort != MIDIPortRef {})
        MIDIPortDispose(inputPort);
    if (outputPort != MIDIPortRef {})
        MIDIPortDispose(outputPort);
    if (client != MIDIClientRef {})
        MIDIClientDispose(client);

    client = MIDIClientRef {};
    inputPort = MIDIPortRef {};
    outputPort = MIDIPortRef {};
    source = MIDIEndpointRef {};
    destination = MIDIEndpointRef {};
    isOpen = false;
    selectedSourceName.clear();
    selectedDestinationName.clear();
}
