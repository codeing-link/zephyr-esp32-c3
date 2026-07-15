# ESP32-C3 GPIO8 闪灯

本 Zephyr 工程每秒翻转一次 GPIO8，目标板为 `esp32c3_devkitm`。GPIO8 在板级
overlay 中被定义为 `led0` devicetree 别名，输出为高电平有效。

源代码目录通过 Samba 挂载，请勿直接在 macOS 上编译。

## 固件功能

- GPIO8 每秒闪烁，作为运行状态指示。
- BLE 外设透传服务：服务 `FFF0`，手机写入 `FFF3` 后转发至 UART0，UART0 数据通过 `FFF4` 通知手机。
- UART `TTM:` AT 命令：支持版本、名称、广播周期、附加广播数据、波特率、连接间隔与软件复位等控制。
- 协议核心与 Zephyr 平台适配层解耦，方便移植。

详细操作见 [蓝牙透传测试文档](doc/蓝牙透传测试文档.md) 和 [蓝牙透传移植文档](doc/蓝牙透传移植文档.md)。

## 复位后的启动日志

日志经 ESP32-C3 内置 USB Serial/JTAG 输出到主机侧 `/dev/ttyACM0`。按下 `RST` 后，
USB 设备会断开并重新枚举约 1 秒；固件的应用主线程会先等待 2 秒，之后才开始 GPIO8
初始化并输出首条日志，因此监听程序重连后仍可获得全部应用启动日志。

### 使用方法

1. 烧录前先按 `Ctrl+C` 退出日志监听，避免 `/dev/ttyACM0` 被占用。
2. 执行烧录脚本。
3. 在工程目录运行 `catcom.sh`。脚本会监听 `/dev/ttyACM0`，在开发板复位导致 USB
   串口短暂断开时自动等待并重连。
4. 按开发板 `RST/EN` 键。应用延后 2 秒才开始输出日志，因此可完整捕获应用层启动过程。
5. 结束监听时直接按 `Ctrl+C`。

```bash
# 烧录：执行前必须关闭日志监听
bash ./zephyr-remote-flash.sh

# 监听日志；按 Ctrl+C 退出
bash ./catcom.sh
```

启动正常时应看到以下日志：

```bash
[00:00:02.030,000] <inf> gpio8_blinky: 应用启动延时结束，开始初始化 GPIO8 状态灯
[00:00:02.031,000] <inf> gpio8_blinky: GPIO8 初始化完成，开始每 1000 ms 翻转一次
```

第一条日志表示 USB 重连等待已完成；第二条日志表示 GPIO8 已成功配置并进入闪灯循环。
若未出现第二条日志，请根据终端中最后一条日志定位初始化失败的位置。

## 验证状态

已于 2026-07-15 验证：

- ESP32-C3 硬件上的 GPIO8 LED 闪烁正常。
- macOS 侧远程编译和烧录流程执行成功。
- SSH 登录 Ubuntu 后手动执行的编译和烧录流程执行成功。

## 从 macOS 远程编译和烧录

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

两个脚本默认使用 `qinbo@192.168.1.100`；如有需要，可通过环境变量覆盖：

```bash
UBUNTU_HOST=user@host bash ./zephyr-remote-build.sh
```

## SSH 登录 Ubuntu 后编译和烧录

```bash
cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3
bash ./build_esp32c3.sh
bash ./flash_esp32c3.sh
```

脚本默认要求工作区位于 `/home/qinbo/mpushare/macos_workspace/zephyr`；如安装在
其他位置，请设置 `ZEPHYR_WORKSPACE`。`flash_esp32c3.sh` 使用 Zephyr 已配置的
ESP32 烧录器。请先将开发板连接到 Ubuntu 主机；需要指定设备时，可将烧录器参数
直接传给脚本，例如 `bash ./flash_esp32c3.sh --esp-device /dev/ttyUSB0`。

## Ubuntu 环境前置条件

ESP32 编译要求 Zephyr Python 环境包含 `esptool` 5.0.2 或更高版本。已验证的
Ubuntu 环境已具备该依赖；其他环境若提示缺失，请在 Zephyr 工作区执行一次：

```bash
source .venv/bin/activate
west packages pip --install
```
