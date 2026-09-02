# ESP32C3 固件开发总览

## 目标

ESP32C3 固件负责把微信小程序和 MCU 串起来：

```text
微信小程序 <--BLE GATT--> ESP32C3 <--UART--> MCU
```

小程序侧会完成：

- 手机蓝牙扫描与连接。
- 选择手机图片。
- 将任意原图裁剪为目标正方形尺寸，默认 `360x360`，可选 `240x240`。
- 按用户选择输出 `JPG` 或 `BMP`。
- 按 `JPGU` 文件传输协议分包发送。
- 等待 MCU 的 ACK/NACK。

ESP32C3 侧建议尽量做薄：

- 当前开发阶段上电后自动 BLE 广播，方便小程序和 OTA 工具随时连接。
- 后续硬件确定配对按键 GPIO 后，可以改为按键进入 60 秒 BLE 配对/广播窗口。
- 暴露一个 BLE 服务、一个写特征、一个通知特征。
- 小程序写入的二进制数据按顺序转发给 UART。
- MCU 返回的 ACK/NACK 按顺序通过 BLE notify 发回小程序。

这样 PC 串口脚本、小程序和 MCU 三者可以复用同一套文件传输协议。

## 用户操作流程

1. 用户短按 ESP32C3 板上的配对按键。
2. ESP32C3 开启 BLE 广播，进入可连接状态。
3. 用户点击小程序首页的“一键配对”。
4. 小程序扫描附近 BLE 设备，命中目标设备后自动连接。
5. 小程序发现服务和特征，打开 notify，协商 MTU。
6. 用户选择图片、下发尺寸和下发格式。
7. 小程序把图片处理成 `360x360` 或 `240x240`，点击“下发到板子”。
8. ESP32C3 将 BLE 收到的数据透传给 MCU。
9. MCU 每个业务包返回 ACK/NACK。
10. ESP32C3 将 ACK/NACK notify 给小程序。
11. 小程序进度到 100%，图片下发完成。

## 固件模块建议

当前固件先集中在 `src/main.c`，已经实现：

```text
BLE GATT server
  自定义主服务 UUID 6e400001-b5a3-f393-e0a9-e50e24dcca9e
  RX 写特征 6e400002-b5a3-f393-e0a9-e50e24dcca9e
  TX notify 特征 6e400003-b5a3-f393-e0a9-e50e24dcca9e

BLE -> UART1
  BLE 写回调只把 bytes 放入 8192 字节环形队列
  独立线程按顺序从 UART1 发出

UART1 -> BLE
  UART RX 中断扫描 13 字节 JPGU ACK/NACK
  找到有效帧后通过 TX notify 原样发给小程序

OTA
  保留 MCUboot
  保留 Zephyr MCUmgr/SMP over BLE 服务
  app 启动后继续确认当前镜像，避免升级成功后回滚
```

后续代码膨胀后建议再拆分为 `ble_gatt_server.c`、`uart_bridge.c`、`app_main.c`。

## 状态机

```text
IDLE
  量产建议默认不广播，等待按键。
  当前开发固件暂时上电自动进入 PAIRING/广播。

PAIRING
  按键触发后进入，启动 BLE 广播，建议持续 60 秒。

CONNECTED
  小程序连接成功，停止或保留连接广播，打开 GATT 数据通道。

TRANSFERRING
  收到 JPGU START 后进入，持续桥接 DATA/END 和 ACK/NACK。

DONE
  END 包成功 ACK 后回到 CONNECTED，允许再次下发。

ERROR
  BLE 断开、UART 超时、缓冲溢出等错误后清理状态。
```

## 图片格式说明

小程序当前支持两种下发格式：

- `JPG`
  - 文件小，BLE 下发速度快。
  - MCU 侧需要能解码 JPG 或原有流程已经支持 JPG。

- `BMP`
  - 小程序生成标准 24-bit BMP，尺寸可以是 `360x360` 或 `240x240`。
  - `360x360` 文件约 `388854` 字节，`240x240` 文件约 `172854` 字节。
  - MCU 侧必须支持 BMP 文件读取或显示转换。

注意：无论选择 JPG 还是 BMP，传输层仍使用现有 `JPGU` 文件传输协议。`JPGU` 在这里表示既有传输帧格式，不再只代表 JPG 内容。

## 推荐默认参数

```text
BLE 设备名：E-Badge-C3
广播窗口：开发阶段上电常开；量产建议按键触发 60 秒
连接超时：12 秒
首选 MTU：247
小程序业务 chunk：240 字节
默认图片尺寸：360x360
可选图片尺寸：240x240
UART 波特率：115200，稳定后建议 921600 或更高
ACK 超时：2.5 秒
重试次数：5
```

如果 UART 波特率保持 115200，BMP 下发会比较慢。建议在 ESP32C3 和 MCU 都稳定后，把 UART 提到 `921600` 或更高。

## 当前硬件连接

|ESP32C3|目标 MCU|用途|
|---|---|---|
|GPIO21 / UART1 TX|MCU RX|小程序图片数据下发|
|GPIO20 / UART1 RX|MCU TX|MCU ACK/NACK 回传|
|GND|GND|共地|
|USB Serial/JTAG|调试电脑|烧录、日志、OTA 调试|

日志和图片流已经分离：日志走 USB Serial/JTAG，业务数据走 UART1。下位机串口不能输出调试日志到这条业务 UART，否则 ESP32C3 会只识别 `JPGU` ACK/NACK，其他字节会被扫描器丢弃。
