#pragma once

#include "CoreMidiTransport.h"
#include "DeviceModel.h"

#include <JuceHeader.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

struct DeviceEvent
{
    enum class Kind
    {
        snapshotReady,
        settingsProgress,
        settingsReady,
        settingsWritten,
        operationFailed
    };

    Kind kind = Kind::operationFailed;
    TransportKind transport = TransportKind::usbC;
    bool settingsOperation = false;
    bool writeOperation = false;
    DeviceSnapshot snapshot;
    SettingsReadResult settings;
    std::vector<SettingsSpan> written;
    double progress = 0.0;
    std::string message;
};

class DeviceController final : private juce::Thread
{
public:
    DeviceController();
    ~DeviceController() override;

    // Reads the identity, then, when asked, the settings image in the same request.
    bool requestRefresh(TransportKind transport, bool thenReadSettings = false);
    bool requestSettings(TransportKind transport);
    bool requestWrite(TransportKind transport, std::vector<SettingsSpan> spans);
    std::vector<DeviceEvent> pollEvents();
    void shutdown();

private:
    enum class RequestKind
    {
        refresh,
        refreshAndSettings,
        settings,
        write
    };

    struct Request
    {
        RequestKind kind = RequestKind::refresh;
        TransportKind transport = TransportKind::usbC;
        std::vector<SettingsSpan> spans;
    };

    void run() override;
    bool enqueue(Request request);
    void executeRefresh(TransportKind transport);
    void executeSettings(TransportKind transport);
    void executeWrite(TransportKind transport, const std::vector<SettingsSpan>& spans);
    std::vector<Bytes> exchange(TransportKind transport,
                                const Bytes& payload,
                                std::chrono::milliseconds usbWindow,
                                double bluetoothTimeoutSeconds,
                                const std::function<bool(const std::vector<Bytes>&)>& ready = {});
    void emit(DeviceEvent event);

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> stop { false };
    std::deque<Request> requests;
    std::vector<DeviceEvent> events;
    bool active = false;
    bool quitting = false;
    bool refreshSucceeded = false; // worker thread only
    CoreMidiTransport midi;
};
