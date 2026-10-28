# 便携版 / Portable ZIP

> **历史文档：Flutter 壳与这个 27 MB 的便携 ZIP 已于 2026-10-28 移除。**
> 2026-10-29 起现行分发是**一个文件夹 + 压缩包**（根目录 `AirBridge32.exe` 即托盘程序，
> `dist\AirBridge32-portable.zip`），见 [DISTRIBUTION.md](DISTRIBUTION.md)；
> 单文件自解压 exe（`dist\AirBridge32.exe`）作为旧形态保留。
> 打包脚本：`tools/Package-Distributable.ps1`（文件夹/zip）与
> `tools/Package-Headless.ps1` + `tools/Build-Sfx.ps1`（单文件）。

目标：一个 ZIP，解压后双击 `AirBridge32.exe`，不需要安装器、不需要目标机 Python/VC++ 运行库、不写注册表、不装服务、不创建 Wintun。

## 产物

```text
dist/AirBridge32-portable.zip   (~27 MB)
```

解压后：

```text
AirBridge32/
  AirBridge32.exe          Flutter UI + 后端监管
  flutter_windows.dll / VC runtime
  backend/
    airbridge_radio.exe
    radio_pipe_bridge.exe
    awdl_netd.exe
    awdld.exe
    libusb-1.0.dll
    python/                embeddable Python 3.12 + cryptography
    airdrop/               receiver.py + mdns.py/identity.py/...
  data/                    日志、密钥、接收文件
  licenses/
  README.txt
```

## 运行行为

- 双击 `AirBridge32.exe` 自动启动 radio 149/40 → bridge → awdl_netd → awdld → receiver。
- **点窗口 X 是“最小化”，不是退出**：app 和 radio 继续驻留，第二次接收不再重新初始化 radio。
- 真正退出用界面里的 **“退出”** 按钮，会先停止所有后端。
- 接收文件在 `data/received`，日志在 `data/airbridge32.log`。
- 首次启动时由包内 `cryptography` 生成自签证书，**不再需要目标机安装 Git/OpenSSL**（`identity.py` 仅在模块缺失时才回退 `openssl.exe`）。

## 唯一需要管理员的步骤

RTL8821CU 的 MI_02 需要绑定 WinUSB。目标机器已绑定则解压即用；全新机器需要一次 Zadig/libwdi 绑定（见 `docs/BINDING_RUNBOOK.md`）。

## 构建

需要先准备 `build/python-embed`（Python 3.12 embeddable + cryptography），然后：

```powershell
powershell -ExecutionPolicy Bypass -File tools/Package-Portable.ps1 -Build
```

## 当前状态

- 便携版能启动、能被 iPhone 发现并接收文件；
- 同一 Flutter 壳 + 系统 Python 的对照是 2.58 MB / 3.2 s；PyInstaller/embedded 的实测波动很大（12–55 s），尚未稳定；
- 已确认 **Dart/Flutter 不是瓶颈**；下一步是原生 C++ receiver 直接并入 `awdl_netd`，去掉 Python 和本地 TCP 跳。
