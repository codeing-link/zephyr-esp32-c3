# ESP32-C3 MCUboot + BLE OTA 使用文档

本文说明如何在 macOS 上通过远程 Ubuntu 主机构建、首次下载和使用手机进行 BLE OTA 升级。当前 app 只保留两项功能：GPIO8 闪灯和 Zephyr 标准 MCUmgr/SMP over BLE OTA。

## 测试信息

|项目|当前值|说明|
|---|---|---|
|手机扫描名称|`ESP32C3-OTA`|在 nRF Connect Device Manager 中按这个名字查找|
|SMP OTA 服务 UUID|`8D53DC1D-1DB7-4CD3-868B-8A527460AA84`|Zephyr MCUmgr/SMP 标准 BLE OTA 服务|
|当前开发板烧录 MAC|`7c:4f:ad:d1:95:04`|来自 esptool 读取结果|
|RSSI|不固定|由手机实时测量，近距离常见约 `-30~-60 dBm`，距离远或遮挡时可能到 `-70~-90 dBm`|

注意：手机软件里看到的 BLE 地址可能带 public/random 类型，显示顺序也可能和 esptool 的芯片 MAC 不完全一致。实际测试时优先按广播名称 `ESP32C3-OTA` 查找。

## 固件目标

- 首次下载写入 `bootloader + initial app`：
  - `mcuboot` bootloader，位于 flash `0x00000000`。
  - `initial` app，位于 flash `0x00020000`，GPIO8 每 `500 ms` 翻转一次。
- OTA 升级只上传 app：
  - `update` app 上传到 secondary slot。
  - 升级并重启后 GPIO8 每 `1000 ms` 翻转一次。
- 手机端推荐使用 `nRF Connect Device Manager`。普通 `nRF Connect` 更适合扫描和查看服务，不一定提供完整 MCUmgr OTA 操作入口。

## Flash 分区

ESP32-C3 DevKitM 4MB flash 使用 Zephyr 板级默认分区：

|分区|地址|大小|用途|
|---|---:|---:|---|
|`mcuboot`|`0x00000000`|64KB|bootloader|
|`sys`|`0x00010000`|64KB|ESP32 系统分区|
|`image-0`|`0x00020000`|1792KB|当前运行 app|
|`image-1`|`0x001e0000`|1792KB|OTA 接收 app|
|`storage`|`0x003b0000`|192KB|预留存储区|
|`image-scratch`|`0x003e0000`|124KB|MCUboot swap|

## 构建产物

构建脚本会把手机 OTA 要用的 signed app 复制到：

```text
/Volumes/mpushare/mpushare/macos_workspace/zephyr/apps/esp32-c3/dist/
```

|变体|闪灯周期|用途|产物|
|---|---:|---|---|
|`initial`|500ms|首次整包下载后的 app|`dist/app-update-initial-500ms.signed.bin`|
|`update`|1000ms|手机 OTA 升级文件|`dist/app-update-update-1000ms.signed.bin`|

手机 OTA 时请选择 `dist/app-update-update-1000ms.signed.bin`，不要选择未签名的 `zephyr.bin`。

## 第一步：远程构建 initial 固件

在 macOS 进入工程目录：

```bash
cd /Volumes/mpushare/mpushare/macos_workspace/zephyr/apps/esp32-c3
```

构建首次下载用的 500ms initial 固件：

```bash
bash ./zephyr-remote-build.sh
```

构建完成后应生成：

```text
build/initial/
dist/app-update-initial-500ms.signed.bin
```

## 第二步：首次下载 bootloader + initial app

开发板 USB 连接在 Ubuntu 主机上，因此仍从 macOS 发起远程下载：

```bash
bash ./zephyr-remote-flash.sh
```

该命令默认下载 `build/initial`，会写入：

- MCUboot：`0x00000000`
- signed initial app：`0x00020000`

下载后观察板子：GPIO8 应约每 `0.5s` 翻转一次。

## 第三步：远程构建 OTA update 固件

构建 1000ms update app：

```bash
VARIANT=update bash ./zephyr-remote-build.sh
```

构建完成后生成：

```text
dist/app-update-update-1000ms.signed.bin
```

这个文件就是手机 OTA 要选择的升级文件。

## 第四步：把升级文件放到手机

把下面这个文件传到手机：

```text
/Volumes/mpushare/mpushare/macos_workspace/zephyr/apps/esp32-c3/dist/app-update-update-1000ms.signed.bin
```

可以使用 AirDrop、聊天工具文件传输、USB 文件传输或手机文件管理器，只要手机上的 nRF Connect Device Manager 能选到这个 `.bin` 文件即可。

## 第五步：手机执行 OTA

1. 打开 `nRF Connect Device Manager`。
2. 扫描设备，找到名称为 `ESP32C3-OTA` 的设备。
3. 连接设备。
4. 进入 `Image`、`DFU` 或 `Firmware Update` 页面，不同版本入口名称略有差异。
5. 选择文件 `app-update-update-1000ms.signed.bin`。
6. 开始 `Upload`。
7. 上传完成后执行 `Test and Reset`、`Confirm and Reset` 或软件提供的等价升级重启按钮。
8. 设备重启后观察 GPIO8：闪灯应从约 `0.5s` 一次变为约 `1.0s` 一次。

当前 app 启动后会主动调用 MCUboot confirm API。升级后的 app 只要能正常启动，就会确认当前镜像，后续复位不会回滚。

## 串口日志验证

烧录和日志共用 Ubuntu 上的 `/dev/ttyACM0`，监听日志前必须结束烧录命令。可在 macOS 通过远程方式监听：

```bash
ssh qinbo@192.168.1.100 \
  "cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3 && bash ./catcom.sh"
```

initial 启动时重点看：

```text
GPIO8 初始化完成，开始每 500 ms 翻转一次
BLE OTA 广播已启动，名称：ESP32C3-OTA
```

OTA 到 update 后应看到：

```text
GPIO8 初始化完成，开始每 1000 ms 翻转一次
当前 MCUboot 镜像确认成功
```

## 常见问题

### 手机找不到设备

先确认使用 `nRF Connect Device Manager` 扫描，目标名称是 `ESP32C3-OTA`。RSSI 不是固定值，手机上一般会显示类似 `-40 dBm`、`-65 dBm` 这样的信号强度。把手机靠近板子，列表里 RSSI 变强的 `ESP32C3-OTA` 就是目标设备。

### 看到了设备但没有 OTA 入口

普通 `nRF Connect` 可能只能看到 GATT 服务。请换成 `nRF Connect Device Manager`，它支持 Zephyr MCUmgr/SMP 固件上传。

### 上传失败

断开其它手机或电脑上的 BLE 连接，再重新连接 `ESP32C3-OTA`。当前固件只允许一个 BLE 连接。

### 上传后还是 0.5s 闪灯

确认上传的文件是：

```text
dist/app-update-update-1000ms.signed.bin
```

上传完成后需要执行 reset/test/confirm 对应操作。只上传但不重启，应用仍会停留在旧的 500ms 固件。

### 想重新从头测试

重新构建并下载 initial：

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

板子会回到 500ms 闪灯状态，然后再用手机上传 1000ms update 文件测试 OTA。
