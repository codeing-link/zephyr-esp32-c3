/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TTM_PROTOCOL_H
#define TTM_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTM_NAME_MAX_LEN 20
#define TTM_ADD_MAX_LEN 16

/* 此文件仅定义应用协议，不依赖 Zephyr 或任何芯片 SDK。 */
struct ttm_config {
	uint32_t baudrate;
	uint16_t delay_ms;
	int8_t tx_power_dbm;
	uint16_t adv_interval_ms;
	uint16_t product_id;
	char name[TTM_NAME_MAX_LEN + 1];
	uint8_t mac[6];
	bool custom_mac;
	uint8_t add_data[TTM_ADD_MAX_LEN];
	uint8_t add_len;
};

/* 后续移植时由新平台实现这些回调。 */
struct ttm_port {
	void *context;
	void (*uart_send)(void *context, const uint8_t *data, size_t len);
	void (*ble_send)(void *context, const uint8_t *data, size_t len);
	bool (*connected)(void *context);
	int (*set_baudrate)(void *context, uint32_t baudrate);
	int (*set_tx_power)(void *context, int8_t dbm);
	int (*restart_advertising)(void *context, const struct ttm_config *config);
	int (*set_connection_interval)(void *context, uint16_t interval_ms);
	int (*set_mac)(void *context, const uint8_t mac[6]);
	void (*disconnect_ble)(void *context);
	void (*save_config)(void *context, const struct ttm_config *config);
	void (*reset_system)(void *context);
};

/* 每个协议实例独立保存状态，可被不同平台或测试程序同时使用。 */
struct ttm_protocol {
	struct ttm_config *config;
	const struct ttm_port *port;
};

void ttm_protocol_defaults(struct ttm_config *config, const uint8_t mac[6]);
void ttm_protocol_init(struct ttm_protocol *protocol, struct ttm_config *config,
	const struct ttm_port *port);
void ttm_protocol_on_uart(struct ttm_protocol *protocol, const uint8_t *data, size_t len);
void ttm_protocol_on_ble(struct ttm_protocol *protocol, const uint8_t *data, size_t len);

#endif
