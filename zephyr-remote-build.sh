#!/usr/bin/env bash

# 目录通过 Samba 挂载，因此经 SSH 调用 Ubuntu 主机上的构建脚本。
set -euo pipefail

UBUNTU_HOST="${UBUNTU_HOST:-qinbo@192.168.1.100}"
REMOTE_PROJECT="${REMOTE_PROJECT:-/home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3}"
VARIANT="${VARIANT:-initial}"
remote_project_quoted="$(printf '%q' "${REMOTE_PROJECT}")"
variant_quoted="$(printf '%q' "${VARIANT}")"
app_blink_quoted="$(printf '%q' "${APP_BLINK_INTERVAL_MS:-}")"
build_dir_quoted="$(printf '%q' "${BUILD_DIR:-}")"
dist_dir_quoted="$(printf '%q' "${DIST_DIR:-}")"

ssh \
	-o ServerAliveInterval=30 \
	-o ServerAliveCountMax=3 \
	"${UBUNTU_HOST}" \
	"cd ${remote_project_quoted} && VARIANT=${variant_quoted} APP_BLINK_INTERVAL_MS=${app_blink_quoted} BUILD_DIR=${build_dir_quoted} DIST_DIR=${dist_dir_quoted} bash ./build_esp32c3.sh $*"
