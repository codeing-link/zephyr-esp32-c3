# ESP32-C3 BLE 图片桥接与 OTA

本工程面向 `esp32c3_devkitm`，当前 app 负责把微信小程序的 BLE 图片数据透传到下位机 MCU，并保留 MCUboot + BLE OTA 升级框架。源代码目录通过 Samba 挂载，请在 macOS 上使用远程脚本调用 Ubuntu 构建和下载环境。

## 当前功能

- `initial` 固件：GPIO8 每 `500 ms` 翻转一次，用于首次下载验证。
- `update` 固件：GPIO8 每 `1000 ms` 翻转一次，用于手机 OTA 验证。
- BLE 广播名称固定为 `E-Badge-C3`。
- 小程序通过自定义 GATT 服务写入图片传输数据，ESP32C3 原样转发到 UART1。
- 下位机 MCU 通过 UART1 返回 JPGU ACK/NACK，ESP32C3 原样通过 BLE notify 回小程序。
- OTA 使用 Zephyr 标准 MCUmgr/SMP over BLE 服务：

```text
8D53DC1D-1DB7-4CD3-868B-8A527460AA84
```

当前测试板 esptool 读取到的烧录 MAC 为 `7c:4f:ad:d1:95:04`。手机上看到的 RSSI 是实时信号强度，不是固定值；近距离常见约 `-30~-60 dBm`。

## 小程序 BLE 通道

|项目|当前值|
|---|---|
|设备名|`E-Badge-C3`|
|主服务 UUID|`6e400001-b5a3-f393-e0a9-e50e24dcca9e`|
|RX 写特征|`6e400002-b5a3-f393-e0a9-e50e24dcca9e`|
|TX 通知特征|`6e400003-b5a3-f393-e0a9-e50e24dcca9e`|
|建议 MTU|247|

ESP32C3 不解析图片文件内容。小程序写入的每段 JPGU 数据都会进入 UART 发送队列，按收到顺序从 UART1 发给下位机。

## 下位机 UART 接线

|ESP32C3|下位机 MCU|说明|
|---|---|---|
|GPIO21 / UART1 TX|MCU RX|ESP32C3 下发 JPGU 数据|
|GPIO20 / UART1 RX|MCU TX|MCU 回传 ACK/NACK|
|GND|GND|必须共地|

串口参数默认 `115200 8N1`，无流控。图片 BMP 文件较大，联调稳定后建议两端一起提升到 `921600` 或更高。

## 构建与下载

在 macOS 进入工程目录：

```bash
cd /Volumes/mpushare/mpushare/macos_workspace/zephyr/apps/esp32-c3
```

首次整包下载 500ms initial 固件：

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

构建手机 OTA 使用的 1000ms update 固件：

```bash
VARIANT=update bash ./zephyr-remote-build.sh
```

手机 OTA 文件输出到：

```text
dist/app-update-update-1000ms.signed.bin
```

详细手机升级步骤见 [OTA升级使用文档](doc/OTA升级使用文档.md)。

## 远程环境

脚本默认使用：

```text
qinbo@192.168.1.100
```

需要覆盖远程主机时：

```bash
UBUNTU_HOST=user@host bash ./zephyr-remote-build.sh
UBUNTU_HOST=user@host bash ./zephyr-remote-flash.sh
```

SSH 登录 Ubuntu 后也可以手动执行：

```bash
cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3
bash ./build_esp32c3.sh
bash ./flash_esp32c3.sh
```

## 日志

烧录和日志共用 Ubuntu 上的 `/dev/ttyACM0`，不能同时占用。监听日志：

```bash
ssh qinbo@192.168.1.100 \
  "cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3 && bash ./catcom.sh"
```

initial 启动时应看到：

```text
GPIO8 初始化完成，开始每 500 ms 翻转一次
BLE 图片桥接广播已启动，名称：E-Badge-C3
业务 UART1 已启动：GPIO21 TX、GPIO20 RX、115200 8N1
```

update 启动时应看到：

```text
GPIO8 初始化完成，开始每 1000 ms 翻转一次
```
