#!/usr/bin/env bash

# 请在 Ubuntu 构建主机上执行本脚本，不要在 Samba 挂载的 macOS 主机上执行。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-/home/qinbo/mpushare/macos_workspace/zephyr}"
BOARD="${BOARD:-esp32c3_devkitm}"

source "${ZEPHYR_WORKSPACE}/.venv/bin/activate"
source "${ZEPHYR_WORKSPACE}/zephyr/zephyr-env.sh"

cd "${SCRIPT_DIR}"
west build -p always -b "${BOARD}" . "$@"
