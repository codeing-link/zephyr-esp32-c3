/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <errno.h>
#include <string.h>

#include "ttm_protocol.h"

#define BLINK_INTERVAL_MS 1000
#define STARTUP_WAIT_MS 2000
#define UART_PACKET_MAX_LEN 160
#define LED0_NODE DT_ALIAS(led0)
#define UART_NODE DT_NODELABEL(uart0)

LOG_MODULE_REGISTER(gpio8_blinky, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "未定义 led0 别名；请检查板级 overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *const data_uart = DEVICE_DT_GET(UART_NODE);
static struct ttm_config ttm_config;
static bool ttm_config_loaded;
static bool ttm_config_invalid;
static struct bt_conn *ble_connection;
static bool notify_enabled;
static uint16_t active_adv_interval_ms;
/* 153 字节 ATT MTU 对应最大 150 字节有效载荷，预留换行符和少量余量。 */
static uint8_t uart_line[UART_PACKET_MAX_LEN];
static size_t uart_line_len;
static uint8_t uart_packet[UART_PACKET_MAX_LEN];
static size_t uart_packet_len;
extern const struct bt_gatt_service_static ttm_service;
static int restart_advertising(const struct ttm_config *config);

static struct bt_uuid_16 ttm_service_uuid = BT_UUID_INIT_16(0xFFF0);
static struct bt_uuid_16 ble_data_uuid = BT_UUID_INIT_16(0xFFF3);
static struct bt_uuid_16 uart_data_uuid = BT_UUID_INIT_16(0xFFF4);

static int load_ttm_config(const char *name, size_t len, settings_read_cb read_cb,
	void *cb_arg)
{
	if (strcmp(name, "config") != 0 || len != sizeof(ttm_config)) {
		/* 旧固件的结构体大小不同，忽略旧记录并保留出厂默认参数。 */
		LOG_INF("发现不兼容的 AT 配置，长度为 %u，将恢复默认参数", (unsigned int)len);
		ttm_config_invalid = true;
		return 0;
	}
	if (read_cb(cb_arg, &ttm_config, sizeof(ttm_config)) != 0) {
		/* NVS 中存在损坏记录时不阻止应用启动，后续保存会覆盖该记录。 */
		LOG_INF("发现损坏的 AT 配置，将恢复默认参数");
		ttm_config_invalid = true;
		return 0;
	}
	ttm_config_loaded = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ttm, "ttm", NULL, load_ttm_config, NULL, NULL);

static void uart_send(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(data_uart, data[i]);
	}
}

static void ble_send(const uint8_t *data, size_t len)
{
	uint16_t max_payload;

	if (ble_connection != NULL && notify_enabled) {
		/* ATT 有效载荷为协商 MTU 减去 3 字节 ATT 头，默认恰好为 20 字节。 */
		max_payload = bt_gatt_get_mtu(ble_connection) - 3;
		while (len > 0) {
			size_t packet_len = MIN(len, max_payload);
			int ret = bt_gatt_notify(ble_connection, &ttm_service.attrs[4], data,
				packet_len);

			if (ret != 0) {
				LOG_WRN("BLE 通知发送失败：%d", ret);
				return;
			}
			data += packet_len;
			len -= packet_len;
		}
	}
}

static bool ble_connected(void) { return ble_connection != NULL; }

static int set_uart_baudrate(uint32_t baudrate)
{
	struct uart_config uart_config;
	int ret = uart_config_get(data_uart, &uart_config);

	if (ret != 0) {
		return ret;
	}
	/*
	 * BPS 命令已经用旧波特率发送了回显和 OK。相同波特率时不得重配，
	 * 否则 ESP32 UART 驱动会复位发送器，导致这些尚未移出的字节丢失。
	 */
	if (uart_config.baudrate == baudrate) {
		return 0;
	}

	/* 最低支持 2400 bps；等待 64 字节在旧波特率下完全发送。 */
	k_msleep(300);
	uart_config.baudrate = baudrate;
	return uart_configure(data_uart, &uart_config);
}

static int set_tx_power(int8_t dbm)
{
	/* ESP32-C3 当前控制器的射频功率由构建配置决定，未开放运行时设置接口。 */
	LOG_INF("请求设置发射功率 %d dBm，当前控制器使用构建时功率配置", dbm);
	return 0;
}

static int restart_advertising(const struct ttm_config *config)
{
	int ret;
	uint8_t manufacturer_data[] = {
		0xff, 0xff,
		(uint8_t)(config->product_id & 0xff),
		(uint8_t)(config->product_id >> 8),
	};
	struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
		BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xf0, 0xff),
		BT_DATA(BT_DATA_MANUFACTURER_DATA, manufacturer_data, sizeof(manufacturer_data)),
	};
	struct bt_data sd[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, config->name, strlen(config->name)),
		BT_DATA(BT_DATA_MANUFACTURER_DATA, config->add_data, config->add_len),
	};
	struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN,
		BT_GAP_MS_TO_ADV_INTERVAL(config->adv_interval_ms),
		BT_GAP_MS_TO_ADV_INTERVAL(config->adv_interval_ms), NULL);

	/* 名称、附加数据和产品识别码变更时无需停播，可避免控制器资源切换失败。 */
	if (active_adv_interval_ms == config->adv_interval_ms) {
		ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd,
			config->add_len == 0 ? 1 : ARRAY_SIZE(sd));
		if (ret != 0) {
			LOG_ERR("更新广播数据失败：%d", ret);
		}
		return ret;
	}

	ret = bt_le_adv_stop();
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("停止旧广播失败：%d", ret);
		return ret;
	}
	/* 可连接广播停止后，给控制器一点时间释放预分配的连接资源。 */
	k_msleep(20);
	ret = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd,
		config->add_len == 0 ? 1 : ARRAY_SIZE(sd));
	if (ret != 0) {
		LOG_ERR("启动广播失败：%d", ret);
	} else {
		active_adv_interval_ms = config->adv_interval_ms;
	}
	return ret;
}

static int set_connection_interval(uint16_t interval_ms)
{
	struct bt_le_conn_param param = BT_LE_CONN_PARAM_INIT(
		(interval_ms * 4) / 5, (interval_ms * 4) / 5, 0, 400);

	return bt_conn_le_param_update(ble_connection, &param);
}

static void restart_advertising_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	restart_advertising(&ttm_config);
}

K_WORK_DELAYABLE_DEFINE(advertising_restart_work, restart_advertising_work_handler);

static void save_config(const struct ttm_config *config)
{
	int ret = settings_save_one("ttm/config", config, sizeof(*config));

	if (ret != 0) {
		LOG_ERR("保存 AT 配置失败：%d", ret);
	}
}

static void reset_system(void) { sys_reboot(SYS_REBOOT_COLD); }

static int set_mac(const uint8_t mac[6])
{
	ARG_UNUSED(mac);
	/* 蓝牙身份地址仅可在 bt_enable() 前创建，已保存的地址会在下次复位时生效。 */
	LOG_INF("自定义 MAC 已保存，将在模块复位后生效");
	return 0;
}

static void disconnect_ble(void)
{
	if (ble_connection != NULL) {
		bt_conn_disconnect(ble_connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static const struct ttm_port ttm_port = {
	.uart_send = uart_send,
	.ble_send = ble_send,
	.connected = ble_connected,
	.set_baudrate = set_uart_baudrate,
	.set_tx_power = set_tx_power,
	.restart_advertising = restart_advertising,
	.set_connection_interval = set_connection_interval,
	.set_mac = set_mac,
	.disconnect_ble = disconnect_ble,
	.save_config = save_config,
	.reset_system = reset_system,
};

static ssize_t on_ble_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
	const void *buffer, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	/* macOS 对超过当前 ATT MTU 的有响应写入会使用 Prepare/Execute Write。 */
	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}
	if (offset != 0 && !(flags & BT_GATT_WRITE_FLAG_EXECUTE)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
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
		BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE, NULL, on_ble_write, NULL),
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
	active_adv_interval_ms = 0;
	/* 控制器释放连接资源需要一点时间，不能在断连回调里立即启动广播。 */
	k_work_reschedule(&advertising_restart_work, K_MSEC(200));
}

BT_CONN_CB_DEFINE(ttm_connection_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

static void process_uart_packet(struct k_work *work)
{
	ARG_UNUSED(work);

	/* 蓝牙协议栈调用在工作队列上下文执行，不能直接在 UART 中断中执行。 */
	ttm_protocol_on_uart(uart_packet, uart_packet_len);
}

K_WORK_DEFINE(uart_packet_work, process_uart_packet);

static void on_uart_rx(const struct device *device, void *user_data)
{
	uint8_t byte;
	ARG_UNUSED(user_data);
	uart_irq_update(device);
	while (uart_irq_rx_ready(device) && uart_fifo_read(device, &byte, 1) == 1) {
		if (byte == '\r' || byte == '\n') {
			if (uart_line_len > 0) {
				memcpy(uart_packet, uart_line, uart_line_len);
				uart_packet_len = uart_line_len;
				uart_line_len = 0;
				k_work_submit(&uart_packet_work);
			}
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
	/* Settings 不会由应用自动初始化；先挂载 NVS 后才能读写 AT 配置。 */
	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("初始化 AT 配置存储失败：%d", ret);
	} else {
		ret = settings_load();
	}
	if (ret != 0) {
		LOG_WRN("读取已保存 AT 配置失败：%d，将使用出厂默认值", ret);
	}
	if (ttm_config_invalid) {
		ret = settings_delete("ttm/config");
		if (ret == 0) {
			LOG_INF("已清除不兼容的 AT 配置记录");
		} else {
			LOG_WRN("清除不兼容的 AT 配置记录失败：%d", ret);
		}
	}
	if (ttm_config.custom_mac) {
		bt_addr_le_t custom_address = {
			.type = BT_ADDR_LE_RANDOM,
		};

		memcpy(custom_address.a.val, ttm_config.mac, sizeof(ttm_config.mac));
		ret = bt_id_create(&custom_address, NULL);
		if (ret < 0) {
			LOG_ERR("创建自定义蓝牙地址失败：%d", ret);
		}
	}
	ttm_protocol_init(&ttm_config, &ttm_port);
	if (bt_enable(NULL) == 0) {
		bt_addr_le_t address;
		size_t address_count = 1;

		bt_id_get(&address, &address_count);
		if (!ttm_config_loaded && address_count > 0) {
			memcpy(ttm_config.mac, address.a.val, sizeof(ttm_config.mac));
			snprintf(ttm_config.name, sizeof(ttm_config.name), "LSD-BLE-%02X%02X%02X",
				ttm_config.mac[2], ttm_config.mac[1], ttm_config.mac[0]);
		}
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
