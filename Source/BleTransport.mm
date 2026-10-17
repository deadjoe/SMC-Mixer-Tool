#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "BleTransport.h"

#include "MidiStream.h"
#include "VendorProtocol.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace
{
NSString* const vendorService = @"AE40";
NSString* const vendorWrite = @"AE41";
NSString* const vendorNotify = @"AE42";
NSString* const midiService = @"03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
NSString* const manufacturerName = @"2A29";
NSString* const modelNumber = @"2A24";
NSString* const batteryLevel = @"2A19";

std::string toStdString(NSString* value)
{
    return value == nil ? std::string() : std::string(value.UTF8String != nullptr ? value.UTF8String : "");
}

bool isMixer(CBPeripheral* peripheral)
{
    return peripheral.name != nil && midiname::isMixerName(toStdString(peripheral.name));
}

CBUUID* uuid(NSString* value)
{
    return [CBUUID UUIDWithString:value];
}

enum class Pending
{
    none,
    query,
    health
};
}

typedef void (^SMCQueryDone)(const Bytes& reply, NSString* error);
typedef void (^SMCHealthDone)(const BleHealthInfo& health, NSString* error);

// Lives on one serial queue. Every method and delegate callback runs there.
@interface SMCBleSession : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype)initWithQueue:(dispatch_queue_t)queue;
- (void)startQuery:(NSData*)payload done:(SMCQueryDone)done;
- (void)startHealth:(SMCHealthDone)done;
- (void)cancelPending;
- (void)shutdown;
@property (atomic, readonly) BOOL ready;
@property (atomic, copy) void (^onDisconnected)(void);
@end

@interface SMCBleSession ()
@property (atomic, readwrite) BOOL ready;
@end

@implementation SMCBleSession
{
    dispatch_queue_t delegateQueue;
    CBCentralManager* central;
    CBPeripheral* peripheral;
    CBCharacteristic* writeCharacteristic;
    CBCharacteristic* notifyCharacteristic;
    BOOL scanning;
    BOOL discovering;
    BOOL servicesReady;
    BOOL subscribing;
    BOOL notifying;
    int servicesToDescribe;

    Pending pending;
    NSData* payload;
    SMCQueryDone queryDone;
    SMCHealthDone healthDone;
    BOOL collecting;
    Bytes reply;
    NSMutableArray<NSData*>* writes;

    BleHealthInfo health;
    int pendingReads;
    BOOL rssiRead;
    BOOL healthReading;
}

- (instancetype)initWithQueue:(dispatch_queue_t)queue
{
    self = [super init];
    if (self != nil)
    {
        delegateQueue = queue;
        writes = [NSMutableArray array];
        pending = Pending::none;
    }
    return self;
}

- (void)startQuery:(NSData*)data done:(SMCQueryDone)done
{
    [self cancelPending];
    pending = Pending::query;
    payload = data;
    queryDone = done;
    [self advance];
}

- (void)startHealth:(SMCHealthDone)done
{
    [self cancelPending];
    pending = Pending::health;
    healthDone = done;
    health = {};
    healthReading = NO;
    [self advance];
}

- (void)cancelPending
{
    pending = Pending::none;
    queryDone = nil;
    healthDone = nil;
    payload = nil;
    collecting = NO;
    healthReading = NO;
    [writes removeAllObjects];
    if (scanning)
    {
        [central stopScan];
        scanning = NO;
    }
    // A connection still being made is abandoned, so it does not complete behind a later request.
    if (peripheral != nil && peripheral.state == CBPeripheralStateConnecting)
        [central cancelPeripheralConnection:peripheral];
}

- (void)shutdown
{
    [self cancelPending];
    if (peripheral != nil)
    {
        peripheral.delegate = nil;
        [central cancelPeripheralConnection:peripheral];
    }
    central.delegate = nil;
    [self forgetConnection];
    peripheral = nil;
    central = nil;
}

- (void)forgetConnection
{
    writeCharacteristic = nil;
    notifyCharacteristic = nil;
    discovering = NO;
    servicesReady = NO;
    subscribing = NO;
    notifying = NO;
    servicesToDescribe = 0;
    self.ready = NO;
}

- (void)fail:(NSString*)message
{
    const auto kind = pending;
    auto query = queryDone;
    auto healthBlock = healthDone;
    [self cancelPending];
    if (kind == Pending::query && query != nil)
        query({}, message);
    else if (kind == Pending::health && healthBlock != nil)
        healthBlock({}, message);
}

// Moves the pending request one step: Bluetooth on, mixer found, connected, services and
// characteristics discovered, AE42 subscribed, then the request itself.
- (void)advance
{
    if (pending == Pending::none)
        return;

    if (central == nil)
    {
        central = [[CBCentralManager alloc] initWithDelegate:self queue:delegateQueue options:nil];
        return;
    }
    if (central.state != CBManagerStatePoweredOn)
    {
        if (central.state != CBManagerStateUnknown && central.state != CBManagerStateResetting)
            [self fail:[NSString stringWithFormat:@"Bluetooth is unavailable (state %ld, authorization %ld). "
                                                  @"Check that Bluetooth is on and the app may use it.",
                                                  (long)central.state, (long)CBManager.authorization]];
        return;
    }

    if (peripheral == nil)
    {
        NSArray<CBPeripheral*>* connected = [central retrieveConnectedPeripheralsWithServices:@[ uuid(midiService), uuid(vendorService), uuid(@"AE00") ]];
        for (CBPeripheral* candidate in connected)
        {
            if (isMixer(candidate))
            {
                [self usePeripheral:candidate];
                break;
            }
        }
        if (peripheral == nil)
        {
            if (!scanning)
            {
                scanning = YES;
                [central scanForPeripheralsWithServices:nil options:nil];
            }
            return;
        }
    }

    switch (peripheral.state)
    {
        case CBPeripheralStateDisconnected:
            [central connectPeripheral:peripheral options:nil];
            return;
        case CBPeripheralStateConnecting:
        case CBPeripheralStateDisconnecting:
            return;
        case CBPeripheralStateConnected:
            break;
    }

    if (!servicesReady)
    {
        if (!discovering)
        {
            discovering = YES;
            [peripheral discoverServices:nil];
        }
        return;
    }

    if (pending == Pending::health)
    {
        [self readHealth];
        return;
    }

    if (writeCharacteristic == nil || notifyCharacteristic == nil)
    {
        [self fail:@"The mixer has no vendor service AE40 with characteristics AE41 and AE42"];
        return;
    }
    if (!notifying)
    {
        if (!subscribing)
        {
            subscribing = YES;
            [peripheral setNotifyValue:YES forCharacteristic:notifyCharacteristic];
        }
        return;
    }
    if (!collecting)
        [self sendQuery];
}

- (void)usePeripheral:(CBPeripheral*)target
{
    if (scanning)
    {
        [central stopScan];
        scanning = NO;
    }
    peripheral = target;
    peripheral.delegate = self;
    [self forgetConnection];
}

- (void)sendQuery
{
    if ((writeCharacteristic.properties & CBCharacteristicPropertyWriteWithoutResponse) == 0)
    {
        [self fail:@"AE41 does not accept writes without response"];
        return;
    }
    reply.clear();
    collecting = YES;
    const auto maximum = static_cast<std::size_t>([peripheral maximumWriteValueLengthForType:CBCharacteristicWriteWithoutResponse]);
    const auto* bytes = static_cast<const std::uint8_t*>(payload.bytes);
    for (const auto& piece : VendorProtocol::fragments(Bytes(bytes, bytes + payload.length), maximum))
        [writes addObject:[NSData dataWithBytes:piece.data() length:piece.size()]];
    [self pumpWrites];
}

// Writes without response go out as fast as the link accepts them, in order.
- (void)pumpWrites
{
    while (writes.count > 0 && peripheral.canSendWriteWithoutResponse)
    {
        NSData* next = writes.firstObject;
        [writes removeObjectAtIndex:0];
        [peripheral writeValue:next forCharacteristic:writeCharacteristic type:CBCharacteristicWriteWithoutResponse];
    }
}

- (void)readHealth
{
    if (healthReading)
        return;
    healthReading = YES;
    pendingReads = 0;
    rssiRead = NO;
    for (CBService* service in peripheral.services)
    {
        health.services.push_back(toStdString(service.UUID.UUIDString));
        for (CBCharacteristic* characteristic in service.characteristics)
        {
            NSString* identifier = characteristic.UUID.UUIDString;
            const auto wanted = [identifier isEqualToString:manufacturerName] || [identifier isEqualToString:modelNumber]
                             || [identifier isEqualToString:batteryLevel];
            if (wanted && (characteristic.properties & CBCharacteristicPropertyRead) != 0)
            {
                ++pendingReads;
                [peripheral readValueForCharacteristic:characteristic];
            }
        }
    }
    [peripheral readRSSI];
}

- (void)finishHealthIfDone
{
    if (pending != Pending::health || !healthReading || pendingReads > 0 || !rssiRead)
        return;
    auto done = healthDone;
    const auto result = health;
    [self cancelPending];
    if (done != nil)
        done(result, nil);
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)manager
{
    if (manager.state != CBManagerStatePoweredOn)
        [self forgetConnection];
    [self advance];
}

- (void)centralManager:(CBCentralManager*)manager
 didDiscoverPeripheral:(CBPeripheral*)discovered
     advertisementData:(NSDictionary<NSString*, id>*)advertisementData
                  RSSI:(NSNumber*)RSSI
{
    if (peripheral != nil || !isMixer(discovered))
        return;
    [self usePeripheral:discovered];
    [self advance];
}

- (void)centralManager:(CBCentralManager*)manager didConnectPeripheral:(CBPeripheral*)connected
{
    [self forgetConnection];
    [self advance];
}

- (void)centralManager:(CBCentralManager*)manager didFailToConnectPeripheral:(CBPeripheral*)failed error:(NSError*)error
{
    NSString* detail = error.localizedDescription != nil ? error.localizedDescription : @"Unknown error";
    [self fail:[NSString stringWithFormat:@"Bluetooth connection failed: %@", detail]];
}

- (void)centralManager:(CBCentralManager*)manager didDisconnectPeripheral:(CBPeripheral*)disconnected error:(NSError*)error
{
    const auto wasReady = self.ready;
    [self forgetConnection];
    if (pending != Pending::none)
    {
        NSString* detail = error.localizedDescription != nil ? error.localizedDescription : @"Connection closed";
        [self fail:[NSString stringWithFormat:@"Bluetooth disconnected: %@", detail]];
    }
    if (wasReady && self.onDisconnected != nil)
        self.onDisconnected();
}

- (void)peripheral:(CBPeripheral*)device didDiscoverServices:(NSError*)error
{
    if (error != nil)
    {
        discovering = NO;
        [self fail:[NSString stringWithFormat:@"GATT service discovery failed: %@", error.localizedDescription]];
        return;
    }
    servicesToDescribe = static_cast<int>(device.services.count);
    if (servicesToDescribe == 0)
    {
        discovering = NO;
        [self fail:@"The mixer did not expose any GATT services"];
        return;
    }
    for (CBService* service in device.services)
        [device discoverCharacteristics:nil forService:service];
}

- (void)peripheral:(CBPeripheral*)device didDiscoverCharacteristicsForService:(CBService*)service error:(NSError*)error
{
    if (error == nil && [service.UUID.UUIDString isEqualToString:vendorService])
    {
        for (CBCharacteristic* characteristic in service.characteristics)
        {
            if ([characteristic.UUID.UUIDString isEqualToString:vendorWrite])
                writeCharacteristic = characteristic;
            if ([characteristic.UUID.UUIDString isEqualToString:vendorNotify])
                notifyCharacteristic = characteristic;
        }
    }
    if (--servicesToDescribe > 0)
        return;
    discovering = NO;
    servicesReady = YES;
    self.ready = YES;
    [self advance];
}

- (void)peripheral:(CBPeripheral*)device didUpdateNotificationStateForCharacteristic:(CBCharacteristic*)characteristic error:(NSError*)error
{
    if (![characteristic.UUID.UUIDString isEqualToString:vendorNotify])
        return;
    subscribing = NO;
    if (error != nil)
    {
        [self fail:[NSString stringWithFormat:@"Could not subscribe to AE42: %@", error.localizedDescription]];
        return;
    }
    notifying = characteristic.isNotifying;
    [self advance];
}

- (void)peripheralIsReadyToSendWriteWithoutResponse:(CBPeripheral*)device
{
    [self pumpWrites];
}

- (void)peripheral:(CBPeripheral*)device didWriteValueForCharacteristic:(CBCharacteristic*)characteristic error:(NSError*)error
{
    if (error != nil && pending == Pending::query)
        [self fail:[NSString stringWithFormat:@"Bluetooth write failed: %@", error.localizedDescription]];
}

- (void)peripheral:(CBPeripheral*)device didReadRSSI:(NSNumber*)RSSI error:(NSError*)error
{
    if (pending != Pending::health)
        return;
    if (error == nil)
        health.rssi = RSSI.intValue;
    rssiRead = YES;
    [self finishHealthIfDone];
}

- (void)peripheral:(CBPeripheral*)device didUpdateValueForCharacteristic:(CBCharacteristic*)characteristic error:(NSError*)error
{
    NSString* identifier = characteristic.UUID.UUIDString;
    if ([identifier isEqualToString:vendorNotify])
    {
        // Only a reply to the request in flight is collected. A late notification for a
        // request that timed out arrives with nothing waiting and is dropped.
        if (pending != Pending::query || !collecting)
            return;
        if (error != nil)
        {
            [self fail:[NSString stringWithFormat:@"AE42 reply failed: %@", error.localizedDescription]];
            return;
        }
        NSData* value = characteristic.value;
        const auto* bytes = static_cast<const std::uint8_t*>(value.bytes);
        if (bytes != nullptr)
            reply.insert(reply.end(), bytes, bytes + value.length);
        if (reply.size() < 7)
            return;
        if (reply[0] != 0x00 || reply[1] != 0x59)
        {
            [self fail:@"Vendor reply header does not match"];
            return;
        }
        const auto length = static_cast<std::size_t>(reply[3]) | (static_cast<std::size_t>(reply[4]) << 8)
                          | (static_cast<std::size_t>(reply[5]) << 16);
        if (length > 8192)
        {
            [self fail:@"Vendor reply length is not valid"];
            return;
        }
        if (reply.size() < length + 7)
            return;
        reply.resize(length + 7);
        auto done = queryDone;
        const auto result = reply;
        [self cancelPending];
        if (done != nil)
            done(result, nil);
        return;
    }

    if (pending != Pending::health || !healthReading)
        return;
    const auto wanted = [identifier isEqualToString:manufacturerName] || [identifier isEqualToString:modelNumber]
                     || [identifier isEqualToString:batteryLevel];
    if (!wanted)
        return;
    NSData* value = characteristic.value;
    if (error == nil && value != nil)
    {
        if ([identifier isEqualToString:batteryLevel] && value.length > 0)
            health.battery = static_cast<const std::uint8_t*>(value.bytes)[0];
        else
        {
            NSString* text = [[NSString alloc] initWithData:value encoding:NSUTF8StringEncoding];
            if (text != nil && [identifier isEqualToString:manufacturerName])
                health.manufacturer = toStdString(text);
            if (text != nil && [identifier isEqualToString:modelNumber])
                health.model = toStdString(text);
        }
    }
    if (pendingReads > 0)
        --pendingReads;
    [self finishHealthIfDone];
}

@end

struct BleTransport::Session
{
    dispatch_queue_t queue = dispatch_queue_create("local.smc-mixer.gui.bluetooth", DISPATCH_QUEUE_SERIAL);
    SMCBleSession* object = [[SMCBleSession alloc] initWithQueue:queue];
};

namespace
{
template <typename Result>
struct Outcome
{
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::string failure;
    Result value {};
};

// Starts one request on the Bluetooth queue and waits for it, the deadline, or a stop.
template <typename Result, typename Start>
Result await(dispatch_queue_t queue, SMCBleSession* object, double timeoutSeconds, const std::atomic<bool>* stop,
             const char* timeoutMessage, Start start)
{
    auto outcome = std::make_shared<Outcome<Result>>();
    start(outcome);

    std::unique_lock<std::mutex> lock(outcome->mutex);
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(timeoutSeconds));
    while (!outcome->finished)
    {
        if (stop != nullptr && stop->load())
            break;
        if (outcome->condition.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(100)))
                == std::cv_status::timeout
            && std::chrono::steady_clock::now() >= deadline)
            break;
    }
    if (!outcome->finished)
    {
        lock.unlock();
        const auto connected = object.ready;
        dispatch_async(queue, ^{ [object cancelPending]; });
        if (stop != nullptr && stop->load())
            throw std::runtime_error("Cancelled");
        throw std::runtime_error(connected ? timeoutMessage
                                           : "The mixer did not connect over Bluetooth. Switch it on and check that it is in range");
    }
    if (!outcome->failure.empty())
        throw std::runtime_error(outcome->failure);
    return outcome->value;
}
}

BleTransport::BleTransport()
    : session(std::make_unique<Session>())
{
    session->object.onDisconnected = ^{
        if (onDisconnected)
            onDisconnected();
    };
}

BleTransport::~BleTransport()
{
    auto* object = session->object;
    object.onDisconnected = nil;
    dispatch_sync(session->queue, ^{ [object shutdown]; });
}

Bytes BleTransport::queryVendor(const Bytes& query, double timeoutSeconds, const std::atomic<bool>* stop)
{
    auto* object = session->object;
    NSData* data = [NSData dataWithBytes:query.data() length:query.size()];
    return await<Bytes>(session->queue, object, timeoutSeconds, stop, "Timed out waiting for the AE42 vendor reply",
                        [&](std::shared_ptr<Outcome<Bytes>> outcome) {
                            dispatch_async(session->queue, ^{
                                [object startQuery:data done:^(const Bytes& reply, NSString* error) {
                                    std::lock_guard<std::mutex> lock(outcome->mutex);
                                    outcome->value = reply;
                                    outcome->failure = toStdString(error);
                                    outcome->finished = true;
                                    outcome->condition.notify_all();
                                }];
                            });
                        });
}

BleHealthInfo BleTransport::readHealth(double timeoutSeconds, const std::atomic<bool>* stop)
{
    auto* object = session->object;
    return await<BleHealthInfo>(session->queue, object, timeoutSeconds, stop, "Bluetooth health data timed out",
                                [&](std::shared_ptr<Outcome<BleHealthInfo>> outcome) {
                                    dispatch_async(session->queue, ^{
                                        [object startHealth:^(const BleHealthInfo& health, NSString* error) {
                                            std::lock_guard<std::mutex> lock(outcome->mutex);
                                            outcome->value = health;
                                            outcome->failure = toStdString(error);
                                            outcome->finished = true;
                                            outcome->condition.notify_all();
                                        }];
                                    });
                                });
}

bool BleTransport::isConnected() const
{
    return session->object.ready;
}

void BleTransport::disconnect()
{
    auto* object = session->object;
    dispatch_sync(session->queue, ^{ [object shutdown]; });
}
