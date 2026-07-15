/*
 * macOS 蓝牙与串口闭环测试程序。
 * 使用 Mac mini 内置蓝牙测试 ESP32-C3 的 FFF0/FFF3/FFF4 透传服务。
 */

import CoreBluetooth
import Darwin
import Foundation

let serviceUUID = CBUUID(string: "FFF0")
let bleDataUUID = CBUUID(string: "FFF3")
let uartDataUUID = CBUUID(string: "FFF4")

enum TestError: Error, CustomStringConvertible {
    case message(String)

    var description: String {
        switch self {
        case .message(let text):
            return text
        }
    }
}

final class SerialPort {
    private let descriptor: Int32

    init(path: String, baudrate: Int = 115200) throws {
        descriptor = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard descriptor >= 0 else {
            throw TestError.message("无法打开串口 \(path)：\(String(cString: strerror(errno)))")
        }

        var options = termios()
        guard tcgetattr(descriptor, &options) == 0 else {
            close(descriptor)
            throw TestError.message("读取串口配置失败")
        }
        cfmakeraw(&options)
        options.c_cflag |= tcflag_t(CLOCAL | CREAD)
        options.c_cflag &= ~tcflag_t(CRTSCTS)
        guard let speed = Self.speed(for: baudrate) else {
            close(descriptor)
            throw TestError.message("不支持的串口波特率 \(baudrate)")
        }
        cfsetispeed(&options, speed)
        cfsetospeed(&options, speed)
        guard tcsetattr(descriptor, TCSANOW, &options) == 0 else {
            close(descriptor)
            throw TestError.message("设置串口配置失败")
        }
    }

    deinit {
        close(descriptor)
    }

    func write(_ data: Data) throws {
        let result = data.withUnsafeBytes { buffer in
            Darwin.write(descriptor, buffer.baseAddress, buffer.count)
        }
        guard result == data.count else {
            throw TestError.message("串口发送失败：\(String(cString: strerror(errno)))")
        }
        tcdrain(descriptor)
    }

    func discardInput() {
        tcflush(descriptor, TCIFLUSH)
    }

    func readUntil(expected: Data, timeout: TimeInterval) -> Data {
        let deadline = Date().addingTimeInterval(timeout)
        var received = Data()
        var byte: UInt8 = 0

        while Date() < deadline {
            let count = Darwin.read(descriptor, &byte, 1)
            if count == 1 {
                received.append(byte)
                if received.range(of: expected) != nil {
                    break
                }
            } else {
                usleep(10_000)
            }
        }
        return received
    }

    private static func speed(for baudrate: Int) -> speed_t? {
        switch baudrate {
        case 2400: return speed_t(B2400)
        case 4800: return speed_t(B4800)
        case 9600: return speed_t(B9600)
        case 14400: return speed_t(B14400)
        case 19200: return speed_t(B19200)
        case 28800: return speed_t(B28800)
        case 38400: return speed_t(B38400)
        case 57600: return speed_t(B57600)
        case 76800: return speed_t(B76800)
        case 115200: return speed_t(B115200)
        default: return nil
        }
    }
}

struct TestResult {
    let name: String
    let passed: Bool
    let detail: String
}

final class BLESession: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var writeCharacteristic: CBCharacteristic?
    private var notifyCharacteristic: CBCharacteristic?
    private var discoveredPeripheral: CBPeripheral?
    private var receivedNotification = Data()
    private let stateReady = DispatchSemaphore(value: 0)
    private let scanReady = DispatchSemaphore(value: 0)
    private let connectReady = DispatchSemaphore(value: 0)
    private let serviceReady = DispatchSemaphore(value: 0)
    private let notifyReady = DispatchSemaphore(value: 0)
    private let writeReady = DispatchSemaphore(value: 0)
    private let notificationReady = DispatchSemaphore(value: 0)
    private var lastError: Error?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func connect(timeout: TimeInterval) throws {
        guard stateReady.wait(timeout: .now() + timeout) == .success else {
            throw TestError.message("等待 Mac 蓝牙进入可用状态超时")
        }
        guard central.state == .poweredOn else {
            throw TestError.message("Mac 蓝牙不可用，状态为 \(central.state.rawValue)")
        }

        central.scanForPeripherals(withServices: [serviceUUID], options: nil)
        guard scanReady.wait(timeout: .now() + timeout) == .success,
              let target = discoveredPeripheral else {
            central.stopScan()
            throw TestError.message("未扫描到服务 UUID 为 FFF0 的设备")
        }
        central.stopScan()
        peripheral = target
        target.delegate = self
        central.connect(target, options: nil)
        guard connectReady.wait(timeout: .now() + timeout) == .success else {
            throw lastError ?? TestError.message("连接蓝牙设备超时")
        }

        target.discoverServices([serviceUUID])
        guard serviceReady.wait(timeout: .now() + timeout) == .success,
              writeCharacteristic != nil, notifyCharacteristic != nil else {
            throw lastError ?? TestError.message("未找到 FFF3 或 FFF4 特征")
        }
        target.setNotifyValue(true, for: notifyCharacteristic!)
        guard notifyReady.wait(timeout: .now() + timeout) == .success else {
            throw lastError ?? TestError.message("订阅 FFF4 通知超时")
        }
    }

    func writeToUART(_ data: Data, timeout: TimeInterval) throws {
        guard let peripheral, let writeCharacteristic else {
            throw TestError.message("蓝牙链路尚未就绪")
        }
        peripheral.writeValue(data, for: writeCharacteristic, type: .withResponse)
        guard writeReady.wait(timeout: .now() + timeout) == .success else {
            throw lastError ?? TestError.message("写 FFF3 超时")
        }
    }

    func waitNotification(expected: Data, timeout: TimeInterval) -> Data {
        receivedNotification = Data()
        _ = notificationReady.wait(timeout: .now() + timeout)
        return receivedNotification
    }

    func disconnect() {
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        stateReady.signal()
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        discoveredPeripheral = peripheral
        scanReady.signal()
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectReady.signal()
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        lastError = error
        connectReady.signal()
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            lastError = error
            serviceReady.signal()
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            serviceReady.signal()
            return
        }
        peripheral.discoverCharacteristics([bleDataUUID, uartDataUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error {
            lastError = error
        } else {
            writeCharacteristic = service.characteristics?.first(where: { $0.uuid == bleDataUUID })
            notifyCharacteristic = service.characteristics?.first(where: { $0.uuid == uartDataUUID })
        }
        serviceReady.signal()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        lastError = error
        notifyReady.signal()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        lastError = error
        writeReady.signal()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            lastError = error
        } else if let value = characteristic.value {
            receivedNotification.append(value)
        }
        notificationReady.signal()
    }
}

func printable(_ data: Data) -> String {
    String(data: data, encoding: .utf8) ?? data.map { String(format: "%02X", $0) }.joined(separator: " ")
}

func atTest(_ serial: SerialPort, command: String, expected: String) -> TestResult {
    do {
        serial.discardInput()
        try serial.write(Data((command + "\r\n").utf8))
        let response = serial.readUntil(expected: Data(expected.utf8), timeout: 1.5)
        return TestResult(name: command, passed: response.range(of: Data(expected.utf8)) != nil,
                          detail: printable(response))
    } catch {
        return TestResult(name: command, passed: false, detail: error.localizedDescription)
    }
}

func parseArguments() throws -> String {
    let arguments = CommandLine.arguments
    guard let index = arguments.firstIndex(of: "--serial"), index + 1 < arguments.count else {
        throw TestError.message("用法：./macos_ble_test --serial /dev/cu.usbserial-xxxx")
    }
    return arguments[index + 1]
}

func writeReport(_ results: [TestResult]) {
    var lines = ["# macOS 蓝牙与串口闭环测试记录", "", "- 生成时间：\(ISO8601DateFormatter().string(from: Date()))", ""]
    for result in results {
        lines.append("## \(result.name)")
        lines.append("- 结果：\(result.passed ? "通过" : "失败")")
        lines.append("- 详情：`\(result.detail)`")
        lines.append("")
    }
    let path = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        .appendingPathComponent("macos_ble_test_record.md")
    try? lines.joined(separator: "\n").write(to: path, atomically: true, encoding: .utf8)
}

do {
    let serialPath = try parseArguments()
    let serial = try SerialPort(path: serialPath)
    var results: [TestResult] = []

    let atCommands = [
        ("TTM:BPS-115200", "TTM:OK"), ("TTM:VID-?", "TTM:VID-LSD_BLE_5.0"),
        ("TTM:REV-?", "TTM:REV-V2.1"), ("TTM:CDL-5ms", "TTM:OK"),
        ("TTM:TPL-+4", "TTM:OK"), ("TTM:PID-1234", "TTM:OK"),
        ("TTM:REN-MAC-BLE-TEST", "TTM:OK"), ("TTM:REN-?", "TTM:REN-MAC-BLE-TEST"),
        ("TTM:MAC-?", "TTM:MAC-"), ("TTM:ADP-10", "TTM:OK"),
        ("TTM:ADD-MACBLE", "TTM:OK"), ("TTM:ADD-?", "TTM:ADD-MACBLE"),
        ("TTM:UNKNOWN", "TTM:ERP")
    ]
    for command in atCommands {
        results.append(atTest(serial, command: command.0, expected: command.1))
    }

    let session = BLESession()
    do {
        try session.connect(timeout: 12)
        results.append(TestResult(name: "扫描、连接 FFF0 并订阅 FFF4", passed: true, detail: "已建立 BLE 链路"))

        results.append(atTest(serial, command: "TTM:CIT-100ms", expected: "TTM:OK"))

        let bleToUART = Data("BLE_TO_UART_20_BYTES".utf8)
        serial.discardInput()
        try session.writeToUART(bleToUART, timeout: 3)
        let uartReceived = serial.readUntil(expected: bleToUART, timeout: 3)
        results.append(TestResult(name: "BLE FFF3 → UART0（20 字节以内）",
                                  passed: uartReceived.range(of: bleToUART) != nil,
                                  detail: printable(uartReceived)))

        let uartToBLE = Data("UART_TO_BLE_20_BYTES".utf8)
        serial.discardInput()
        _ = session.waitNotification(expected: Data(), timeout: 0.05)
        try serial.write(uartToBLE + Data("\r\n".utf8))
        let notification = session.waitNotification(expected: uartToBLE, timeout: 3)
        results.append(TestResult(name: "UART0 → BLE FFF4（20 字节以内）",
                                  passed: notification.range(of: uartToBLE) != nil,
                                  detail: printable(notification)))

        let longPayload = Data(repeating: 0x5A, count: 150)
        serial.discardInput()
        try session.writeToUART(longPayload, timeout: 5)
        let longReceived = serial.readUntil(expected: longPayload, timeout: 5)
        results.append(TestResult(name: "BLE FFF3 → UART0（150 字节 MTU 有效载荷）",
                                  passed: longReceived.range(of: longPayload) != nil,
                                  detail: "接收 \(longReceived.count) 字节"))
        session.disconnect()
    } catch {
        results.append(TestResult(name: "BLE 闭环测试", passed: false, detail: error.localizedDescription))
    }

    results.append(TestResult(name: "TTM:RST-SYSTEMRESET", passed: false,
                              detail: "未自动执行：该命令会使 USB 和串口短暂断开，请手动测试"))
    results.append(TestResult(name: "TTM:MAC-xxxxxxxxxxxx", passed: false,
                              detail: "未自动执行：会永久修改蓝牙地址，请在专门的生产测试中执行"))
    writeReport(results)
    let failed = results.filter { !$0.passed }.count
    print("测试完成：\(results.count - failed)/\(results.count) 通过；报告：macos_ble_test_record.md")
    exit(failed == 2 ? 0 : 1)
} catch {
    fputs("错误：\(error)\n", stderr)
    exit(2)
}
