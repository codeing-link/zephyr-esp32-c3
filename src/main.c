/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <string.h>

#include "ttm_protocol.h"

#define BLINK_INTERVAL_MS 1000
#define STARTUP_WAIT_MS 2000
#define LED0_NODE DT_ALIAS(led0)
#define UART_NODE DT_NODELABEL(uart0)

LOG_MODULE_REGISTER(gpio8_blinky, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "未定义 led0 别名；请检查板级 overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *const data_uart = DEVICE_DT_GET(UART_NODE);
static struct ttm_config ttm_config;
static struct bt_conn *ble_connection;
static bool notify_enabled;
static uint8_t uart_line[64];
static size_t uart_line_len;
extern const struct bt_gatt_service_static ttm_service;

static struct bt_uuid_16 ttm_service_uuid = BT_UUID_INIT_16(0xFFF0);
static struct bt_uuid_16 ble_data_uuid = BT_UUID_INIT_16(0xFFF3);
static struct bt_uuid_16 uart_data_uuid = BT_UUID_INIT_16(0xFFF4);

static void uart_send(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(data_uart, data[i]);
	}
}

static void ble_send(const uint8_t *data, size_t len)
{
	if (ble_connection != NULL && notify_enabled) {
		bt_gatt_notify(ble_connection, &ttm_service.attrs[4], data, len);
	}
}

static bool ble_connected(void) { return ble_connection != NULL; }

static int set_uart_baudrate(uint32_t baudrate)
{
	struct uart_config uart_config;
	int ret = uart_config_get(data_uart, &uart_config);

	if (ret == 0) {
		uart_config.baudrate = baudrate;
		ret = uart_configure(data_uart, &uart_config);
	}
	return ret;
}

static int set_tx_power(int8_t dbm)
{
	ARG_UNUSED(dbm);
	/* ESP32-C3 的运行时功率设置需使用厂商接口，移植时在此处实现。 */
	return 0;
}

static int restart_advertising(const struct ttm_config *config)
{
	struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
		BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xf0, 0xff),
	};
	struct bt_data sd[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, config->name, strlen(config->name)),
		BT_DATA(BT_DATA_MANUFACTURER_DATA, config->add_data, config->add_len),
	};
	struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN,
		BT_GAP_MS_TO_ADV_INTERVAL(config->adv_interval_ms),
		BT_GAP_MS_TO_ADV_INTERVAL(config->adv_interval_ms), NULL);

	bt_le_adv_stop();
	return bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd,
		config->add_len == 0 ? 1 : ARRAY_SIZE(sd));
}

static int set_connection_interval(uint16_t interval_ms)
{
	struct bt_le_conn_param param = BT_LE_CONN_PARAM_INIT(
		(interval_ms * 4) / 5, (interval_ms * 4) / 5, 0, 400);

	return bt_conn_le_param_update(ble_connection, &param);
}

static void save_config(const struct ttm_config *config)
{
	ARG_UNUSED(config);
	/* 平台适配层可改为 Settings、NVS 或其他非易失存储。 */
}

static void reset_system(void) { sys_reboot(SYS_REBOOT_COLD); }

static const struct ttm_port ttm_port = {
	.uart_send = uart_send,
	.ble_send = ble_send,
	.connected = ble_connected,
	.set_baudrate = set_uart_baudrate,
	.set_tx_power = set_tx_power,
	.restart_advertising = restart_advertising,
	.set_connection_interval = set_connection_interval,
	.save_config = save_config,
	.reset_system = reset_system,
};

static ssize_t on_ble_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
	const void *buffer, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn); ARG_UNUSED(attr); ARG_UNUSED(flags);
	if (offset != 0) { return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET); }
	ttm_protocol_on_ble(buffer, len);
	return len;
}

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = value == BT_GATT_CCC_NOTIFY;
}

BT_GATT_SERVICE_DEFINE(ttm_service,
	BT_GATT_PRIMARY_SERVICE(&ttm_service_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&ble_data_uuid.uuid,
		BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		BT_GATT_PERM_WRITE, NULL, on_ble_write, NULL),
	BT_GATT_CHARACTERISTIC(&uart_data_uuid.uuid, BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err == 0) { ble_connection = bt_conn_ref(conn); uart_send((const uint8_t *)"TTM:OK\r\n", 8); }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn); ARG_UNUSED(reason);
	if (ble_connection != NULL) { bt_conn_unref(ble_connection); ble_connection = NULL; }
	notify_enabled = false;
	uart_send((const uint8_t *)"TTM:DISCONNET\r\n", 14);
	restart_advertising(&ttm_config);
}

BT_CONN_CB_DEFINE(ttm_connection_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

static void on_uart_rx(const struct device *device, void *user_data)
{
	uint8_t byte;
	ARG_UNUSED(user_data);
	uart_irq_update(device);
	while (uart_irq_rx_ready(device) && uart_fifo_read(device, &byte, 1) == 1) {
		if (byte == '\r' || byte == '\n') {
			if (uart_line_len > 0) { ttm_protocol_on_uart(uart_line, uart_line_len); uart_line_len = 0; }
		} else if (uart_line_len < sizeof(uart_line)) { uart_line[uart_line_len++] = byte; }
	}
}

int main(void)
{
	int ret;

	/*
	 * RST 会使 USB Serial/JTAG 断开约 1 秒。延后应用初始化和首条日志，
	 * 让主机侧监听程序有足够时间重新打开 /dev/ttyACM0，避免丢失启动日志。
	 */
	k_msleep(STARTUP_WAIT_MS);
	LOG_INF("应用启动延时结束，开始初始化 GPIO8 状态灯");

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO8 控制器未就绪，应用停止运行");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("GPIO8 配置为输出失败：%d", ret);
		return 0;
	}
	LOG_INF("GPIO8 初始化完成，开始每 %d ms 翻转一次", BLINK_INTERVAL_MS);

	/* 蓝牙和 UART 初始化在状态灯确认正常后进行。 */
	ttm_protocol_defaults(&ttm_config, (const uint8_t[6]){0, 0, 0, 0, 0, 0});
	ttm_protocol_init(&ttm_config, &ttm_port);
	if (bt_enable(NULL) == 0) {
		bt_set_name(ttm_config.name);
		restart_advertising(&ttm_config);
		LOG_INF("BLE 透传服务已启动：FFF0/FFF3/FFF4");
	} else {
		LOG_ERR("蓝牙控制器初始化失败");
	}
	uart_irq_callback_user_data_set(data_uart, on_uart_rx, NULL);
	uart_irq_rx_enable(data_uart);

	while (true) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			LOG_ERR("GPIO8 翻转失败：%d，应用停止运行", ret);
			return 0;
		}

		k_msleep(BLINK_INTERVAL_MS);
	}
}
