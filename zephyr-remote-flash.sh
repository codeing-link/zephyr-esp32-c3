#!/usr/bin/env bash

# ESP32-C3 的 USB 串口设备连接在 Ubuntu 主机上，因此从该主机执行烧录。
set -euo pipefail

UBUNTU_HOST="${UBUNTU_HOST:-qinbo@192.168.1.100}"
REMOTE_PROJECT="${REMOTE_PROJECT:-/home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3}"
VARIANT="${VARIANT:-initial}"
remote_project_quoted="$(printf '%q' "${REMOTE_PROJECT}")"
variant_quoted="$(printf '%q' "${VARIANT}")"
build_dir_quoted="$(printf '%q' "${BUILD_DIR:-}")"

ssh \
	-o BatchMode=yes \
	-o ServerAliveInterval=30 \
	-o ServerAliveCountMax=3 \
	"${UBUNTU_HOST}" \
	"cd ${remote_project_quoted} && VARIANT=${variant_quoted} BUILD_DIR=${build_dir_quoted} bash ./flash_esp32c3.sh $*"
