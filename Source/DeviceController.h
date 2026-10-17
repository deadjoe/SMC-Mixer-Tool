#pragma once

#include "BleTransport.h"
#include "CoreMidiTransport.h"
#include "DeviceLink.h"

#include <JuceHeader.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

// Runs one request at a time on a worker thread, over USB-C or Bluetooth.
// Construct it on the message thread, so CoreMIDI reports setup changes there.
class DeviceController final : public DeviceLink,
                               private juce::Thread
{
public:
    DeviceController();
    ~DeviceController() override;

    // Reads the identity, then, when asked, the settings image in the same request.
    bool requestRefresh(TransportKind transport, bool thenReadSettings) override;
    bool requestSettings(TransportKind transport) override;
    bool requestWrite(TransportKind transport, std::vector<SettingsSpan> spans) override;
    std::vector<DeviceEvent> pollEvents() override;
    bool takeConnectionChange() override;
    bool isUsbPresent() override;
    bool isBluetoothConnected() override;
    void shutdown() override;

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
    std::atomic<bool> connectionChanged { false };
    std::deque<Request> requests;
    std::vector<DeviceEvent> events;
    bool active = false;
    bool quitting = false;
    bool refreshSucceeded = false; // worker thread only
    CoreMidiTransport midi;
    BleTransport bluetooth;
};
