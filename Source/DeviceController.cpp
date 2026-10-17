#include "DeviceController.h"

#include "BleTransport.h"
#include "SettingsProtocol.h"
#include "SettingsWriter.h"
#include "VendorProtocol.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

namespace
{
bool chunkReady(const std::vector<Bytes>& messages, std::uint32_t address, std::uint32_t length)
{
    try
    {
        return SettingsProtocol::parseChunk(messages, address, length).size() == length;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool acknowledgementReady(const std::vector<Bytes>& messages)
{
    try
    {
        SettingsProtocol::parseWriteAcknowledgement(messages);
        return true;
    }
    catch (const std::exception& error)
    {
        return std::string(error.what()).find("No write acknowledgement") == std::string::npos;
    }
}
}

DeviceController::DeviceController()
    : juce::Thread("SMC Mixer Device Worker")
{
    startThread();
}

DeviceController::~DeviceController()
{
    shutdown();
}

bool DeviceController::requestRefresh(TransportKind transport)
{
    return enqueue({ RequestKind::refresh, transport, {} });
}

bool DeviceController::requestSettings(TransportKind transport)
{
    return enqueue({ RequestKind::settings, transport, {} });
}

bool DeviceController::requestWrite(TransportKind transport, std::vector<SettingsSpan> spans)
{
    return enqueue({ RequestKind::write, transport, std::move(spans) });
}

bool DeviceController::enqueue(Request request)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (quitting || stop.load() || active || !requests.empty())
            return false;
        requests.push_back(request);
    }
    condition.notify_all();
    return true;
}

bool DeviceController::isBusy() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return active || !requests.empty();
}

std::vector<DeviceEvent> DeviceController::pollEvents()
{
    std::lock_guard<std::mutex> lock(mutex);
    return std::exchange(events, {});
}

void DeviceController::shutdown()
{
    stop.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (quitting)
            return;
        quitting = true;
        requests.clear();
    }
    condition.notify_all();
    stopThread(30000);
}

void DeviceController::run()
{
    while (!threadShouldExit() && !stop.load())
    {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return quitting || stop.load() || !requests.empty(); });
            if (quitting || stop.load())
                return;
            request = requests.front();
            requests.pop_front();
            active = true;
        }

        try
        {
            if (request.kind == RequestKind::refresh)
                executeRefresh(request.transport);
            else if (request.kind == RequestKind::settings)
                executeSettings(request.transport);
            else
                executeWrite(request.transport, request.spans);
        }
        catch (const std::exception& error)
        {
            // A USB-C port that timed out may be stale after a replug or power cycle. Reopen it next time.
            if (request.transport == TransportKind::usbC)
                midi.close();
            if (!threadShouldExit() && !stop.load())
            {
                DeviceEvent event;
                event.kind = DeviceEvent::Kind::operationFailed;
                event.transport = request.transport;
                event.settingsOperation = request.kind != RequestKind::refresh;
                event.writeOperation = request.kind == RequestKind::write;
                event.message = error.what();
                emit(std::move(event));
            }
        }
        catch (...)
        {
            if (request.transport == TransportKind::usbC)
                midi.close();
            if (!threadShouldExit() && !stop.load())
            {
                DeviceEvent event;
                event.kind = DeviceEvent::Kind::operationFailed;
                event.transport = request.transport;
                event.settingsOperation = request.kind != RequestKind::refresh;
                event.writeOperation = request.kind == RequestKind::write;
                event.message = "Unexpected device communication error";
                emit(std::move(event));
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active = false;
        }
    }
}

void DeviceController::executeRefresh(TransportKind transport)
{
    DeviceSnapshot snapshot;
    snapshot.transport = transport;

    MidiEndpointDescriptor source;
    MidiEndpointDescriptor destination;
    if (CoreMidiTransport::findEndpoints(transport, source, destination))
    {
        snapshot.sourceName = source.fullName();
        snapshot.destinationName = destination.fullName();
    }
    else
    {
        snapshot.sourceName = "Not found";
        snapshot.destinationName = "Not found";
    }

    if (transport == TransportKind::usbC)
    {
        // Refresh always finds the ports again, so a power-cycled mixer gets fresh endpoints.
        midi.close();
        snapshot.rawSysEx = midi.query(transport, VendorProtocol::versionQuerySysEx(), std::chrono::milliseconds { 2000 }, &stop);
        snapshot.version = VendorProtocol::parseVersionResponse(snapshot.rawSysEx);
        snapshot.sourceName = midi.connectedSourceName();
        snapshot.destinationName = midi.connectedDestinationName();
    }
    else
    {
        const auto payload = BleTransport::queryVendor(VendorProtocol::versionQueryPayload(), 12.0, &stop);
        snapshot.version = VendorProtocol::parseVersionPayload(payload);
        try
        {
            snapshot.health = BleTransport::readHealth(20.0, &stop);
            snapshot.hasHealth = true;
        }
        catch (const std::exception&)
        {
            snapshot.warning = "Wireless health data is unavailable";
        }
    }

    DeviceEvent event;
    event.kind = DeviceEvent::Kind::snapshotReady;
    event.transport = transport;
    event.snapshot = std::move(snapshot);
    emit(std::move(event));
}

void DeviceController::executeSettings(TransportKind transport)
{
    constexpr std::uint32_t totalLength = 6282;
    constexpr std::uint32_t chunkSize = 1009;
    std::vector<Bytes> responses;

    for (std::uint32_t address = 0; address < totalLength; address += chunkSize)
    {
        if (threadShouldExit() || stop.load())
            return;

        DeviceEvent progressEvent;
        progressEvent.kind = DeviceEvent::Kind::settingsProgress;
        progressEvent.transport = transport;
        progressEvent.settingsOperation = true;
        progressEvent.progress = static_cast<double>(address) / totalLength;
        emit(std::move(progressEvent));

        const auto length = std::min(chunkSize, totalLength - address);
        if (transport == TransportKind::usbC)
        {
            auto part = midi.sendAndCapture(transport,
                                             SettingsProtocol::readSysEx(5, address, length),
                                             std::chrono::milliseconds { 2000 },
                                             &stop,
                                             [&](const std::vector<Bytes>& messages) {
                                                 return chunkReady(messages, address, length);
                                             });
            responses.insert(responses.end(),
                             std::make_move_iterator(part.begin()),
                             std::make_move_iterator(part.end()));
        }
        else
        {
            responses.push_back(BleTransport::queryVendor(SettingsProtocol::readPayload(5, address, length), 3.0, &stop));
        }
    }

    if (threadShouldExit() || stop.load())
        return;

    auto result = transport == TransportKind::usbC
        ? SettingsProtocol::parseResponses(responses)
        : SettingsProtocol::parsePayloads(responses);
    result.transport = transport;
    if (result.data.size() != totalLength)
        throw std::runtime_error("Settings snapshot should be 6282 bytes, received "
                                 + std::to_string(result.data.size()));

    DeviceEvent progressEvent;
    progressEvent.kind = DeviceEvent::Kind::settingsProgress;
    progressEvent.transport = transport;
    progressEvent.settingsOperation = true;
    progressEvent.progress = 1.0;
    emit(std::move(progressEvent));

    DeviceEvent event;
    event.kind = DeviceEvent::Kind::settingsReady;
    event.transport = transport;
    event.settingsOperation = true;
    event.settings = std::move(result);
    emit(std::move(event));
}

std::vector<Bytes> DeviceController::exchange(TransportKind transport,
                                                 const Bytes& payload,
                                                 std::chrono::milliseconds usbWindow,
                                                 double bluetoothTimeoutSeconds,
                                                 const std::function<bool(const std::vector<Bytes>&)>& ready)
{
    // Both links carry the same vendor payload. USB-C frames it as SysEx on the open
    // Private port and returns once the expected reply is in. Bluetooth sends it once on AE41.
    if (transport == TransportKind::usbC)
        return midi.sendAndCapture(transport, SettingsProtocol::sysEx(payload), usbWindow, &stop, ready);

    return { BleTransport::queryVendor(payload, bluetoothTimeoutSeconds, &stop) };
}

void DeviceController::executeWrite(TransportKind transport, const std::vector<SettingsSpan>& spans)
{
    const auto exchangeStep = [&](const SettingsWriter::Request& request) {
        if (request.step == SettingsWriter::Step::readBack)
        {
            const auto address = request.address;
            const auto length = request.length;
            return exchange(transport, request.payload, std::chrono::milliseconds { 1500 }, 12.0,
                            [address, length](const std::vector<Bytes>& replies) {
                                return chunkReady(replies, address, length);
                            });
        }
        // The save waits longer: the mixer answers it only after programming flash, about 60 ms.
        const auto window = request.step == SettingsWriter::Step::save ? std::chrono::milliseconds { 3000 }
                                                                      : std::chrono::milliseconds { 1000 };
        return exchange(transport, request.payload, window, 12.0, acknowledgementReady);
    };
    const auto report = [&](double progress) {
        DeviceEvent progressEvent;
        progressEvent.kind = DeviceEvent::Kind::settingsProgress;
        progressEvent.transport = transport;
        progressEvent.settingsOperation = true;
        progressEvent.writeOperation = true;
        progressEvent.progress = progress;
        emit(std::move(progressEvent));
    };

    auto written = SettingsWriter::write(spans, exchangeStep, report,
                                         [this] { return threadShouldExit() || stop.load(); });
    if (!written)
        return;

    DeviceEvent event;
    event.kind = DeviceEvent::Kind::settingsWritten;
    event.transport = transport;
    event.settingsOperation = true;
    event.writeOperation = true;
    event.written = std::move(*written);
    emit(std::move(event));
}

void DeviceController::emit(DeviceEvent event)
{
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back(std::move(event));
}
