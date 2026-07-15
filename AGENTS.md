# AGENTS.md

## 文字语言规则

本工程中的代码注释、脚本提示、配置注释、README 和其他文字说明必须使用中文。
第三方工具要求的标识符、命令、许可证声明及专有名词可保持原样。

## 编译与烧录

本工程从 macOS 编辑，但由于目录通过 Samba 挂载，必须在 Ubuntu 上编译和烧录。
不要直接在 macOS 上执行 `west build` 或 `west flash`。

从 macOS 使用以下远程包装脚本：

```bash
bash ./zephyr-remote-build.sh
bash ./zephyr-remote-flash.sh
```

`build_esp32c3.sh` 和 `flash_esp32c3.sh` 供已 SSH 登录 Ubuntu 构建主机的用户
手动执行。

Ubuntu 的 Zephyr Python 环境必须包含 `esptool>=5.0.2`。如缺少 Python 构建
依赖，请激活工作区 `.venv` 后执行一次 `west packages pip --install`。
