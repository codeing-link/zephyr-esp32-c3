# AT 指令测试记录

测试日期：2026-07-15

测试串口：`/dev/ttyUSB0`
串口参数：115200、8N1、无流控

## 自动测试结论

`function_test/test_at_commands.py` 已执行，14/14 个安全 AT 测试项目通过。串口返回会包含命令回显，以下结果均以包含期望响应为判定条件。

|命令|结果|实际关键响应|
|---|---|---|
|`TTM:BPS-115200`|通过|`TTM:OK`|
|`TTM:VID-?`|通过|`TTM:VID-LSD_BLE_5.0`|
|`TTM:REV-?`|通过|`TTM:REV-V2.1`|
|`TTM:CDL-5ms`|通过|`TTM:OK`|
|`TTM:TPL-+4`|通过|`TTM:OK`|
|`TTM:PID-1234`|通过|`TTM:OK`|
|`TTM:REN-?`|通过|返回当前名称|
|`TTM:REN-TEST-C3`|通过|`TTM:OK`|
|再次 `TTM:REN-?`|通过|`TTM:REN-TEST-C3`|
|`TTM:MAC-?`|通过|返回 `TTM:MAC-` 加 12 位十六进制地址|
|`TTM:ADP-10`|通过|`TTM:OK`|
|`TTM:ADD-?`|通过|返回当前附加广播内容|
|`TTM:ADD-ABC`|通过|`TTM:OK`|
|`TTM:UNKNOWN`|通过|`TTM:ERP`|

## 闭环关联验证

macOS 闭环程序还验证了已连接状态下的 `TTM:CIT-100ms`，结果为 `TTM:OK`；其余 BLE 与 UART 透传结果见 [macos_ble_test_record.md](../function_test/macos_ble_test/macos_ble_test_record.md)。

## 需人工执行的安全项目

|命令|自动化状态|原因与判定|
|---|---|---|
|`TTM:RST-SYSTEMRESET`|未执行|会断开当前串口和 BLE 链路。人工发送后应先收到 `TTM:OK`，随后确认设备重启、GPIO8 闪烁和启动日志正常。|
|`TTM:MAC-xxxxxxxxxxxx`|未执行|会永久修改身份地址。人工设置后复位，并扫描或查询确认新 MAC；操作前应记录原地址。|

## 注意事项

- 配置类命令应在未连接 BLE 时发送；连接中设置受限制的参数会返回 `TTM:ERP`。
- `BPS` 在发送 `TTM:OK` 后才切换波特率，且掉电不保存。
- `CDL` 参数可保存，但本开发板未引出 BCTS，不能验证 BCTS 到 TX 的硬件延时。
- `TPL` 的协议响应已通过；ESP32-C3 当前控制器不支持运行时发射功率切换，实际功率需使用目标平台接口和射频仪表验证。
