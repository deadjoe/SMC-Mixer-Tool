#pragma once

#include "CoreMidiTransport.h"
#include "DeviceModel.h"

#include <JuceHeader.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

struct DeviceEvent
{
    enum class Kind
    {
        snapshotReady,
        settingsProgress,
        settingsReady,
        operationFailed
    };

    Kind kind = Kind::operationFailed;
    TransportKind transport = TransportKind::usbC;
    bool settingsOperation = false;
    DeviceSnapshot snapshot;
    SettingsReadResult settings;
    double progress = 0.0;
    std::string message;
};

class DeviceController final : private juce::Thread
{
public:
    DeviceController();
    ~DeviceController() override;

    bool requestRefresh(TransportKind transport);
    bool requestSettings(TransportKind transport);
    bool isBusy() const;
    std::vector<DeviceEvent> pollEvents();
    void shutdown();

private:
    enum class RequestKind
    {
        refresh,
        settings
    };

    struct Request
    {
        RequestKind kind = RequestKind::refresh;
        TransportKind transport = TransportKind::usbC;
    };

    void run() override;
    bool enqueue(Request request);
    void executeRefresh(TransportKind transport);
    void executeSettings(TransportKind transport);
    void emit(DeviceEvent event);

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> stop { false };
    std::deque<Request> requests;
    std::vector<DeviceEvent> events;
    bool active = false;
    bool quitting = false;
    CoreMidiTransport midi;
};
