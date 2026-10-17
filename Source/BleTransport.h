#pragma once

#include "DeviceModel.h"

#include <atomic>
#include <functional>
#include <memory>

// The mixer's vendor channel over Bluetooth: GATT service AE40, writes to AE41, replies on
// AE42. The connection, the discovered characteristics, and the AE42 subscription are kept
// between requests, so a settings read connects once instead of once per chunk.
class BleTransport
{
public:
    BleTransport();
    ~BleTransport();

    BleTransport(const BleTransport&) = delete;
    BleTransport& operator=(const BleTransport&) = delete;

    // Sends one vendor payload and returns the reply, connecting first when needed.
    Bytes queryVendor(const Bytes& query, double timeoutSeconds, const std::atomic<bool>* stop = nullptr);
    BleHealthInfo readHealth(double timeoutSeconds, const std::atomic<bool>* stop = nullptr);
    bool isConnected() const;
    void disconnect();

    // Called on the Bluetooth queue when an established connection drops.
    std::function<void()> onDisconnected;

private:
    struct Session;
    std::unique_ptr<Session> session;
};
