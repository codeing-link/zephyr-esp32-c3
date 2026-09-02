# 下位机 MCU 固件对接指南

本文面向目标板 MCU 固件开发。ESP32C3 当前只负责 BLE 与 UART 之间的透明桥接，不解析 JPG/BMP 文件，不替下位机做业务校验。

## 链路

```text
微信小程序 <--BLE GATT--> ESP32C3 <--UART1 115200 8N1--> 下位机 MCU
```

ESP32C3 接线：

|ESP32C3|下位机 MCU|方向|
|---|---|---|
|GPIO21 / UART1 TX|MCU RX|小程序数据下发到 MCU|
|GPIO20 / UART1 RX|MCU TX|MCU ACK/NACK 回传到小程序|
|GND|GND|共地|

ESP32C3 日志走 USB Serial/JTAG，不会混入 UART1。下位机 MCU 的业务 UART 也不要输出普通日志，避免影响 ACK/NACK 扫描和后续扩展。

## MCU 必须实现

1. 从 UART RX 字节流中扫描 `JPGU` 包头。
2. 解析 START、DATA、END、FORMAT 四类业务包。
3. 校验每个包末尾 CRC16。
4. 对 DATA 校验 `seq`、`offset` 和 `payload_len`。
5. 将 DATA payload 按顺序写入 RAM、Flash、文件系统或显示缓存。
6. END 包后校验整文件 CRC32。
7. 每处理完一个业务包，都通过 UART TX 返回 13 字节 ACK/NACK。

ESP32C3 会把 ACK/NACK 原样 notify 给小程序。小程序依赖 ACK 才会继续发送下一包。

## ACK/NACK

ACK/NACK 固定 13 字节，小端：

```text
offset size field
0      4    MAGIC = "JPGU"
4      1    VERSION = 1
5      1    CMD = 0x80 ACK 或 0x81 NACK
6      2    file_id
8      4    seq
12     1    status
```

ACK 时 `status=0`。NACK 时 `status` 使用：

|status|含义|小程序行为|
|---:|---|---|
|1|CRC|自动重试|
|2|SEQ|停止或按策略重试|
|3|OFFSET|停止或按策略重试|
|4|FS_WRITE|停止|
|5|FILE_CRC|停止|
|6|PARAM|停止|
|7|TIMEOUT|自动重试|
|8|STORAGE_FULL|询问用户是否发送 FORMAT|

DATA 包 ACK 的 `seq` 填当前 DATA 的序号。START 可以填 `seq=0`。END 可以填最后一个已接收序号或 `total_chunks`，但小程序侧应以 END ACK 作为完成信号。

## 接收包处理

### START

START 包提供文件总体信息：

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x01
file_id[2]
file_size[4]
file_crc32[4]
chunk_size[2]
name_len[1]
filename[name_len]
crc16[2]
```

MCU 收到 START 后建议：

1. 校验 `version`、`cmd`、`file_size`、`chunk_size`、`name_len` 和 CRC16。
2. 根据 `file_size` 判断剩余存储是否足够。
3. 根据 `filename` 后缀识别 `.jpg` 或 `.bmp`。
4. 清空上一轮同 `file_id` 的临时接收状态。
5. 准备写入区域后返回 ACK。

### DATA

DATA 包承载文件内容：

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x02
file_id[2]
seq[4]
offset[4]
payload_len[2]
payload[payload_len]
crc16[2]
```

MCU 收到 DATA 后建议：

1. 校验 CRC16。
2. 校验 `file_id` 是否匹配当前 START。
3. 校验 `seq` 是否为期望序号。
4. 校验 `offset` 是否为已写入字节数。
5. 写入 `payload`。
6. 写入成功后 ACK，失败后 NACK。

### END

END 包表示文件发送完成：

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x03
file_id[2]
total_chunks[4]
file_crc32[4]
crc16[2]
```

MCU 收到 END 后建议：

1. 校验 CRC16。
2. 校验 `file_id`、累计字节数和 `total_chunks`。
3. 计算接收文件 CRC32，与 `file_crc32` 比较。
4. JPG 文件进入 JPG 解码或存储流程。
5. BMP 文件解析 BMP 头，确认宽高、24-bit BGR、bottom-up、4 字节行对齐。
6. 全部成功后返回 ACK。

### FORMAT

FORMAT 包用于用户确认清空目标存储：

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x04
file_id[2]
crc16[2]
```

MCU 收到 FORMAT 后只应清理图片接收相关存储，不建议擦除 bootloader、应用固件或配置区。完成后返回 ACK，失败返回 NACK `FS_WRITE` 或 `PARAM`。

## 图片格式

JPG：

- 文件名通常为 `badge_360x360.jpg` 或 `badge_240x240.jpg`。
- MCU 需要支持 JPEG 解码，或先只做存储和 CRC 验证。

BMP：

- 文件名通常为 `badge_360x360.bmp` 或 `badge_240x240.bmp`。
- BMP 为标准 24-bit BGR。
- 像素方向为 bottom-up。
- 行按 4 字节对齐。
- `360x360` 文件约 `388854` 字节。
- `240x240` 文件约 `172854` 字节。

MCU 最终应以文件头解析结果为准，文件名只作为快速判断。

## 超时和重试

小程序默认 ACK 超时约 `2.5s`，重试 5 次。MCU 如果需要擦除 Flash，建议在 START ACK 前完成必要擦除，或者先优化为按扇区边收边擦，避免长时间不回 ACK。

115200 波特率下 BMP 完整下发会较慢。链路稳定后，建议 ESP32C3 和 MCU 同时改为 `921600 8N1` 或更高，并重新做完整 JPG、BMP、异常断链测试。
