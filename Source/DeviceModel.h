#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

enum class TransportKind
{
    usbC,
    bluetooth
};

struct VersionInfo
{
    std::string deviceName;
    int versionNumber = 0;
    std::string versionLabel;
    Bytes versionDigitBytes;
    Bytes rawPayload;
};

struct BleHealthInfo
{
    std::optional<int> rssi;
    std::optional<int> battery;
    std::optional<std::string> manufacturer;
    std::optional<std::string> model;
    std::vector<std::string> services;
};

struct DeviceSnapshot
{
    TransportKind transport = TransportKind::usbC;
    VersionInfo version;
    BleHealthInfo health;
    bool hasHealth = false;
    std::string sourceName;
    std::string destinationName;
    Bytes rawSysEx;
    std::string warning;
};

struct SettingsReadResult
{
    Bytes data;
    int chunkCount = 0;
    int acknowledgementCount = 0;
    TransportKind transport = TransportKind::usbC;
};
