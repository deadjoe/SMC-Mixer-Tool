#include "DeviceController.h"

#include "BleTransport.h"
#include "SettingsProtocol.h"
#include "VendorProtocol.h"

#include <algorithm>
#include <iterator>
#include <utility>

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
    return enqueue({ RequestKind::refresh, transport });
}

bool DeviceController::requestSettings(TransportKind transport)
{
    return enqueue({ RequestKind::settings, transport });
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
            else
                executeSettings(request.transport);
        }
        catch (const std::exception& error)
        {
            if (!threadShouldExit() && !stop.load())
            {
                DeviceEvent event;
                event.kind = DeviceEvent::Kind::operationFailed;
                event.transport = request.transport;
                event.settingsOperation = request.kind == RequestKind::settings;
                event.message = error.what();
                emit(std::move(event));
            }
        }
        catch (...)
        {
            if (!threadShouldExit() && !stop.load())
            {
                DeviceEvent event;
                event.kind = DeviceEvent::Kind::operationFailed;
                event.transport = request.transport;
                event.settingsOperation = request.kind == RequestKind::settings;
                event.message = "发生未知设备通讯错误";
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
        snapshot.sourceName = "未连接";
        snapshot.destinationName = "未连接";
    }

    if (transport == TransportKind::usbC)
    {
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
                                             &stop);
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
        throw std::runtime_error("设置快照长度应为 6282 字节，实际为 " + std::to_string(result.data.size()));

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

void DeviceController::emit(DeviceEvent event)
{
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back(std::move(event));
}
