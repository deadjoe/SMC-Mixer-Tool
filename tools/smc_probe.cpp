// Reads the mixer over USB-C or Bluetooth from the command line, for checking the transports
// on real hardware. It never writes: it reads the identity, then the settings image a few
// times, and reports how long each read took and whether the reads agree.
//
//   SMCMixerProbe usb [reads]
//   SMCMixerProbe bluetooth [reads]
//   SMCMixerProbe notify     checks that MIDI setup changes reach the transport, by adding
//                            and removing a virtual MIDI source of its own

#include "BleTransport.h"
#include "CoreMidiTransport.h"
#include "SettingsProtocol.h"
#include "SettingsReader.h"
#include "VendorProtocol.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
double secondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}

int checkNotifications()
{
    CoreMidiTransport midi;
    int changes = 0;
    midi.onSetupChanged = [&changes] { ++changes; };

    MIDIClientRef other = MIDIClientRef {};
    MIDIEndpointRef virtualSource = MIDIEndpointRef {};
    MIDIClientCreate(CFSTR("SMC Probe Other"), nullptr, nullptr, &other);
    MIDISourceCreate(other, CFSTR("SMC Probe Virtual Source"), &virtualSource);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    const auto afterAdd = changes;
    MIDIEndpointDispose(virtualSource);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    MIDIClientDispose(other);
    std::cout << "setup changes: " << afterAdd << " after adding a source, " << changes - afterAdd << " after removing it"
              << std::endl;
    return afterAdd > 0 && changes > afterAdd ? 0 : 1;
}

int main(int argc, char* argv[])
{
    const std::string link = argc > 1 ? argv[1] : "usb";
    if (link == "notify")
        return checkNotifications();
    const int reads = argc > 2 ? std::max(1, std::atoi(argv[2])) : 3;
    if (link != "usb" && link != "bluetooth")
    {
        std::cerr << "usage: SMCMixerProbe usb|bluetooth [reads]" << std::endl;
        return 2;
    }

    CoreMidiTransport midi;
    BleTransport bluetooth;
    const auto usb = link == "usb";

    try
    {
        auto start = std::chrono::steady_clock::now();
        VersionInfo version;
        if (usb)
            version = VendorProtocol::parseVersionResponse(
                midi.query(TransportKind::usbC, VendorProtocol::sysEx(VendorProtocol::versionQueryPayload())));
        else
            version = VendorProtocol::parseVersionPayload(bluetooth.queryVendor(VendorProtocol::versionQueryPayload(), 15.0));
        std::cout << "identity " << version.deviceName << " " << version.versionLabel << " in " << secondsSince(start) << " s";
        if (usb)
            std::cout << " on " << midi.connectedSourceName();
        std::cout << std::endl;

        if (!usb)
        {
            start = std::chrono::steady_clock::now();
            try
            {
                const auto health = bluetooth.readHealth(8.0);
                std::cout << "health battery " << (health.battery ? std::to_string(*health.battery) + "%" : "?") << ", signal "
                          << (health.rssi ? std::to_string(*health.rssi) + " dBm" : "?") << ", " << health.services.size()
                          << " services in " << secondsSince(start) << " s" << std::endl;
            }
            catch (const std::exception& error)
            {
                std::cout << "health unavailable: " << error.what() << std::endl;
            }
        }

        Bytes first;
        for (int read = 1; read <= reads; ++read)
        {
            int requests = 0;
            const auto exchange = [&](std::uint32_t address, std::uint32_t length) -> std::vector<Bytes> {
                ++requests;
                const auto payload = SettingsProtocol::readPayload(address, length);
                if (!usb)
                    return { bluetooth.queryVendor(payload, 4.0) };
                return midi.sendAndCapture(TransportKind::usbC, VendorProtocol::sysEx(payload), std::chrono::milliseconds { 2000 },
                                           nullptr, [&](const std::vector<Bytes>& replies) {
                                               try
                                               {
                                                   return SettingsProtocol::findChunk(replies, address, length).has_value();
                                               }
                                               catch (const ProtocolError&)
                                               {
                                                   return false;
                                               }
                                           });
            };
            start = std::chrono::steady_clock::now();
            const auto result = SettingsReader::read(exchange);
            std::cout << "read " << read << ": " << result->data.size() << " bytes, " << result->chunkCount << " chunks, "
                      << requests << " requests in " << secondsSince(start) << " s";
            if (read == 1)
                first = result->data;
            else
                std::cout << (result->data == first ? ", same as read 1" : ", DIFFERS from read 1");
            std::cout << std::endl;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "failed: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
