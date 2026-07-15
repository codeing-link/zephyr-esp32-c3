#!/usr/bin/env bash

# 目录通过 Samba 挂载，因此经 SSH 调用 Ubuntu 主机上的构建脚本。
set -euo pipefail

UBUNTU_HOST="${UBUNTU_HOST:-qinbo@192.168.1.100}"
REMOTE_PROJECT="${REMOTE_PROJECT:-/home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3}"

ssh \
	-o ServerAliveInterval=30 \
	-o ServerAliveCountMax=3 \
	"${UBUNTU_HOST}" \
	"cd '${REMOTE_PROJECT}' && bash ./build_esp32c3.sh $*"
