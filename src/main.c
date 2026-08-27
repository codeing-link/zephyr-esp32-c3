/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>

#include <errno.h>

#define BLINK_INTERVAL_MS CONFIG_APP_BLINK_INTERVAL_MS
#define BLE_DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define STARTUP_WAIT_MS 2000
#define LED0_NODE DT_ALIAS(led0)

LOG_MODULE_REGISTER(esp32c3_ota_blinky, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "未定义 led0 别名；请检查板级 overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEVICE_NAME, sizeof(BLE_DEVICE_NAME) - 1),
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
		LOG_ERR("启动 BLE OTA 广播失败：%d", ret);
		return;
	}

	LOG_INF("BLE OTA 广播已启动，名称：%s", BLE_DEVICE_NAME);
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
	ARG_UNUSED(conn);

	if (err != 0) {
		LOG_WRN("BLE 连接失败：%u", err);
		k_work_reschedule(&advertising_restart_work, K_MSEC(200));
		return;
	}

	LOG_INF("BLE 已连接");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("BLE 已断开：%u", reason);
	k_work_reschedule(&advertising_restart_work, K_MSEC(200));
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

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
