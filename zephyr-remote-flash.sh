#!/usr/bin/env bash

# ESP32-C3 的 USB 串口设备连接在 Ubuntu 主机上，因此从该主机执行烧录。
set -euo pipefail

UBUNTU_HOST="${UBUNTU_HOST:-qinbo@192.168.1.100}"
REMOTE_PROJECT="${REMOTE_PROJECT:-/home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3}"

ssh \
	-o BatchMode=yes \
	-o ServerAliveInterval=30 \
	-o ServerAliveCountMax=3 \
	"${UBUNTU_HOST}" \
	"cd '${REMOTE_PROJECT}' && bash ./flash_esp32c3.sh $*"
