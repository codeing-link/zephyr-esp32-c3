#!/usr/bin/env bash

# 请在 Ubuntu 构建主机上执行本脚本，不要在 Samba 挂载的 macOS 主机上执行。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-/home/qinbo/mpushare/macos_workspace/zephyr}"
BOARD="${BOARD:-esp32c3_devkitm}"
VARIANT="${VARIANT:-initial}"

case "${VARIANT}" in
	initial)
		APP_BLINK_INTERVAL_MS="500"
		;;
	update)
		APP_BLINK_INTERVAL_MS="1000"
		;;
	*)
		echo "错误：未知 VARIANT=${VARIANT}，请使用 initial 或 update" >&2
		exit 1
		;;
esac

BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build/${VARIANT}}"
DIST_DIR="${DIST_DIR:-${SCRIPT_DIR}/dist}"
VARIANT_CONF="${SCRIPT_DIR}/conf/${VARIANT}.conf"

if [[ ! -f "${VARIANT_CONF}" ]]; then
	echo "错误：未找到变体配置：${VARIANT_CONF}" >&2
	exit 1
fi

source "${ZEPHYR_WORKSPACE}/.venv/bin/activate"
source "${ZEPHYR_WORKSPACE}/zephyr/zephyr-env.sh"

cd "${SCRIPT_DIR}"
west build --sysbuild -p always -b "${BOARD}" -d "${BUILD_DIR}" . -- \
	-Desp32-c3_EXTRA_CONF_FILE="${VARIANT_CONF}" "$@"

mkdir -p "${DIST_DIR}"

app_update="$(find "${BUILD_DIR}" -path '*/zephyr/zephyr.signed.bin' ! -path '*/mcuboot/*' -print -quit)"
if [[ -z "${app_update}" ]]; then
	echo "错误：未找到已签名 app 升级镜像 zephyr.signed.bin" >&2
	exit 1
fi

cp "${app_update}" "${DIST_DIR}/app-update-${VARIANT}-${APP_BLINK_INTERVAL_MS}ms.signed.bin"

merged_image="$(find "${BUILD_DIR}" -maxdepth 2 -name 'merged.bin' -print -quit)"
if [[ -n "${merged_image}" ]]; then
	cp "${merged_image}" "${DIST_DIR}/factory-${VARIANT}-${APP_BLINK_INTERVAL_MS}ms.merged.bin"
fi

echo
echo "构建完成："
echo "  variant: ${VARIANT}"
echo "  blink:   ${APP_BLINK_INTERVAL_MS} ms"
echo "  build:   ${BUILD_DIR}"
echo "  ota app: ${DIST_DIR}/app-update-${VARIANT}-${APP_BLINK_INTERVAL_MS}ms.signed.bin"
if [[ -n "${merged_image}" ]]; then
	echo "  merged:  ${DIST_DIR}/factory-${VARIANT}-${APP_BLINK_INTERVAL_MS}ms.merged.bin"
fi
