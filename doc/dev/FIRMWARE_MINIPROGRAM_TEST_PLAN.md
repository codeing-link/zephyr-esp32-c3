# ESP32C3 与小程序联调清单

## 第一阶段：BLE 连接

1. 烧录 ESP32C3 固件。
2. 当前开发固件上电后会自动广播；量产版再接入配对按键。
3. 确认 ESP32C3 开始广播，设备名为 `E-Badge-C3`。
4. 打开小程序，点击“一键配对”。
5. 确认小程序显示“已连接”。
6. 小程序日志应出现“已找到写入/通知通道”。

失败排查：

- 扫不到设备：检查是否进入配对广播窗口。
- 能扫到但不自动连接：检查设备名是否包含 `E-Badge`、`Monokaro`、`ESP32`、`Badge`、`BLE`，或在小程序中填写 Service UUID。
- 连接后提示找不到通道：检查 GATT 是否有一个 Write 特征和一个 Notify 特征。

## 第二阶段：UART 透传

1. ESP32C3 连接 MCU UART：GPIO21 接 MCU RX，GPIO20 接 MCU TX，两端共地。
2. MCU 使用和 `send_jpg_uart.py` 相同的接收协议。
3. 小程序选择 `360x360` 和 JPG 格式。
4. 选择一张小图片。
5. 点击“下发到板子”。
6. MCU 应先收到 START 包。
7. MCU 返回 ACK。
8. 小程序开始显示 DATA 包进度。

失败排查：

- 小程序一直 `START seq=0 TIMEOUT`：ESP32C3 没有把 MCU ACK notify 回小程序。
- MCU 收到乱码：检查 UART 波特率是否为 `115200 8N1`，以及 TX/RX 是否交叉连接。
- MCU 报 CRC：检查 ESP32C3 是否丢字节、插入日志、或多个任务同时写 UART。
  当前固件日志走 USB Serial/JTAG，业务数据走 UART1，正常情况下不会互相污染。

## 第三阶段：完整 JPG 下发

1. 选择 `360x360` 和 JPG。
2. 下发一张普通图片。
3. 小程序进度到 100%。
4. MCU 收到 END 包并校验文件 CRC32。
5. 目标板显示图片。

建议测试图片：

- 全黑。
- 全白。
- 红绿蓝色块。
- 一张普通照片。

## 第四阶段：BMP 下发

1. 选择 `360x360` 和 BMP。
2. 选择同一张图片。
3. 小程序显示 `360x360 BMP`。
4. 下发完成后，MCU 根据 `.bmp` 后缀走 BMP 解析。

注意：

- BMP 文件约 `388854` 字节，115200 波特率会比较慢。
- 如果 MCU 当前只支持 JPG，BMP 可以先只做存储验证，显示解析后续再接。

## 第五阶段：240x240 硬件适配

1. 选择 `240x240` 和 JPG。
2. 下发一张普通图片。
3. MCU 根据文件名或图片头识别 `240x240`。
4. 目标板按 240 分辨率显示图片。
5. 再选择 `240x240` 和 BMP，重复验证。

注意：

- `240x240` BMP 文件约 `172854` 字节。
- 如果硬件只支持 240 分辨率，MCU 收到 `360x360` 文件时应返回 `PARAM` NACK 或做缩放处理。

## 第六阶段：异常路径

依次验证：

```text
断开蓝牙后下发
传输中关闭 ESP32C3
传输中 MCU 不回 ACK
MCU 返回 CRC NACK
MCU 返回 STORAGE_FULL
用户确认 FORMAT
用户取消 FORMAT
```

小程序期望表现：

- 未连接时禁止下发。
- CRC/TIMEOUT 自动重试。
- STORAGE_FULL 弹窗确认格式化。
- 用户取消格式化后停止下发并显示失败日志。

## 推荐日志

ESP32C3 日志建议包含：

```text
BLE 图片桥接广播已启动
业务 UART1 已启动
ble connected
notify enabled
rx write len=...
uart tx queued len=...
uart ack notify file_id=... seq=... status=...
ble disconnected
pairing timeout
```

当前固件默认日志级别为 INFO。BLE 写入长度和 ACK 明细在 DEBUG 级别，必要时可临时把 `CONFIG_LOG_DEFAULT_LEVEL` 调到 `4` 后重新构建。

MCU 日志建议包含：

```text
START file_id=... size=... crc32=... chunk=... name=...
DATA seq=... offset=... len=...
END total_chunks=... crc32=...
ACK seq=...
NACK seq=... status=...
```
