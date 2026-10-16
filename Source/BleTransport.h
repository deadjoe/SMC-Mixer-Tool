#pragma once

#include "DeviceModel.h"

#include <atomic>

class BleTransport
{
public:
    static Bytes queryVendor(const Bytes& query,
                             double timeoutSeconds = 12.0,
                             const std::atomic<bool>* stop = nullptr);
    static BleHealthInfo readHealth(double timeoutSeconds = 20.0,
                                    const std::atomic<bool>* stop = nullptr);
};
