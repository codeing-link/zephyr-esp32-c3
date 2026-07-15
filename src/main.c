/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define BLINK_INTERVAL_MS 1000
#define STARTUP_WAIT_MS 2000
#define LED0_NODE DT_ALIAS(led0)

LOG_MODULE_REGISTER(gpio8_blinky, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "未定义 led0 别名；请检查板级 overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

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

	while (true) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			LOG_ERR("GPIO8 翻转失败：%d，应用停止运行", ret);
			return 0;
		}

		k_msleep(BLINK_INTERVAL_MS);
	}
}
