#!/usr/bin/env python3
"""ESP32-C3 蓝牙透传 AT 指令自动测试脚本。"""

import argparse
import pathlib
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial；请在 Ubuntu 执行：python3 -m pip install pyserial")


TESTS = [
	("设置默认波特率", "TTM:BPS-115200", "TTM:OK"),
    ("查询厂商 ID", "TTM:VID-?", "TTM:VID-LSD_BLE_5.0"),
    ("查询固件版本", "TTM:REV-?", "TTM:REV-V2.1"),
	("设置数据延时", "TTM:CDL-5ms", "TTM:OK"),
	("设置发射功率", "TTM:TPL-+4", "TTM:OK"),
	("设置产品识别码", "TTM:PID-1234", "TTM:OK"),
    ("查询当前名称", "TTM:REN-?", "TTM:REN-"),
    ("设置测试名称", "TTM:REN-TEST-C3", "TTM:OK"),
    ("查询新名称", "TTM:REN-?", "TTM:REN-TEST-C3"),
	("查询物理地址", "TTM:MAC-?", "TTM:MAC-"),
    ("设置广播周期", "TTM:ADP-10", "TTM:OK"),
	("查询附加广播内容", "TTM:ADD-?", "TTM:ADD-"),
    ("设置附加广播内容", "TTM:ADD-ABC", "TTM:OK"),
    ("非法命令处理", "TTM:UNKNOWN", "TTM:ERP"),
]


def exchange(port, command):
    """发送一条以 CRLF 结束的命令，并收集短时间内返回的所有字节。"""
    port.reset_input_buffer()
    port.write((command + "\r\n").encode("ascii"))
    port.flush()
    deadline = time.monotonic() + 1.0
    received = bytearray()
    while time.monotonic() < deadline:
        waiting = port.in_waiting
        if waiting:
            received.extend(port.read(waiting))
            deadline = time.monotonic() + 0.15
        else:
            time.sleep(0.02)
    return received.decode("ascii", errors="replace")


def main():
    parser = argparse.ArgumentParser(description="测试 ESP32-C3 TTM AT 指令")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="测试串口设备")
    parser.add_argument("--baudrate", type=int, default=115200, help="测试串口波特率")
    args = parser.parse_args()

    record = ["# AT 指令测试记录", "", f"- 串口：`{args.port}`", f"- 波特率：`{args.baudrate}`", ""]
    passed = 0
    with serial.Serial(args.port, args.baudrate, timeout=0.05) as port:
        for title, command, expected in TESTS:
            response = exchange(port, command)
            result = expected in response
            passed += result
            status = "通过" if result else "失败"
            print(f"[{status}] {title}: {command} -> {response!r}")
            record.extend([f"## {title}", f"- 命令：`{command}`", f"- 期望：`{expected}`", f"- 实际：`{response}`", f"- 结果：{status}", ""])

    record.insert(1, f"\n总计：{passed}/{len(TESTS)} 通过。")
    output = pathlib.Path(__file__).resolve().parents[1] / "doc" / "AT指令测试记录.md"
    output.write_text("\n".join(record), encoding="utf-8")
    return 0 if passed == len(TESTS) else 1


if __name__ == "__main__":
    sys.exit(main())
