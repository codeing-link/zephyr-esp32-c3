# ESP32C3 UART 桥接与 JPGU 协议

## 桥接原则

ESP32C3 推荐先做“透明桥接”，不要解析完整图片文件：

```text
BLE Write bytes -> UART TX -> MCU
MCU ACK/NACK bytes -> UART RX -> BLE Notify
```

这样 MCU 端可以继续使用 PC 串口脚本已经验证过的接收逻辑。

## UART 参数

与 MCU 约定一组固定参数：

```text
Baudrate: 115200 起步，建议稳定后提升到 921600
Data bits: 8
Parity: none
Stop bits: 1
Flow control: none 或 RTS/CTS
```

如果不用硬件流控，ESP32C3 必须做发送队列，避免 BLE 写入速度超过 UART 消化速度。

当前固件实际使用：

|项目|当前值|
|---|---|
|UART 控制器|ESP32C3 UART1|
|ESP32C3 TX|GPIO21，连接 MCU RX|
|ESP32C3 RX|GPIO20，连接 MCU TX|
|波特率|115200|
|数据格式|8N1|
|硬件流控|无|
|BLE 到 UART 队列|8192 字节环形队列|
|日志口|USB Serial/JTAG，不占用业务 UART|

下位机开发时请确保业务 UART 不混入调试日志。ESP32C3 只会从 UART RX 流中扫描 `JPGU` ACK/NACK 帧，其他字节会被丢弃。

## BLE 到 UART

小程序每次 BLE 写入的内容是 `JPGU` 业务包的一段或一个完整包。ESP32C3 处理方式：

1. 收到 RX Write 特征数据。
2. 将 bytes 追加到发送队列。
3. UART TX 任务按顺序写出。
4. 不改变任何字节。
5. 不插入分隔符。
6. 不合并或拆包到影响顺序的程度。

ESP32C3 不需要知道 START/DATA/END 的边界。真正的业务校验由 MCU 完成。

## UART 到 BLE

MCU 每个业务包返回 13 字节 ACK/NACK：

```text
offset size field
0      4    MAGIC = "JPGU"
4      1    VERSION = 1
5      1    CMD = 0x80 ACK 或 0x81 NACK
6      2    file_id, little-endian
8      4    seq, little-endian
12     1    status
```

ESP32C3 从 UART RX 收到 ACK/NACK 后，通过 TX Notify 特征原样通知给小程序。

推荐实现一个 13 字节窗口扫描：

1. 在 UART RX 流中寻找 `JPGU`。
2. 收满 13 字节。
3. 校验 VERSION 是否为 `1`。
4. CMD 必须是 `0x80` 或 `0x81`。
5. 通过 BLE notify 发给小程序。

如果 MCU 保证 ACK/NACK 永远是完整 13 字节且没有其它日志混入 UART，ESP32C3 也可以直接每 13 字节 notify 一次。但为了抗干扰，建议做窗口扫描。

当前 ESP32C3 固件已经实现 13 字节窗口扫描。下位机每处理完 START、DATA、END 或 FORMAT 后，必须尽快返回一帧 ACK/NACK，否则小程序会按超时重试。

ACK/NACK 帧示例：

```text
4a 50 47 55 01 80 01 00 00 00 00 00 00
J  P  G  U  v  ACK file  seq=0       ok
```

## JPGU 包结构

多字节字段全部小端。包尾 CRC 使用 CRC16-CCITT，文件 CRC 使用 CRC32。

### START

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

### DATA

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

### END

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x03
file_id[2]
total_chunks[4]
file_crc32[4]
crc16[2]
```

### FORMAT

```text
MAGIC[4]      "JPGU"
version[1]    0x01
cmd[1]        0x04
file_id[2]
crc16[2]
```

## NACK status

```text
1 CRC
2 SEQ
3 OFFSET
4 FS_WRITE
5 FILE_CRC
6 PARAM
7 TIMEOUT
8 STORAGE_FULL
```

小程序只会自动重试：

```text
CRC
TIMEOUT
```

如果收到 `STORAGE_FULL`，小程序会询问用户是否发送 FORMAT。

## 下位机接收状态机建议

下位机 MCU 建议按完整 JPGU 业务包解析，而不是按 UART 一次接收长度解析：

1. 在 UART RX 流中寻找 `JPGU`。
2. 读取 `version` 和 `cmd`。
3. 按 `cmd` 判断后续固定字段长度。
4. 对 START 根据 `name_len` 继续收文件名和 CRC16。
5. 对 DATA 根据 `payload_len` 继续收 payload 和 CRC16。
6. 对 END/FORMAT 收到固定长度后做 CRC16。
7. 校验通过后执行写入、擦除或结束逻辑。
8. 每个业务包都返回 13 字节 ACK/NACK。

ESP32C3 不会替 MCU 做 CRC16、CRC32、seq、offset 或存储校验。所有业务可靠性判断都在下位机和小程序之间完成。

## 图片格式处理

小程序会把用户选择的任意图片统一转换：

```text
尺寸：360x360 或 240x240
裁剪：居中裁剪为正方形
格式：JPG 或 BMP，用户手动选择
```

JPG：

- 文件名：`badge_360x360.jpg` 或 `badge_240x240.jpg`
- 内容：JPEG 压缩数据

BMP：

- 文件名：`badge_360x360.bmp` 或 `badge_240x240.bmp`
- 内容：标准 BMP 文件
- 像素格式：24-bit BGR
- 行方向：bottom-up
- 行对齐：4 字节
- `360x360` 预期大小：`54 + 360 * 360 * 3 = 388854` 字节
- `240x240` 预期大小：`54 + 240 * 240 * 3 = 172854` 字节

MCU 可以通过 START 包里的 filename 后缀判断图片格式，通过文件名中的 `360x360` 或 `240x240` 判断目标分辨率。更稳的做法是在 MCU 解析 JPG/BMP 文件头后再次确认实际宽高。
