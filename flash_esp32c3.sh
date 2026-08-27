#!/usr/bin/env bash

# 请在连接 ESP32-C3 开发板的 Ubuntu 主机上执行本脚本。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-/home/qinbo/mpushare/macos_workspace/zephyr}"
VARIANT="${VARIANT:-initial}"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build/${VARIANT}}"

if [[ ! -d "${BUILD_DIR}" ]]; then
	echo "错误：未找到构建目录：${BUILD_DIR}" >&2
	echo "请先执行构建：VARIANT=${VARIANT} bash ./build_esp32c3.sh" >&2
	exit 1
fi

source "${ZEPHYR_WORKSPACE}/.venv/bin/activate"
source "${ZEPHYR_WORKSPACE}/zephyr/zephyr-env.sh"

cd "${SCRIPT_DIR}"
west flash -d "${BUILD_DIR}" "$@"
