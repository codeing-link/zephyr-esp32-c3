/* SPDX-License-Identifier: Apache-2.0 */
#include "ttm_protocol.h"

#include <stdio.h>
#include <string.h>

static struct ttm_config *current_config;
static const struct ttm_port *current_port;

static void send_text(const char *text)
{
	current_port->uart_send((const uint8_t *)text, strlen(text));
}

static void send_ok(void) { send_text("TTM:OK\r\n"); }
static void send_error(void) { send_text("TTM:ERP\r\n"); }

static bool is_idle(void)
{
	return !current_port->connected();
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

void ttm_protocol_defaults(struct ttm_config *config, const uint8_t mac[6])
{
	memset(config, 0, sizeof(*config));
	config->baudrate = 115200;
	config->tx_power_dbm = 4;
	config->adv_interval_ms = 2000;
	snprintf(config->name, sizeof(config->name), "LSD-BLE-%02X%02X%02X",
		 mac[2], mac[1], mac[0]);
}

void ttm_protocol_init(struct ttm_config *config, const struct ttm_port *port)
{
	current_config = config;
	current_port = port;
}

void ttm_protocol_on_ble(const uint8_t *data, size_t len)
{
	current_port->uart_send(data, len);
}

void ttm_protocol_on_uart(const uint8_t *data, size_t len)
{
	char command[64];
	uint32_t value;
	int used;

	if (len < 3 || memcmp(data, "TTM", 3) != 0) {
		current_port->ble_send(data, len);
		return;
	}
	if (len >= sizeof(command)) {
		send_error();
		return;
	}

	memcpy(command, data, len);
	command[len] = '\0';
	while (len > 0 && (command[len - 1] == '\r' || command[len - 1] == '\n')) {
		command[--len] = '\0';
	}
	current_port->uart_send((const uint8_t *)command, len);

	if (strcmp(command, "TTM:VID-?") == 0) { send_text("TTM:VID-LSD_BLE_5.0\r\n"); return; }
	if (strcmp(command, "TTM:REV-?") == 0) { send_text("TTM:REV-V2.1\r\n"); return; }
	if (strcmp(command, "TTM:REN-?") == 0) { snprintf(command, sizeof(command), "TTM:REN-%s\r\n", current_config->name); send_text(command); return; }
	if (strcmp(command, "TTM:ADD-?") == 0) { send_text("TTM:ADD-?\r\n"); return; }
	if (strcmp(command, "TTM:RST-SYSTEMRESET") == 0) { send_ok(); current_port->reset_system(); return; }

	if (sscanf(command, "TTM:BPS-%u%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {2400,4800,9600,14400,19200,28800,38400,57600,76800,115200};
		if (!is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) { send_error(); return; }
		send_ok(); current_config->baudrate = value; current_port->set_baudrate(value); return;
	}
	if (sscanf(command, "TTM:ADP-%u%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {2,5,10,15,20,25,30,40,50};
		if (!is_idle() || !is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) { send_error(); return; }
		current_config->adv_interval_ms = value * 100; current_port->save_config(current_config);
		if (current_port->restart_advertising(current_config)) { send_error(); return; } send_ok(); return;
	}
	if (strncmp(command, "TTM:REN-", 8) == 0 && is_idle()) {
		const char *name = command + 8;
		if (strlen(name) == 0 || strlen(name) > TTM_NAME_MAX_LEN) { send_error(); return; }
		strcpy(current_config->name, name); current_port->save_config(current_config);
		if (current_port->restart_advertising(current_config)) { send_error(); return; } send_ok(); return;
	}
	if (strncmp(command, "TTM:ADD-", 8) == 0 && is_idle()) {
		const char *add = command + 8; size_t add_len = strlen(add);
		if (add_len == 0 || add_len > TTM_ADD_MAX_LEN) { send_error(); return; }
		memcpy(current_config->add_data, add, add_len); current_config->add_len = add_len;
		current_port->save_config(current_config);
		if (current_port->restart_advertising(current_config)) { send_error(); return; } send_ok(); return;
	}
	if (sscanf(command, "TTM:CIT-%ums%n", &value, &used) == 1 && command[used] == '\0') {
		static const uint32_t allowed[] = {20,50,100,200,300,400,500,1000,1500,2000};
		if (!current_port->connected() || !is_allowed(value, allowed, sizeof(allowed) / sizeof(allowed[0]))) { send_error(); return; }
		if (current_port->set_connection_interval(value)) { send_text("TTM:TIMEOUT\r\n"); return; } send_ok(); return;
	}
	send_error();
}
