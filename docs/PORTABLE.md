# 便携版 / Portable ZIP

目标：一个 ZIP，解压后双击 `AirBridge32.exe`，不需要安装器、不需要 Python/VC++ 运行库、不写注册表、不装服务、不创建 Wintun。

## 产物

```text
dist/AirBridge32-portable.zip
```

解压后：

```text
AirBridge32/
  AirBridge32.exe          Flutter UI + 后端监管
  config / flutter_windows.dll / VC runtime
  backend/
    airbridge_radio.exe
    radio_pipe_bridge.exe
    awdl_netd.exe
    awdld.exe
    airdrop_receiver.exe   PyInstaller，无 Python 依赖
    libusb-1.0.dll
  data/                    日志、密钥、接收文件
  licenses/
  README.txt
```

`AirBridge32.exe` 自动按顺序启动并监管后端：radio 149/40 → bridge → awdl_netd → awdld → receiver。退出时逆序结束。

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File tools/Package-Portable.ps1 -Build
```

`-Build` 会重建 radio/bridge、awdl_netd、awdld、Flutter，然后由 PyInstaller 生成 `airdrop_receiver.exe` 并压缩。

## 唯一需要管理员的步骤

RTL8821CU 的 MI_02 需要绑定 WinUSB。如果目标机器已经绑定，ZIP 就是真正的解压即用；全新机器需要一次 Zadig/libwdi 绑定（见 `docs/BINDING_RUNBOOK.md`），之后不再需要。

## 当前状态

- 便携版可以通过 `AirBridge32.exe` 启动全部后端，iPhone 能用隔空投送发现并传文件；
- 接收文件在 `data/received`，日志在 `data/airbridge32.log`；
- v2ray TUN 可以保持开启；
- 已知：便携版实测约 216 KB/s，手动同链路 40 MHz 约 1.05 MB/s；下一轮定位 receiver onefile/Flutter 监管层的差异。
