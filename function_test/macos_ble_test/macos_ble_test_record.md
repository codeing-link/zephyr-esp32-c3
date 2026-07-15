# macOS 蓝牙与串口闭环测试记录

## TTM:BPS-115200
- 结果：通过
- 详情：`TTM:BPS-115200TTM:OK`

## TTM:VID-?
- 结果：通过
- 详情：`TTM:VID-?TTM:VID-LSD_BLE_5.0`

## TTM:REV-?
- 结果：通过
- 详情：`TTM:REV-?TTM:REV-V2.1`

## TTM:CDL-5ms
- 结果：通过
- 详情：`TTM:CDL-5msTTM:OK`

## TTM:TPL-+4
- 结果：通过
- 详情：`TTM:TPL-+4TTM:OK`

## TTM:PID-1234
- 结果：通过
- 详情：`TTM:PID-1234TTM:OK`

## TTM:REN-MAC-BLE-TEST
- 结果：通过
- 详情：`TTM:REN-MAC-BLE-TESTTTM:OK`

## TTM:REN-?
- 结果：通过
- 详情：`TTM:REN-?TTM:REN-MAC-BLE-TEST`

## TTM:MAC-?
- 结果：通过
- 详情：`TTM:MAC-?TTM:MAC-7C4FADD19506
`

## TTM:ADP-10
- 结果：通过
- 详情：`TTM:ADP-10TTM:OK`

## TTM:ADD-MACBLE
- 结果：通过
- 详情：`TTM:ADD-MACBLETTM:OK`

## TTM:ADD-?
- 结果：通过
- 详情：`TTM:ADD-?TTM:ADD-MACBLE`

## TTM:UNKNOWN
- 结果：通过
- 详情：`TTM:UNKNOWNTTM:ERP`

## BLE 扫描、连接 FFF0 并订阅 FFF4
- 结果：通过
- 详情：`已建立链路`

## TTM:CIT-100ms（连接状态）
- 结果：通过
- 详情：`TTM:CIT-100msTTM:OK`

## BLE FFF3 → UART0（20 字节）
- 结果：通过
- 详情：`BLE_TO_UART_20_BYTES`

## UART0 → BLE FFF4（20 字节）
- 结果：通过
- 详情：`UART_TO_BLE_20_BYTES`

## BLE FFF3 → UART0（150 字节）
- 结果：通过
- 详情：`Mac 单次写入上限 512 字节；接收 150 字节`

## TTM:RST-SYSTEMRESET
- 结果：未执行
- 详情：`未自动执行：该命令会断开串口和蓝牙`

## TTM:MAC-xxxxxxxxxxxx
- 结果：未执行
- 详情：`未自动执行：该命令会永久修改 MAC`
