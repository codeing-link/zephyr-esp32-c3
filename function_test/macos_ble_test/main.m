/* macOS 蓝牙与串口闭环测试程序；使用 Objective-C 以避免依赖额外的 Swift 包。 */
#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static CBUUID *ServiceUUID(void) { return [CBUUID UUIDWithString:@"FFF0"]; }
static CBUUID *WriteUUID(void) { return [CBUUID UUIDWithString:@"FFF3"]; }
static CBUUID *NotifyUUID(void) { return [CBUUID UUIDWithString:@"FFF4"]; }

static int serial_open(const char *path) {
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    struct termios options;
    if (fd < 0 || tcgetattr(fd, &options) != 0) return -1;
    cfmakeraw(&options);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CRTSCTS;
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    return tcsetattr(fd, TCSANOW, &options) == 0 ? fd : -1;
}

static NSData *serial_exchange(int fd, NSData *send, NSData *expected, NSTimeInterval timeout) {
    tcflush(fd, TCIFLUSH);
    write(fd, send.bytes, send.length);
    tcdrain(fd);
    NSMutableData *received = [NSMutableData data];
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:timeout];
    uint8_t byte;
    while ([end timeIntervalSinceNow] > 0) {
        if (read(fd, &byte, 1) == 1) {
            [received appendBytes:&byte length:1];
            if ([received rangeOfData:expected options:0 range:NSMakeRange(0, received.length)].location != NSNotFound) break;
        } else {
            usleep(10000);
        }
    }
    return received;
}

/* BLE 写入后仅等待串口接收，绝不能再次清空输入缓冲。 */
static NSData *serial_read_until(int fd, NSData *expected, NSTimeInterval timeout) {
    NSMutableData *received = [NSMutableData data];
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:timeout];
    uint8_t byte;
    while ([end timeIntervalSinceNow] > 0) {
        if (read(fd, &byte, 1) == 1) {
            [received appendBytes:&byte length:1];
            if ([received rangeOfData:expected options:0 range:NSMakeRange(0, received.length)].location != NSNotFound) break;
        } else {
            usleep(10000);
        }
    }
    return received;
}

/* 查询 MAC 等可变长度响应需要读满一个静默窗口，不能按固定前缀提前结束。 */
static NSData *serial_read_for(int fd, NSTimeInterval duration) {
    NSMutableData *received = [NSMutableData data];
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:duration];
    uint8_t byte;
    while ([end timeIntervalSinceNow] > 0) {
        if (read(fd, &byte, 1) == 1) {
            [received appendBytes:&byte length:1];
        } else {
            usleep(10000);
        }
    }
    return received;
}

static NSString *printable(NSData *data) {
    NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (text) return text;
    NSMutableString *hex = [NSMutableString string];
    const uint8_t *bytes = data.bytes;
    for (NSUInteger i = 0; i < data.length; i++) [hex appendFormat:@"%02X ", bytes[i]];
    return hex;
}

@interface BLESession : NSObject<CBCentralManagerDelegate, CBPeripheralDelegate>
@property CBCentralManager *central;
@property CBPeripheral *peripheral;
@property CBCharacteristic *writeCharacteristic;
@property CBCharacteristic *notifyCharacteristic;
@property NSMutableData *notification;
@property dispatch_semaphore_t stateSem, scanSem, connectSem, charsSem, notifySem, writeSem, dataSem;
@property NSError *error;
@end

@implementation BLESession
- (instancetype)init {
    if ((self = [super init])) {
        _stateSem = dispatch_semaphore_create(0); _scanSem = dispatch_semaphore_create(0);
        _connectSem = dispatch_semaphore_create(0); _charsSem = dispatch_semaphore_create(0);
        _notifySem = dispatch_semaphore_create(0); _writeSem = dispatch_semaphore_create(0);
        _dataSem = dispatch_semaphore_create(0); _notification = [NSMutableData data];
        _central = [[CBCentralManager alloc] initWithDelegate:self queue:nil];
    }
    return self;
}
- (BOOL)wait:(dispatch_semaphore_t)sem seconds:(NSInteger)seconds { return dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, seconds * NSEC_PER_SEC)) == 0; }
- (BOOL)connect:(NSString **)reason {
    if (![self wait:self.stateSem seconds:10] || self.central.state != CBManagerStatePoweredOn) { *reason = @"Mac 蓝牙未开启或无权限"; return NO; }
    [self.central scanForPeripheralsWithServices:@[ServiceUUID()] options:nil];
    if (![self wait:self.scanSem seconds:12]) { *reason = @"未扫描到 FFF0 服务"; return NO; }
    [self.central stopScan]; [self.central connectPeripheral:self.peripheral options:nil];
    if (![self wait:self.connectSem seconds:10]) { *reason = @"连接蓝牙设备超时"; return NO; }
    [self.peripheral discoverServices:@[ServiceUUID()]];
    if (![self wait:self.charsSem seconds:10] || !self.writeCharacteristic || !self.notifyCharacteristic) { *reason = @"未发现 FFF3/FFF4"; return NO; }
    [self.peripheral setNotifyValue:YES forCharacteristic:self.notifyCharacteristic];
    if (![self wait:self.notifySem seconds:5]) { *reason = @"订阅 FFF4 通知超时"; return NO; }
    return YES;
}
- (BOOL)write:(NSData *)data reason:(NSString **)reason {
    NSUInteger maximumLength = [self.peripheral maximumWriteValueLengthForType:CBCharacteristicWriteWithResponse];
    if (data.length > maximumLength) {
        *reason = [NSString stringWithFormat:@"单次有响应写入上限为 %lu 字节", (unsigned long)maximumLength];
        return NO;
    }
    [self.peripheral writeValue:data forCharacteristic:self.writeCharacteristic type:CBCharacteristicWriteWithResponse];
    if (![self wait:self.writeSem seconds:5] || self.error) { *reason = self.error.localizedDescription ?: @"写 FFF3 超时"; return NO; }
    return YES;
}
- (NSUInteger)maximumWriteLength { return [self.peripheral maximumWriteValueLengthForType:CBCharacteristicWriteWithResponse]; }
- (NSData *)waitNotification:(NSInteger)seconds {
    self.notification = [NSMutableData data];
    [self wait:self.dataSem seconds:seconds];
    return self.notification;
}
- (void)centralManagerDidUpdateState:(CBCentralManager *)central { dispatch_semaphore_signal(self.stateSem); }
- (void)centralManager:(CBCentralManager *)central didDiscoverPeripheral:(CBPeripheral *)peripheral advertisementData:(NSDictionary<NSString *,id> *)data RSSI:(NSNumber *)RSSI { self.peripheral = peripheral; peripheral.delegate = self; dispatch_semaphore_signal(self.scanSem); }
- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral { dispatch_semaphore_signal(self.connectSem); }
- (void)centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error { self.error = error; dispatch_semaphore_signal(self.connectSem); }
- (void)peripheral:(CBPeripheral *)p didDiscoverServices:(NSError *)error { CBService *service = p.services.firstObject; if (service) [p discoverCharacteristics:@[WriteUUID(), NotifyUUID()] forService:service]; else dispatch_semaphore_signal(self.charsSem); }
- (void)peripheral:(CBPeripheral *)p didDiscoverCharacteristicsForService:(CBService *)service error:(NSError *)error {
    for (CBCharacteristic *characteristic in service.characteristics) {
        if ([characteristic.UUID isEqual:WriteUUID()]) self.writeCharacteristic = characteristic;
        if ([characteristic.UUID isEqual:NotifyUUID()]) self.notifyCharacteristic = characteristic;
    }
    dispatch_semaphore_signal(self.charsSem);
}
- (void)peripheral:(CBPeripheral *)p didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)c error:(NSError *)error {
    self.error = error;
    dispatch_semaphore_signal(self.notifySem);
}
- (void)peripheral:(CBPeripheral *)p didWriteValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)error {
    self.error = error;
    dispatch_semaphore_signal(self.writeSem);
}
- (void)peripheral:(CBPeripheral *)p didUpdateValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)error {
    if (c.value) [self.notification appendData:c.value];
    dispatch_semaphore_signal(self.dataSem);
}
@end

static void add_result(NSMutableArray *lines, NSString *name, BOOL pass, NSString *detail) {
    [lines addObject:[NSString stringWithFormat:@"## %@\n- 结果：%@\n- 详情：`%@`\n", name, pass ? @"通过" : @"失败", detail]];
}

/* 将有破坏性的人工项目单独标为未执行，避免被误统计为自动化失败。 */
static void add_skipped_result(NSMutableArray *lines, NSString *name, NSString *detail) {
    [lines addObject:[NSString stringWithFormat:@"## %@\n- 结果：未执行\n- 详情：`%@`\n", name, detail]];
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 3 || strcmp(argv[1], "--serial") != 0) {
            fprintf(stderr, "用法：./macos_ble_test --serial /dev/cu.usbserial-xxxx\n");
            return 2;
        }
        int fd = serial_open(argv[2]);
        if (fd < 0) {
            fprintf(stderr, "无法打开串口 %s\n", argv[2]);
            return 2;
        }
        NSMutableArray *lines = [NSMutableArray arrayWithObject:@"# macOS 蓝牙与串口闭环测试记录\n"];

        /* 给刚复位或刚插入的模块留出 UART 接收中断初始化时间。 */
        usleep(500000);
        NSArray *commands = @[@[@"TTM:BPS-115200", @"TTM:OK"], @[@"TTM:VID-?", @"TTM:VID-LSD_BLE_5.0"], @[@"TTM:REV-?", @"TTM:REV-V2.1"], @[@"TTM:CDL-5ms", @"TTM:OK"], @[@"TTM:TPL-+4", @"TTM:OK"], @[@"TTM:PID-1234", @"TTM:OK"], @[@"TTM:REN-MAC-BLE-TEST", @"TTM:OK"], @[@"TTM:REN-?", @"TTM:REN-MAC-BLE-TEST"], @[@"TTM:MAC-?", @"TTM:MAC-"], @[@"TTM:ADP-10", @"TTM:OK"], @[@"TTM:ADD-MACBLE", @"TTM:OK"], @[@"TTM:ADD-?", @"TTM:ADD-MACBLE"], @[@"TTM:UNKNOWN", @"TTM:ERP"]];
        __block NSInteger failed = 0;
        for (NSArray *item in commands) {
            NSString *command = item[0];
            NSString *expected = item[1];
            NSData *response = serial_exchange(fd, [[command stringByAppendingString:@"\r\n"] dataUsingEncoding:NSUTF8StringEncoding], [expected dataUsingEncoding:NSUTF8StringEncoding], 2);
            BOOL pass = [response rangeOfData:[expected dataUsingEncoding:NSUTF8StringEncoding] options:0 range:NSMakeRange(0, response.length)].location != NSNotFound;
            /* BPS 是首条命令时，模块可能仍在完成 UART 初始化，允许仅重试一次。 */
            if (!pass && [command isEqualToString:@"TTM:BPS-115200"]) {
                usleep(200000);
                response = serial_exchange(fd, [[command stringByAppendingString:@"\r\n"] dataUsingEncoding:NSUTF8StringEncoding], [expected dataUsingEncoding:NSUTF8StringEncoding], 2);
                pass = [response rangeOfData:[expected dataUsingEncoding:NSUTF8StringEncoding] options:0 range:NSMakeRange(0, response.length)].location != NSNotFound;
            }
            /* 不能只收到 MAC 查询命令自身的回显，必须同时收到 12 位地址。 */
            if ([command isEqualToString:@"TTM:MAC-?"] && response.length < 30) {
                NSData *remaining = serial_read_for(fd, 0.4);
                NSMutableData *complete = [response mutableCopy];
                [complete appendData:remaining];
                response = complete;
                pass = response.length >= 30;
            }
            add_result(lines, command, pass, printable(response));
            if (!pass) failed++;
        }
        BLESession *session = [BLESession new];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSString *reason = nil;
            if (![session connect:&reason]) {
                add_result(lines, @"BLE 扫描、连接和订阅", NO, reason); failed++;
            } else {
                add_result(lines, @"BLE 扫描、连接 FFF0 并订阅 FFF4", YES, @"已建立链路");
                NSData *citResponse = serial_exchange(fd, [@"TTM:CIT-100ms\r\n" dataUsingEncoding:NSUTF8StringEncoding], [@"TTM:OK" dataUsingEncoding:NSUTF8StringEncoding], 2);
                BOOL citPass = [citResponse rangeOfData:[@"TTM:OK" dataUsingEncoding:NSUTF8StringEncoding] options:0 range:NSMakeRange(0, citResponse.length)].location != NSNotFound;
                add_result(lines, @"TTM:CIT-100ms（连接状态）", citPass, printable(citResponse));
                if (!citPass) failed++;
                NSData *bleToUart = [@"BLE_TO_UART_20_BYTES" dataUsingEncoding:NSUTF8StringEncoding];
                tcflush(fd, TCIFLUSH);
                BOOL writeOK = [session write:bleToUart reason:&reason];
                NSData *uartBack = serial_read_until(fd, bleToUart, 3);
                BOOL pass = writeOK && [uartBack rangeOfData:bleToUart options:0 range:NSMakeRange(0, uartBack.length)].location != NSNotFound;
                add_result(lines, @"BLE FFF3 → UART0（20 字节）", pass, printable(uartBack));
                if (!pass) failed++;
                NSData *uartToBle = [@"UART_TO_BLE_20_BYTES" dataUsingEncoding:NSUTF8StringEncoding]; NSMutableData *uartLine = [uartToBle mutableCopy]; [uartLine appendData:[@"\r\n" dataUsingEncoding:NSUTF8StringEncoding]]; [session waitNotification:0]; write(fd, uartLine.bytes, uartLine.length); NSData *notification = [session waitNotification:3]; pass = [notification rangeOfData:uartToBle options:0 range:NSMakeRange(0, notification.length)].location != NSNotFound; add_result(lines, @"UART0 → BLE FFF4（20 字节）", pass, printable(notification)); if (!pass) failed++;
                NSData *longData = [NSMutableData dataWithLength:150];
                memset(((NSMutableData *)longData).mutableBytes, 0x5A, 150);
                tcflush(fd, TCIFLUSH);
                writeOK = [session write:longData reason:&reason];
                uartBack = serial_read_until(fd, longData, 5);
                pass = writeOK && [uartBack rangeOfData:longData options:0 range:NSMakeRange(0, uartBack.length)].location != NSNotFound;
                NSString *longDetail = [NSString stringWithFormat:@"Mac 单次写入上限 %lu 字节；接收 %lu 字节%@", (unsigned long)[session maximumWriteLength], (unsigned long)uartBack.length, writeOK ? @"" : [NSString stringWithFormat:@"；写入失败：%@", reason]];
                add_result(lines, @"BLE FFF3 → UART0（150 字节）", pass, longDetail);
                if (!pass) failed++;
                [session.central cancelPeripheralConnection:session.peripheral];
            }
            add_skipped_result(lines, @"TTM:RST-SYSTEMRESET", @"未自动执行：该命令会断开串口和蓝牙");
            add_skipped_result(lines, @"TTM:MAC-xxxxxxxxxxxx", @"未自动执行：该命令会永久修改 MAC");
            NSString *report = [lines componentsJoinedByString:@"\n"]; [report writeToFile:@"macos_ble_test_record.md" atomically:YES encoding:NSUTF8StringEncoding error:nil]; printf("测试完成：失败 %ld 项；记录：macos_ble_test_record.md\n", (long)failed); close(fd); exit(failed == 0 ? 0 : 1);
        });
        dispatch_main();
    }
}
