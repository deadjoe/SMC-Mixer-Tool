#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "BleTransport.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace
{
struct OperationState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::string failure;
    Bytes response;
    BleHealthInfo health;
};

std::string toStdString(NSString* value)
{
    NSData* data = value == nil ? nil : [value dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil || data.length == 0)
        return {};
    const auto* bytes = static_cast<const char*>(data.bytes);
    return std::string(bytes, data.length);
}

NSString* readableValue(NSData* data)
{
    return data == nil ? nil : [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

CBUUID* uuid(NSString* value)
{
    return [CBUUID UUIDWithString:value];
}
}

@interface SMCBleOperation : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype)initWithState:(OperationState*)state
                        query:(const Bytes*)query
                    healthMode:(BOOL)healthMode
                         queue:(dispatch_queue_t)queue;
- (void)start;
- (void)shutdown;
@end

@implementation SMCBleOperation
{
    OperationState* operationState;
    NSData* vendorQuery;
    BOOL healthMode;
    dispatch_queue_t delegateQueue;
    CBCentralManager* central;
    CBPeripheral* peripheral;
    int serviceCount;
    int describedServices;
    int pendingReads;
    bool rssiCompleted;
}

- (instancetype)initWithState:(OperationState*)state
                        query:(const Bytes*)query
                    healthMode:(BOOL)wantsHealth
                         queue:(dispatch_queue_t)queue
{
    self = [super init];
    if (self != nil)
    {
        operationState = state;
        healthMode = wantsHealth;
        delegateQueue = queue;
        serviceCount = 0;
        describedServices = 0;
        pendingReads = 0;
        rssiCompleted = false;
        if (query != nullptr)
            vendorQuery = [NSData dataWithBytes:query->data() length:query->size()];
    }
    return self;
}

- (void)start
{
    central = [[CBCentralManager alloc] initWithDelegate:self queue:delegateQueue options:nil];
}

- (void)shutdown
{
    dispatch_sync(delegateQueue, ^{
        central.delegate = nil;
        [central stopScan];
        peripheral.delegate = nil;
        if (peripheral != nil)
            [central cancelPeripheralConnection:peripheral];
        peripheral = nil;
        central = nil;
    });
}

- (void)finishWithError:(NSString*)message
{
    std::lock_guard<std::mutex> lock(operationState->mutex);
    if (operationState->finished)
        return;
    if (message != nil)
        operationState->failure = toStdString(message);
    operationState->finished = true;
    operationState->condition.notify_all();
}

- (BOOL)hasFinished
{
    std::lock_guard<std::mutex> lock(operationState->mutex);
    return operationState->finished;
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)manager
{
    if (manager.state != CBManagerStatePoweredOn)
    {
        if (manager.state != CBManagerStateUnknown)
        {
            NSString* message = [NSString stringWithFormat:@"Bluetooth is unavailable (state %ld, authorization %ld). Check the macOS Bluetooth permission.",
                                 (long)manager.state, (long)CBManager.authorization];
            [self finishWithError:message];
        }
        return;
    }

    NSArray<CBUUID*>* services = healthMode
        ? @[ uuid(@"03B80E5A-EDE8-4B33-A751-6CE34EC4C700"), uuid(@"AE00") ]
        : @[ uuid(@"AE40") ];
    NSArray<CBPeripheral*>* connected = [manager retrieveConnectedPeripheralsWithServices:services];
    NSUInteger index = [connected indexOfObjectPassingTest:^BOOL(CBPeripheral* item, NSUInteger, BOOL*) {
        return [item.name isEqualToString:@"SMC-Mixer"];
    }];
    if (index != NSNotFound)
        [self usePeripheral:connected[index]];
    else
        [manager scanForPeripheralsWithServices:nil options:nil];
}

- (void)centralManager:(CBCentralManager*)manager
 didDiscoverPeripheral:(CBPeripheral*)discovered
     advertisementData:(NSDictionary<NSString*, id>*)advertisementData
                  RSSI:(NSNumber*)RSSI
{
    if (peripheral != nil || ![discovered.name isEqualToString:@"SMC-Mixer"])
        return;
    if (healthMode)
        operationState->health.rssi = RSSI.intValue;
    [self usePeripheral:discovered];
}

- (void)usePeripheral:(CBPeripheral*)target
{
    peripheral = target;
    peripheral.delegate = self;
    [central stopScan];
    if (peripheral.state == CBPeripheralStateConnected)
    {
        if (healthMode)
        {
            [peripheral readRSSI];
            [peripheral discoverServices:nil];
        }
        else
        {
            [peripheral discoverServices:@[ uuid(@"AE40") ]];
        }
    }
    else
    {
        [central connectPeripheral:peripheral options:nil];
    }
}

- (void)centralManager:(CBCentralManager*)manager didConnectPeripheral:(CBPeripheral*)connectedPeripheral
{
    if (healthMode)
    {
        [connectedPeripheral readRSSI];
        [connectedPeripheral discoverServices:nil];
    }
    else
    {
        [connectedPeripheral discoverServices:@[ uuid(@"AE40") ]];
    }
}

- (void)centralManager:(CBCentralManager*)manager
 didFailToConnectPeripheral:(CBPeripheral*)failedPeripheral
                 error:(NSError*)error
{
    NSString* detail = error.localizedDescription != nil ? error.localizedDescription : @"Unknown error";
    [self finishWithError:[NSString stringWithFormat:@"Bluetooth connection failed: %@", detail]];
}

- (void)centralManager:(CBCentralManager*)manager
 didDisconnectPeripheral:(CBPeripheral*)disconnectedPeripheral
                 error:(NSError*)error
{
    if (![self hasFinished])
    {
        NSString* detail = error.localizedDescription != nil ? error.localizedDescription : @"Connection closed";
        [self finishWithError:[NSString stringWithFormat:@"Bluetooth disconnected before the query completed: %@", detail]];
    }
}

- (void)peripheral:(CBPeripheral*)device didReadRSSI:(NSNumber*)RSSI error:(NSError*)error
{
    if (!healthMode)
        return;
    if (error == nil)
        operationState->health.rssi = RSSI.intValue;
    rssiCompleted = true;
    [self completeHealthIfReady];
}

- (void)peripheral:(CBPeripheral*)device didDiscoverServices:(NSError*)error
{
    if (error != nil)
    {
        [self finishWithError:[NSString stringWithFormat:@"GATT service discovery failed: %@", error.localizedDescription]];
        return;
    }

    if (!healthMode)
    {
        CBService* service = nil;
        for (CBService* candidate in device.services)
        {
            if ([candidate.UUID.UUIDString isEqualToString:@"AE40"])
                service = candidate;
        }
        if (service == nil)
        {
            [self finishWithError:@"The mixer has no AE40 vendor service"];
            return;
        }
        [device discoverCharacteristics:@[ uuid(@"AE41"), uuid(@"AE42") ] forService:service];
        return;
    }

    NSArray<CBService*>* services = device.services != nil ? device.services : @[];
    serviceCount = static_cast<int>(services.count);
    operationState->health.services.clear();
    for (CBService* service in services)
    {
        operationState->health.services.push_back(toStdString(service.UUID.UUIDString));
        [device discoverCharacteristics:nil forService:service];
    }
    if (services.count == 0)
        [self finishWithError:@"The mixer did not expose any GATT services"];
}

- (void)peripheral:(CBPeripheral*)device
 didDiscoverCharacteristicsForService:(CBService*)service
             error:(NSError*)error
{
    if (error != nil)
    {
        [self finishWithError:[NSString stringWithFormat:@"GATT characteristic discovery failed: %@", error.localizedDescription]];
        return;
    }

    if (!healthMode)
    {
        for (CBCharacteristic* characteristic in service.characteristics)
        {
            if ([characteristic.UUID.UUIDString isEqualToString:@"AE42"])
            {
                [device setNotifyValue:YES forCharacteristic:characteristic];
                return;
            }
        }
        [self finishWithError:@"Vendor service AE40 has no AE42 notify characteristic"];
        return;
    }

    NSSet<NSString*>* readableIdentifiers = [NSSet setWithArray:@[ @"2A29", @"2A24", @"2A19" ]];
    for (CBCharacteristic* characteristic in service.characteristics)
    {
        if ([readableIdentifiers containsObject:characteristic.UUID.UUIDString]
            && (characteristic.properties & CBCharacteristicPropertyRead) != 0)
        {
            ++pendingReads;
            [device readValueForCharacteristic:characteristic];
        }
    }
    ++describedServices;
    [self completeHealthIfReady];
}

- (void)peripheral:(CBPeripheral*)device
 didUpdateNotificationStateForCharacteristic:(CBCharacteristic*)characteristic
             error:(NSError*)error
{
    if (healthMode)
        return;
    if (error != nil)
    {
        [self finishWithError:[NSString stringWithFormat:@"Could not subscribe to AE42: %@", error.localizedDescription]];
        return;
    }

    CBCharacteristic* writeCharacteristic = nil;
    for (CBCharacteristic* candidate in characteristic.service.characteristics)
    {
        if ([candidate.UUID.UUIDString isEqualToString:@"AE41"])
            writeCharacteristic = candidate;
    }
    if (writeCharacteristic == nil)
    {
        [self finishWithError:@"Vendor service AE40 has no AE41 write characteristic"];
        return;
    }
    if ((writeCharacteristic.properties & CBCharacteristicPropertyWriteWithoutResponse) == 0)
    {
        [self finishWithError:@"AE41 does not accept writes without response"];
        return;
    }

    const auto maximum = [device maximumWriteValueLengthForType:CBCharacteristicWriteWithoutResponse];
    if (vendorQuery.length > maximum)
    {
        [self finishWithError:[NSString stringWithFormat:@"Bluetooth write is %lu bytes, above the current limit of %lu",
                               (unsigned long)vendorQuery.length, (unsigned long)maximum]];
        return;
    }

    operationState->response.clear();
    [device writeValue:vendorQuery forCharacteristic:writeCharacteristic type:CBCharacteristicWriteWithoutResponse];
}

- (void)peripheral:(CBPeripheral*)device
 didWriteValueForCharacteristic:(CBCharacteristic*)characteristic
             error:(NSError*)error
{
    if (healthMode || error == nil || [self hasFinished])
        return;
    NSString* detail = error.localizedDescription != nil ? error.localizedDescription : @"Write failed";
    [self finishWithError:[NSString stringWithFormat:@"Bluetooth write failed: %@", detail]];
}

- (void)peripheral:(CBPeripheral*)device
 didUpdateValueForCharacteristic:(CBCharacteristic*)characteristic
             error:(NSError*)error
{
    NSString* identifier = characteristic.UUID.UUIDString;
    if (!healthMode)
    {
        if (![identifier isEqualToString:@"AE42"])
            return;
        if (error != nil)
        {
            [self finishWithError:[NSString stringWithFormat:@"AE42 reply failed: %@", error.localizedDescription]];
            return;
        }

        NSData* value = characteristic.value;
        if (value != nil && value.bytes != nil)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(value.bytes);
            operationState->response.insert(operationState->response.end(), bytes, bytes + value.length);
        }
        auto& response = operationState->response;
        if (response.size() < 7)
            return;
        if (response[0] != 0x00 || response[1] != 0x59)
        {
            [self finishWithError:@"Vendor reply header does not match"];
            return;
        }
        const int dataLength = static_cast<int>(response[3])
                             | (static_cast<int>(response[4]) << 8)
                             | (static_cast<int>(response[5]) << 16);
        if (dataLength > 8192)
        {
            [self finishWithError:@"Vendor reply length is not valid"];
            return;
        }
        const auto expectedLength = static_cast<std::size_t>(dataLength + 7);
        if (response.size() < expectedLength)
            return;
        response.resize(expectedLength);
        [self finishWithError:nil];
        return;
    }

    NSSet<NSString*>* readableIdentifiers = [NSSet setWithArray:@[ @"2A29", @"2A24", @"2A19" ]];
    if (![readableIdentifiers containsObject:identifier])
        return;

    NSData* value = characteristic.value;
    if (error == nil && value != nil)
    {
        if ([identifier isEqualToString:@"2A19"] && value.length > 0)
        {
            operationState->health.battery = static_cast<const std::uint8_t*>(value.bytes)[0];
        }
        else
        {
            NSString* text = readableValue(value);
            if (text != nil && [identifier isEqualToString:@"2A29"])
                operationState->health.manufacturer = toStdString(text);
            if (text != nil && [identifier isEqualToString:@"2A24"])
                operationState->health.model = toStdString(text);
        }
    }
    if (pendingReads > 0)
        --pendingReads;
    [self completeHealthIfReady];
}

- (void)completeHealthIfReady
{
    if (serviceCount > 0 && describedServices == serviceCount && pendingReads == 0 && rssiCompleted)
        [self finishWithError:nil];
}

@end

namespace
{
template <typename Result, typename Extract>
Result runOperation(const Bytes* query,
                    bool healthMode,
                    double timeoutSeconds,
                    const std::atomic<bool>* stop,
                    Extract extract)
{
    @autoreleasepool
    {
        OperationState state;
        const char* label = healthMode ? "local.smc-mixer.gui.ble-health" : "local.smc-mixer.gui.ble-query";
        dispatch_queue_t queue = dispatch_queue_create(label, DISPATCH_QUEUE_SERIAL);
        SMCBleOperation* operation = [[SMCBleOperation alloc] initWithState:&state
                                                                      query:query
                                                                  healthMode:healthMode
                                                                       queue:queue];
        [operation start];

        std::unique_lock<std::mutex> lock(state.mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
        bool completed = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (stop != nullptr && stop->load())
                break;
            completed = state.condition.wait_for(lock, std::chrono::milliseconds(200), [&state] {
                return state.finished;
            });
            if (completed)
                break;
        }
        if (!completed)
        {
            const auto cancelled = stop != nullptr && stop->load();
            state.failure = cancelled ? "Cancelled"
                                      : healthMode ? "Bluetooth health data timed out" : "Timed out waiting for the AE42 vendor reply";
            state.finished = true;
        }
        lock.unlock();

        [operation shutdown];
        if (!state.failure.empty())
            throw std::runtime_error(state.failure);
        return extract(state);
    }
}
}

Bytes BleTransport::queryVendor(const Bytes& query, double timeoutSeconds, const std::atomic<bool>* stop)
{
    return runOperation<Bytes>(&query, false, timeoutSeconds, stop, [](OperationState& state) {
        if (state.response.empty())
            throw std::runtime_error("AE42 returned an empty payload");
        return state.response;
    });
}

BleHealthInfo BleTransport::readHealth(double timeoutSeconds, const std::atomic<bool>* stop)
{
    return runOperation<BleHealthInfo>(nullptr, true, timeoutSeconds, stop, [](OperationState& state) {
        return state.health;
    });
}
