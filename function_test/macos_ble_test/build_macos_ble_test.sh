#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"
clang -fobjc-arc -framework CoreBluetooth -framework Foundation main.m -o macos_ble_test
echo "编译完成：$(pwd)/macos_ble_test"
