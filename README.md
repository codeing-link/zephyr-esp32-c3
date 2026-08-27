# ESP32-C3 GPIO8 闪灯与 BLE OTA

本工程面向 `esp32c3_devkitm`，当前 app 已简化为 GPIO8 闪灯和 MCUboot + BLE OTA。源代码目录通过 Samba 挂载，请在 macOS 上使用远程脚本调用 Ubuntu 构建和下载环境。

## 当前功能

- `initial` 固件：GPIO8 每 `500 ms` 翻转一次，用于首次下载验证。
- `update` 固件：GPIO8 每 `1000 ms` 翻转一次，用于手机 OTA 验证。
- BLE 广播名称固定为 `ESP32C3-OTA`。
- OTA 使用 Zephyr 标准 MCUmgr/SMP over BLE 服务：

```text
8D53DC1D-1DB7-4CD3-868B-8A527460AA84
```

当前测试板 esptool 读取到的烧录 MAC 为 `7c:4f:ad:d1:95:04`。手机上看到的 RSSI 是实时信号强度，不是固定值；近距离常见约 `-30~-60 dBm`。

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
BLE OTA 广播已启动，名称：ESP32C3-OTA
```

update 启动时应看到：

```text
GPIO8 初始化完成，开始每 1000 ms 翻转一次
```
