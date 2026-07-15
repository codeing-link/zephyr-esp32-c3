/* SPDX-License-Identifier: Apache-2.0 */
#include "ttm_protocol.h"

#include <stdio.h>
#include <string.h>

static void send_bytes(const struct ttm_protocol *protocol, const uint8_t *data, size_t len)
{
	protocol->port->uart_send(protocol->port->context, data, len);
}

static void send_text(const struct ttm_protocol *protocol, const char *text)
{
	send_bytes(protocol, (const uint8_t *)text, strlen(text));
}

static void send_ok(const struct ttm_protocol *protocol)
{
	send_text(protocol, "TTM:OK\r\n");
}

static void send_error(const struct ttm_protocol *protocol)
{
	send_text(protocol, "TTM:ERP\r\n");
}

static bool is_idle(const struct ttm_protocol *protocol)
{
	return !protocol->port->connected(protocol->port->context);
}

static bool is_allowed(uint32_t value, const uint32_t *values, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (value == values[i]) {
			return true;
		}
	}

	return false;
}

static int hex_value(char character)
{
	if (character >= '0' && character <= '9') {
		return character - '0';
	}
	if (character >= 'A' && character <= 'F') {
		return character - 'A' + 10;
	}
	if (character >= 'a' && character <= 'f') {
		return character - 'a' + 10;
	}

	return -1;
}

static bool parse_hex_byte(const char *text, uint8_t *value)
{
	int high = hex_value(text[0]);
	int low = hex_value(text[1]);

	if (high < 0 || low < 0) {
		return false;
	}
	*value = (uint8_t)((high << 4) | low);
	return true;
}

static bool parse_product_id(const char *text, uint16_t *product_id)
{
	uint8_t high;
	uint8_t low;

	if (strlen(text) != 4 || !parse_hex_byte(text, &high) ||
	    !parse_hex_byte(text + 2, &low)) {
		return false;
	}
	*product_id = ((uint16_t)high << 8) | low;
	return true;
}

static bool parse_mac(const char *text, uint8_t mac[6])
{
	if (strlen(text) != 12) {
		return false;
	}

	/* AT 命令按正常显示顺序输入，Zephyr 地址数组按小端顺序保存。 */
	for (size_t i = 0; i < 6; i++) {
		if (!parse_hex_byte(text + i * 2, &mac[5 - i])) {
			return false;
		}
	}

	return true;
}

static int apply_advertising_config(struct ttm_protocol *protocol,
	const struct ttm_config *candidate)
{
	int ret = protocol->port->restart_advertising(protocol->port->context, candidate);

	if (ret == 0) {
		*protocol->config = *candidate;
		protocol->port->save_config(protocol->port->context, protocol->config);
	}

	return ret;
}

static void reply_name(const struct ttm_protocol *protocol)
{
	char reply[sizeof(protocol->config->name) + 10];

	snprintf(reply, sizeof(reply), "TTM:REN-%s\r\n", protocol->config->name);
	send_text(protocol, reply);
}

static void reply_mac(const struct ttm_protocol *protocol)
{
	char reply[32];

	snprintf(reply, sizeof(reply), "TTM:MAC-%02X%02X%02X%02X%02X%02X\r\n",
		 protocol->config->mac[5], protocol->config->mac[4], protocol->config->mac[3],
		 protocol->config->mac[2], protocol->config->mac[1], protocol->config->mac[0]);
	send_text(protocol, reply);
}

static void reply_add(const struct ttm_protocol *protocol)
{
	static const char prefix[] = "TTM:ADD-";

	send_text(protocol, prefix);
	send_bytes(protocol, protocol->config->add_data, protocol->config->add_len);
	send_text(protocol, "\r\n");
}

void ttm_protocol_defaults(struct ttm_config *config, const uint8_t mac[6])
{
	memset(config, 0, sizeof(*config));
	config->baudrate = 115200;
	config->tx_power_dbm = 4;
	/* 原始模块出厂广播间隔为 20 ms。 */
	config->adv_interval_ms = 20;
	memcpy(config->mac, mac, sizeof(config->mac));
	snprintf(config->name, sizeof(config->name), "LSD-BLE-%02X%02X%02X",
		 mac[2], mac[1], mac[0]);
}

void ttm_protocol_init(struct ttm_protocol *protocol, struct ttm_config *config,
	const struct ttm_port *port)
{
	protocol->config = config;
	protocol->port = port;
}

void ttm_protocol_on_ble(struct ttm_protocol *protocol, const uint8_t *data, size_t len)
{
	protocol->port->uart_send(protocol->port->context, data, len);
}

void ttm_protocol_on_uart(struct ttm_protocol *protocol, const uint8_t *data, size_t len)
{
	char command[64];
	uint32_t value;
	int used;

	if (len < 3 || memcmp(data, "TTM", 3) != 0) {
		protocol->port->ble_send(protocol->port->context, data, len);
		return;
	}
	if (len >= sizeof(command)) {
		send_error(protocol);
		return;
	}

	memcpy(command, data, len);
	command[len] = '\0';
	while (len > 0 && (command[len - 1] == '\r' || command[len - 1] == '\n')) {
		command[--len] = '\0';
	}
	/* 所有 AT 指令均先回显，再给出执行结果。 */
	send_bytes(protocol, (const uint8_t *)command, len);
	/* 原始协议的 PID 参数是两个二进制字节，保留这一兼容格式。 */
	if (len == 10 && memcmp(data, "TTM:PID-", 8) == 0) {
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol)) {
			send_error(protocol);
			return;
		}
		candidate.product_id = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
		if (apply_advertising_config(protocol, &candidate) != 0) {
			send_error(protocol);
			return;
		}
		send_ok(protocol);
		return;
	}

	if (strcmp(command, "TTM:VID-?") == 0) {
		send_text(protocol, "TTM:VID-LSD_BLE_5.0\r\n");
		return;
	}
	if (strcmp(command, "TTM:REV-?") == 0) {
		send_text(protocol, "TTM:REV-V2.1\r\n");
		return;
	}
	if (strcmp(command, "TTM:REN-?") == 0) {
		reply_name(protocol);
		return;
	}
	if (strcmp(command, "TTM:MAC-?") == 0) {
		reply_mac(protocol);
		return;
	}
	if (strcmp(command, "TTM:ADD-?") == 0) {
		reply_add(protocol);
		return;
	}
	if (strcmp(command, "TTM:RST-SYSTEMRESET") == 0) {
		send_ok(protocol);
		protocol->port->reset_system(protocol->port->context);
		return;
	}

	if (sscanf(command, "TTM:BPS-%u%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {2400, 4800, 9600, 14400, 19200,
			28800, 38400, 57600, 76800, 115200};

		if (!is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) {
			send_error(protocol);
			return;
		}
		/* 先以旧波特率发送确认，再切换到新波特率。 */
		send_ok(protocol);
		protocol->config->baudrate = value;
		protocol->port->set_baudrate(protocol->port->context, value);
		return;
	}

	if (sscanf(command, "TTM:CDL-%ums%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {0, 2, 5, 10, 15, 20, 25};

		if (!is_idle(protocol) || !is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) {
			send_error(protocol);
			return;
		}
		protocol->config->delay_ms = value;
		protocol->port->save_config(protocol->port->context, protocol->config);
		send_ok(protocol);
		return;
	}

	if (strncmp(command, "TTM:TPL-", 8) == 0) {
		const char *power = command + 8;
		int8_t dbm;

		if (strcmp(power, "+4") == 0) {
			dbm = 4;
		} else if (strcmp(power, "0") == 0) {
			dbm = 0;
		} else if (strcmp(power, "-6") == 0) {
			dbm = -6;
		} else if (strcmp(power, "-23") == 0) {
			dbm = -23;
		} else {
			send_error(protocol);
			return;
		}
		if (protocol->port->set_tx_power(protocol->port->context, dbm) != 0) {
			send_error(protocol);
			return;
		}
		protocol->config->tx_power_dbm = dbm;
		if (protocol->port->connected(protocol->port->context)) {
			protocol->port->disconnect_ble(protocol->port->context);
		}
		send_ok(protocol);
		return;
	}

	if (sscanf(command, "TTM:ADP-%u%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {2, 5, 10, 15, 20, 25, 30, 40, 50};
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol) || !is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) {
			send_error(protocol);
			return;
		}
		candidate.adv_interval_ms = value * 100;
		if (apply_advertising_config(protocol, &candidate) != 0) {
			send_error(protocol);
			return;
		}
		send_ok(protocol);
		return;
	}

	if (strncmp(command, "TTM:PID-", 8) == 0) {
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol) || !parse_product_id(command + 8, &candidate.product_id)) {
			send_error(protocol);
			return;
		}
		if (apply_advertising_config(protocol, &candidate) != 0) {
			send_error(protocol);
			return;
		}
		send_ok(protocol);
		return;
	}

	if (strncmp(command, "TTM:REN-", 8) == 0) {
		const char *name = command + 8;
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol) || strlen(name) == 0 || strlen(name) > TTM_NAME_MAX_LEN) {
			send_error(protocol);
			return;
		}
		strcpy(candidate.name, name);
		if (apply_advertising_config(protocol, &candidate) != 0) {
			send_error(protocol);
			return;
		}
		return send_ok(protocol);
	}

	if (strncmp(command, "TTM:MAC-", 8) == 0) {
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol) || !parse_mac(command + 8, candidate.mac)) {
			send_error(protocol);
			return;
		}
		candidate.custom_mac = true;
		if (protocol->port->set_mac(protocol->port->context, candidate.mac) != 0) {
			send_error(protocol);
			return;
		}
		*protocol->config = candidate;
		protocol->port->save_config(protocol->port->context, protocol->config);
		send_ok(protocol);
		return;
	}

	if (strncmp(command, "TTM:ADD-", 8) == 0) {
		const char *add = command + 8;
		size_t add_len = strlen(add);
		struct ttm_config candidate = *protocol->config;

		if (!is_idle(protocol) || add_len == 0 || add_len > TTM_ADD_MAX_LEN) {
			send_error(protocol);
			return;
		}
		memcpy(candidate.add_data, add, add_len);
		candidate.add_len = add_len;
		if (apply_advertising_config(protocol, &candidate) != 0) {
			send_error(protocol);
			return;
		}
		send_ok(protocol);
		return;
	}

	if (sscanf(command, "TTM:CIT-%ums%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {20, 50, 100, 200, 300, 400, 500,
			1000, 1500, 2000};

		if (!protocol->port->connected(protocol->port->context) || !is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) {
			send_error(protocol);
			return;
		}
		if (protocol->port->set_connection_interval(protocol->port->context, value) != 0) {
			send_text(protocol, "TTM:TIMEOUT\r\n");
			return;
		}
		send_ok(protocol);
		return;
	}

	send_error(protocol);
}
