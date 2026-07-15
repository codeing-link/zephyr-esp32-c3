#!/usr/bin/env bash

set -u

PORT="${PORT:-/dev/ttyACM0}"
BAUDRATE="${BAUDRATE:-115200}"

# cat 不会将本机终端切换为原始模式，因此 Ctrl+C 会产生 SIGINT 并立即退出。
trap 'echo; echo "已停止串口监听。"; exit 0' INT TERM

while true; do
    if [[ ! -e "${PORT}" ]]; then
        echo "等待串口设备：${PORT}"
        sleep 1
        continue
    fi

    # 仅配置 USB 串口设备，不修改当前终端的 Ctrl+C 行为。
    stty -F "${PORT}" "${BAUDRATE}" cs8 -cstopb -parenb -ixon -ixoff
    echo "开始监听 ${PORT}；按 Ctrl+C 退出。"
    cat "${PORT}"

    # RST 导致 USB 串口短暂断开时，cat 会返回；等待设备重新枚举后自动重连。
    sleep 1
done
