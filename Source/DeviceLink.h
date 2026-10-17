#pragma once

#include "DeviceModel.h"

#include <string>
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

// What the main window needs from the mixer: requests answered by events, and word when the
// connection changes. DeviceController talks to the hardware; tests supply their own.
class DeviceLink
{
public:
    virtual ~DeviceLink() = default;

    // Each returns false when a request is already running.
    virtual bool requestRefresh(TransportKind transport, bool thenReadSettings) = 0;
    virtual bool requestSettings(TransportKind transport) = 0;
    virtual bool requestWrite(TransportKind transport, std::vector<SettingsSpan> spans) = 0;
    virtual std::vector<DeviceEvent> pollEvents() = 0;

    // True once after a MIDI device appeared or disappeared, or the Bluetooth link dropped.
    virtual bool takeConnectionChange() = 0;
    virtual bool isUsbPresent() = 0;
    virtual bool isBluetoothConnected() = 0;
    virtual void shutdown() = 0;
};
