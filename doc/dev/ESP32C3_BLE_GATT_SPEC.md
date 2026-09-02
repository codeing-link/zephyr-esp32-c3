# ESP32C3 BLE GATT 与一键配对规格

## 一键配对逻辑

ESP32C3 不需要做系统级蓝牙绑定，只需要做 BLE GATT 连接。

量产推荐逻辑：

1. 默认状态不广播，避免小程序误连旧设备。
2. 用户短按配对按键。
3. ESP32C3 点亮或闪烁状态灯，开始广播。
4. 广播持续 60 秒。
5. 小程序点击“一键配对”后扫描 BLE 设备。
6. 小程序按设备名或 Service UUID 命中 ESP32C3。
7. 小程序连接成功后，ESP32C3 退出配对广播窗口。
8. BLE 断开后回到 IDLE，用户需要再次按键才能重新进入配对。

如果需要更方便的开发调试，可以临时改成上电后一直广播。

当前固件处于开发联调阶段，采用上电后自动广播、断开后自动重新广播。等目标板按键 GPIO 定下来后，再把广播入口改成短按按键触发 60 秒窗口即可。

## 广播数据

当前固件广播包含：

```text
Complete Local Name: E-Badge-C3
Service UUID: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
Connectable: true
```

设备名建议包含小程序默认匹配关键字之一：

```text
E-Badge
Monokaro
ESP32
Badge
BLE
```

当前小程序即使不知道 Service UUID，也会优先连接名称匹配的设备。量产时建议固定 UUID，并填入小程序 `pages/index/index.js` 的 `bleConfig`。

## GATT 服务

当前固件定义一个自定义主服务：

```text
Primary Service UUID:
  6e400001-b5a3-f393-e0a9-e50e24dcca9e
```

可以使用 Nordic UART Service UUID，也可以换成自定义 UUID。关键是小程序和固件保持一致。

## 特征定义

### RX Write 特征

小程序写入，ESP32C3 接收。

```text
Characteristic UUID:
  6e400002-b5a3-f393-e0a9-e50e24dcca9e

Properties:
  Write Without Response
  Write
```

建议两个写属性都打开。小程序会自动优先用 `writeNoResponse`，如果没有则使用 `write`。

### TX Notify 特征

ESP32C3 通知小程序。

```text
Characteristic UUID:
  6e400003-b5a3-f393-e0a9-e50e24dcca9e

Properties:
  Notify
```

小程序连接后会调用 `notifyBLECharacteristicValueChange` 打开通知。ESP32C3 只有在 notify enabled 后再发送 ACK/NACK。

## 小程序 UUID 配置位置

在 `pages/index/index.js`：

```js
const bleConfig = {
  ...DEFAULT_CONFIG,
  serviceUuid: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
  writeCharacteristicUuid: '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
  notifyCharacteristicUuid: '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
}
```

如果暂时不填 UUID，小程序会自动寻找“可写入 + 可通知”的特征，适合早期联调。

## MTU

小程序会请求 `247` MTU：

```text
ATT payload = MTU - 3
247 MTU -> 单次 BLE 写入最多 244 字节
```

小程序业务层 `DATA` payload 默认是 `240` 字节，但一个完整 `JPGU DATA` 包还包含包头和 CRC，所以 BLE 层仍可能拆成多个 ATT 写入。ESP32C3 必须保持所有 BLE 写入片段的顺序并连续写到 UART。

iOS 小程序不支持主动设置 MTU，常见情况下单次写入会退回 20 字节。因此 ESP32C3 不能假设一次 BLE 写入就是一个完整业务包。

## 连接事件处理

连接成功：

- 停止配对计时。
- 记录 conn_id。
- 准备接收写特征数据。
- 等待小程序打开 notify。

断开连接：

- 停止当前传输。
- 清空 BLE->UART 缓冲。
- 清空 UART->BLE ACK 缓冲。
- 回到 IDLE。

按键再次按下：

- 如果未连接，重新进入 PAIRING。
- 如果已连接，可忽略，或作为主动断开连接按键，二选一即可。

## 与 OTA 共存

当前固件仍保留 Zephyr MCUmgr/SMP over BLE OTA 服务：

```text
8d53dc1d-1db7-4cd3-868b-8a527460aa84
```

小程序图片服务和 OTA 服务可以同时存在，但同一时间只允许一个 BLE 连接。进行 OTA 前请断开小程序；进行图片下发前请断开 OTA 工具。
