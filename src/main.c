/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

#define BLINK_INTERVAL_MS CONFIG_APP_BLINK_INTERVAL_MS
#define BLE_DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define BRIDGE_UART_NODE DT_NODELABEL(uart1)
#define STARTUP_WAIT_MS 2000
#define LED0_NODE DT_ALIAS(led0)
#define UART_TX_RING_SIZE 8192
#define UART_TX_CHUNK_SIZE 128
#define JPGU_ACK_LEN 13
#define JPGU_ACK_QUEUE_LEN 8
#define JPGU_MAGIC "JPGU"
#define JPGU_VERSION 0x01
#define JPGU_CMD_ACK 0x80
#define JPGU_CMD_NACK 0x81

LOG_MODULE_REGISTER(esp32c3_bridge, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "未定义 led0 别名；请检查板级 overlay"
#endif

#if !DT_NODE_HAS_STATUS(BRIDGE_UART_NODE, okay)
#error "未启用 uart1；请检查板级 overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *const bridge_uart = DEVICE_DT_GET(BRIDGE_UART_NODE);
static struct bt_conn *current_conn;
static bool notify_enabled;

RING_BUF_DECLARE(uart_tx_ring, UART_TX_RING_SIZE);
K_MUTEX_DEFINE(uart_tx_lock);
K_SEM_DEFINE(uart_tx_sem, 0, UINT_MAX);
K_MSGQ_DEFINE(jpgu_ack_msgq, JPGU_ACK_LEN, JPGU_ACK_QUEUE_LEN, 4);

static struct k_work notify_ack_work;

static struct bt_uuid_128 bridge_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9,
					    0xe50e24dcca9e));
static struct bt_uuid_128 bridge_rx_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9,
					    0xe50e24dcca9e));
static struct bt_uuid_128 bridge_tx_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9,
					    0xe50e24dcca9e));

static ssize_t bridge_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags);
static void bridge_tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);

BT_GATT_SERVICE_DEFINE(bridge_svc,
	BT_GATT_PRIMARY_SERVICE(&bridge_svc_uuid),
	BT_GATT_CHARACTERISTIC(&bridge_rx_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, bridge_rx_write, NULL),
	BT_GATT_CHARACTERISTIC(&bridge_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(bridge_tx_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_128_ENCODE(0x6e400001, 0xb5a3,
							      0xf393, 0xe0a9,
							      0xe50e24dcca9e)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEVICE_NAME, sizeof(BLE_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static void start_advertising(void)
{
	int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
		ARRAY_SIZE(sd));

	if (ret == -EALREADY) {
		LOG_INF("BLE 广播已经启动");
		return;
	}
	if (ret != 0) {
		LOG_ERR("启动 BLE 广播失败：%d", ret);
		return;
	}

	LOG_INF("BLE 图片桥接广播已启动，名称：%s", BLE_DEVICE_NAME);
	LOG_INF("小程序服务 UUID：6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
	LOG_INF("SMP OTA 服务 UUID：8D53DC1D-1DB7-4CD3-868B-8A527460AA84");
}

static void restart_advertising_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	start_advertising();
}

K_WORK_DELAYABLE_DEFINE(advertising_restart_work, restart_advertising_work_handler);

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		LOG_WRN("BLE 连接失败：%u", err);
		k_work_reschedule(&advertising_restart_work, K_MSEC(200));
		return;
	}

	current_conn = bt_conn_ref(conn);
	notify_enabled = false;
	LOG_INF("BLE 已连接");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	notify_enabled = false;

	k_mutex_lock(&uart_tx_lock, K_FOREVER);
	ring_buf_reset(&uart_tx_ring);
	k_mutex_unlock(&uart_tx_lock);
	k_msgq_purge(&jpgu_ack_msgq);

	LOG_INF("BLE 已断开：%u", reason);
	k_work_reschedule(&advertising_restart_work, K_MSEC(200));
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

static void bridge_tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("小程序通知通道%s", notify_enabled ? "已开启" : "已关闭");
}

static ssize_t bridge_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags)
{
	uint32_t written;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	k_mutex_lock(&uart_tx_lock, K_FOREVER);
	written = ring_buf_put(&uart_tx_ring, buf, len);
	k_mutex_unlock(&uart_tx_lock);

	if (written != len) {
		LOG_WRN("UART 发送队列空间不足：收到 %u 字节，仅入队 %u 字节", len, written);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	k_sem_give(&uart_tx_sem);
	LOG_DBG("BLE 写入 %u 字节，已加入 UART 队列", len);
	return len;
}

static bool is_valid_jpgu_ack(const uint8_t frame[JPGU_ACK_LEN])
{
	return memcmp(frame, JPGU_MAGIC, strlen(JPGU_MAGIC)) == 0 &&
	       frame[4] == JPGU_VERSION &&
	       (frame[5] == JPGU_CMD_ACK || frame[5] == JPGU_CMD_NACK);
}

static void uart_ack_scanner_push(uint8_t byte)
{
	static uint8_t frame[JPGU_ACK_LEN];
	static uint8_t pos;

	if (pos < strlen(JPGU_MAGIC)) {
		if (byte == JPGU_MAGIC[pos]) {
			frame[pos++] = byte;
			return;
		}

		pos = (byte == JPGU_MAGIC[0]) ? 1U : 0U;
		frame[0] = byte;
		return;
	}

	frame[pos++] = byte;

	if (pos < JPGU_ACK_LEN) {
		return;
	}

	if (is_valid_jpgu_ack(frame)) {
		if (k_msgq_put(&jpgu_ack_msgq, frame, K_NO_WAIT) != 0) {
			LOG_WRN("ACK 通知队列已满，丢弃一帧 MCU 回复");
		} else {
			k_work_submit(&notify_ack_work);
		}
	} else {
		LOG_WRN("收到无效 JPGU ACK/NACK 帧，已丢弃");
	}

	pos = 0U;
}

static void bridge_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t rx_buf[32];

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) != 0 && uart_irq_is_pending(dev) != 0) {
		if (uart_irq_rx_ready(dev) == 0) {
			continue;
		}

		int read_len = uart_fifo_read(dev, rx_buf, sizeof(rx_buf));

		for (int i = 0; i < read_len; i++) {
			uart_ack_scanner_push(rx_buf[i]);
		}
	}
}

static void notify_ack_work_handler(struct k_work *work)
{
	uint8_t frame[JPGU_ACK_LEN];

	ARG_UNUSED(work);

	while (k_msgq_get(&jpgu_ack_msgq, frame, K_NO_WAIT) == 0) {
		if (current_conn == NULL || !notify_enabled) {
			LOG_WRN("小程序通知通道未就绪，丢弃一帧 MCU 回复");
			continue;
		}

		int ret = bt_gatt_notify(current_conn, &bridge_svc.attrs[4], frame,
					 sizeof(frame));

		if (ret != 0) {
			LOG_WRN("通知小程序 ACK/NACK 失败：%d", ret);
		} else {
			LOG_DBG("已通知小程序 JPGU ACK/NACK cmd=0x%02x status=%u", frame[5],
				frame[12]);
		}
	}
}

static void uart_tx_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t tx_buf[UART_TX_CHUNK_SIZE];

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_sem_take(&uart_tx_sem, K_FOREVER);

		while (true) {
			uint32_t len;

			k_mutex_lock(&uart_tx_lock, K_FOREVER);
			len = ring_buf_get(&uart_tx_ring, tx_buf, sizeof(tx_buf));
			k_mutex_unlock(&uart_tx_lock);

			if (len == 0U) {
				break;
			}

			for (uint32_t i = 0; i < len; i++) {
				uart_poll_out(bridge_uart, tx_buf[i]);
			}
		}
	}
}

K_THREAD_DEFINE(uart_tx_thread_id, 1536, uart_tx_thread, NULL, NULL, NULL, 7, 0, 0);

static void confirm_running_image(void)
{
	int ret;

	if (boot_is_img_confirmed()) {
		LOG_INF("当前 MCUboot 镜像已确认");
		return;
	}

	ret = boot_write_img_confirmed();
	if (ret == 0) {
		LOG_INF("当前 MCUboot 镜像确认成功");
	} else {
		LOG_WRN("当前 MCUboot 镜像确认失败：%d", ret);
	}
}

static int init_uart_bridge(void)
{
	int ret;

	if (!device_is_ready(bridge_uart)) {
		LOG_ERR("业务 UART1 未就绪");
		return -ENODEV;
	}

	k_work_init(&notify_ack_work, notify_ack_work_handler);
	ret = uart_irq_callback_user_data_set(bridge_uart, bridge_uart_isr, NULL);
	if (ret != 0) {
		LOG_ERR("设置业务 UART1 中断回调失败：%d", ret);
		return ret;
	}
	uart_irq_rx_enable(bridge_uart);

	LOG_INF("业务 UART1 已启动：GPIO21 TX、GPIO20 RX、115200 8N1");
	return 0;
}

int main(void)
{
	int ret;

	k_msleep(STARTUP_WAIT_MS);
	LOG_INF("应用启动，开始初始化 GPIO8 状态灯");

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
	confirm_running_image();

	ret = init_uart_bridge();
	if (ret != 0) {
		LOG_ERR("业务 UART 桥接初始化失败：%d", ret);
		return 0;
	}

	ret = bt_enable(NULL);
	if (ret != 0) {
		LOG_ERR("蓝牙控制器初始化失败：%d", ret);
	} else {
		start_advertising();
	}

	while (true) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			LOG_ERR("GPIO8 翻转失败：%d，应用停止运行", ret);
			return 0;
		}

		k_msleep(BLINK_INTERVAL_MS);
	}
}
