# ESP32-C3 GPIO8 闪灯

本 Zephyr 工程每秒翻转一次 GPIO8，目标板为 `esp32c3_devkitm`。GPIO8 在板级
overlay 中被定义为 `led0` devicetree 别名，输出为高电平有效。

源代码目录通过 Samba 挂载，请勿直接在 macOS 上编译。

## 复位后的启动日志

日志经 ESP32-C3 内置 USB Serial/JTAG 输出到主机侧 `/dev/ttyACM0`。按下 `RST` 后，
USB 设备会断开并重新枚举约 1 秒；固件的应用主线程会先等待 2 秒，之后才开始 GPIO8
初始化并输出首条日志，因此监听程序重连后仍可获得全部应用启动日志。

可使用自动重连命令监听：

```bash
while true; do
    picocom --noreset --noinit --nolock -b 115200 /dev/ttyACM0
    sleep 1
done
```

## 验证状态

已于 2026-07-15 验证：

- ESP32-C3 硬件上的 GPIO8 LED 闪烁正常。
- macOS 侧远程编译和烧录流程执行成功。
- SSH 登录 Ubuntu 后手动执行的编译和烧录流程执行成功。

## 从 macOS 远程编译和烧录

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

两个脚本默认使用 `qinbo@192.168.1.100`；如有需要，可通过环境变量覆盖：

```bash
UBUNTU_HOST=user@host bash ./zephyr-remote-build.sh
```

## SSH 登录 Ubuntu 后编译和烧录

```bash
cd /home/qinbo/mpushare/macos_workspace/zephyr/apps/esp32-c3
bash ./build_esp32c3.sh
bash ./flash_esp32c3.sh
```

脚本默认要求工作区位于 `/home/qinbo/mpushare/macos_workspace/zephyr`；如安装在
其他位置，请设置 `ZEPHYR_WORKSPACE`。`flash_esp32c3.sh` 使用 Zephyr 已配置的
ESP32 烧录器。请先将开发板连接到 Ubuntu 主机；需要指定设备时，可将烧录器参数
直接传给脚本，例如 `bash ./flash_esp32c3.sh --esp-device /dev/ttyUSB0`。

## Ubuntu 环境前置条件

ESP32 编译要求 Zephyr Python 环境包含 `esptool` 5.0.2 或更高版本。已验证的
Ubuntu 环境已具备该依赖；其他环境若提示缺失，请在 Zephyr 工作区执行一次：

```bash
source .venv/bin/activate
west packages pip --install
```
