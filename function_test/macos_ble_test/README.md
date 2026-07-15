# macOS 蓝牙与串口闭环测试

本程序在 Mac mini 本地编译运行，不使用 SSH。它同时使用 Mac mini 内置蓝牙和 USB 转串口，对 ESP32-C3 执行 AT 配置、BLE 连接及透传闭环验证。

截至 2026-07-15，全部自动测试项目已通过；最新执行结果见 [macos_ble_test_record.md](macos_ble_test_record.md)。

## 覆盖范围

|类别|自动测试内容|
|---|---|
|UART AT|波特率、厂商、版本、数据延时、发射功率、产品 ID、名称、MAC 查询、广播周期、附加广播数据及非法命令|
|BLE 建链|扫描目标广播、连接 `FFF0` 服务、订阅 `FFF4` 通知|
|连接状态|连接后 `TTM:CIT-100ms` 返回成功|
|透传|`FFF3`→UART0 20 字节、UART0→`FFF4` 20 字节、`FFF3`→UART0 150 字节|

150 字节项目使用 macOS 的长写机制，验证目标设备能够处理 Prepare/Execute 写入及协商后的 153 字节 MTU。

## 测试前准备

- Mac mini 已打开蓝牙，并在“隐私与安全性”中允许终端使用蓝牙。
- ESP32-C3 已烧录最新固件，UART0 已接 USB 转串口模块。
- 接线：ESP32-C3 GPIO21（TX）接转串口 RX，GPIO20（RX）接转串口 TX，双方 GND 共地。
- 使用 macOS 串口设备名，例如 `/dev/cu.usbserial-xxxx`，不要使用 Linux 的 `/dev/ttyUSB0`。
- 测试前断开 nRF Connect 或其他 BLE 中央设备，避免受连接状态限制的 AT 设置命令返回 `TTM:ERP`。

查看可用串口：

```bash
ls /dev/cu.*
```

## 编译和运行

```bash
cd apps/esp32-c3/function_test/macos_ble_test
bash ./build_macos_ble_test.sh
./macos_ble_test --serial /dev/cu.usbserial-xxxx
```

程序退出码为 0 表示全部自动项目通过，并会在当前目录生成 `macos_ble_test_record.md`。测试会修改名称、产品 ID、广播周期、数据延时及附加广播内容，属于预期行为。

## 人工安全项目

以下命令不会自动执行，因此报告显示“未执行”，不应按测试失败处理：

|命令|原因|人工验证方法|
|---|---|---|
|`TTM:RST-SYSTEMRESET`|会立即中断串口与 BLE 链路|手工发送命令，确认先收到 `TTM:OK`，设备重启后重新连接并查看启动日志|
|`TTM:MAC-xxxxxxxxxxxx`|会永久修改身份地址|记录原 MAC 后手工设置一个可追溯的测试地址，复位并扫描确认新地址；完成后按项目要求恢复|

## 常见问题

- 扫描不到设备：确认手机已断开、设备正在广播、名称未被之前测试修改为意外值，并检查 macOS 蓝牙权限。
- 150 字节失败：确认固件包含长写支持，检查 Central 是否协商 MTU，以及 UART 接收侧是否连接正确。
- AT 返回空：检查串口路径、115200 波特率和 TX/RX 是否交叉连接。
