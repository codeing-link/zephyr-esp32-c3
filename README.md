# ESP32-C3 GPIO8 状态灯与 BLE 透传

本工程面向 `esp32c3_devkitm`，已完成 GPIO8 状态指示、UART0 AT 指令和 BLE 透传服务的初步开发及硬件闭环测试。源代码目录通过 Samba 挂载，请勿直接在 macOS 上编译 Zephyr 固件。

## 固件功能

- GPIO8 每 1000 ms 翻转一次，作为固件运行状态指示。
- BLE 外设服务 UUID 为 `0xFFF0`：手机向 `0xFFF3` 写入的数据转发至 UART0；UART0 收到的数据通过 `0xFFF4` Notify 发给已订阅的手机。
- 支持普通 GATT 写入以及 Prepare/Execute 长写入。默认 ATT MTU 为 23（单包有效载荷 20 字节），中央设备可协商至 153（单包有效载荷 150 字节）。
- UART0 默认参数为 115200、8N1、无流控；支持原始需求第 5.2 节的 15 类 `TTM:` AT 指令。
- 名称、广播周期、产品 ID、附加广播数据、数据延时和自定义 MAC 使用内部 Flash 保存；自定义 MAC 在下次复位后生效。
- AT 协议解析位于平台无关的 `src/ttm_protocol.c/.h`。协议实例不使用全局状态，且通过带 `context` 的回调调用 Zephyr、UART、BLE、NVS 和 GPIO 适配层，便于移植或在主机侧模拟测试。

原始需求的脱敏 Markdown 版本见 [蓝牙需求.md](蓝牙需求.md)。测试方法见 [蓝牙透传测试文档](doc/蓝牙透传测试文档.md)，移植说明见 [蓝牙透传移植文档](doc/蓝牙透传移植文档.md)，Mac mini 本地闭环测试说明见 [macOS 测试说明](function_test/macos_ble_test/README.md)。

## 已完成验证

截至 2026-07-15，以下项目已完成验证：

|项目|结果|验证方式|
|---|---|---|
|GPIO8 状态灯与启动日志|通过|开发板实测，GPIO8 每秒闪烁|
|远程编译、烧录|通过|macOS 通过 SSH 使用 Ubuntu 构建环境|
|Linux UART AT 指令|14/14 通过|`function_test/test_at_commands.py`，记录见 [AT指令测试记录.md](doc/AT指令测试记录.md)|
|手机 nRF Connect BLE 透传|通过|扫描、连接、订阅和双向 20 字节透传|
|macOS 蓝牙与串口闭环|自动项目全部通过|AT 配置、扫描连接、订阅、双向 20 字节和 BLE→UART 150 字节|

`TTM:RST-SYSTEMRESET` 会主动断开当前串口和 BLE 链路，`TTM:MAC-xxxxxxxxxxxx` 会永久改变设备身份地址；macOS 自动化程序刻意不执行这两项，并在报告中标为“未执行”，请按测试文档中的人工步骤验证。

## USB 日志监听

ESP32-C3 内置 USB Serial/JTAG 在主机侧枚举为 `/dev/ttyACM0`，用于烧录和日志，二者不能同时占用。按下 `RST` 后 USB 会断开并约 1 秒后重连；应用启动会额外等待 2 秒，确保 `catcom.sh` 重连后仍能抓到完整的应用层启动日志。

```bash
# 烧录前必须按 Ctrl+C 结束日志监听
bash ./zephyr-remote-flash.sh

# 监听日志；开发板复位时会自动重连，Ctrl+C 退出
bash ./catcom.sh
```

正常启动时应出现以下关键日志：

```text
应用启动延时结束，开始初始化 GPIO8 状态灯
GPIO8 初始化完成，开始每 1000 ms 翻转一次
BLE 透传服务已启动：FFF0/FFF3/FFF4
```

首次升级后如检测到旧版或损坏的 NVS AT 配置，固件会恢复出厂默认参数；这是保护性迁移行为，不影响后续配置保存。

## 构建与烧录

### macOS 发起远程构建

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

脚本默认使用 `qinbo@192.168.1.100`，可通过环境变量覆盖：

```bash
UBUNTU_HOST=user@host bash ./zephyr-remote-build.sh
```

### SSH 登录 Ubuntu 后手动构建

```bash
cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3
bash ./build_esp32c3.sh
bash ./flash_esp32c3.sh
```

脚本默认工作区为 `/home/qinbo/mpushare/macos_workspace/zephyr`。若路径不同，请设置 `ZEPHYR_WORKSPACE`。需要指定烧录口时，可传递参数，例如：

```bash
bash ./flash_esp32c3.sh --esp-device /dev/ttyUSB0
```

Ubuntu 构建环境需要 Zephyr Python 环境及 `esptool` 5.0.2 或更高版本；缺少依赖时执行：

```bash
source .venv/bin/activate
west packages pip --install
```
