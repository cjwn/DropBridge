# ESP32-S3 2.4 GHz 空口见证

用途：在 2.4 GHz 上独立验证 RTL8821CU 是否真的把帧发到了空中，以及内容是否一致。**只监听，不扫描、不连接、不发包。**

## 硬件与工具

- ESP32-S3-DevKitC-1（本机通过 CH343 串口，示例 `COM17`）
- PlatformIO Core 6.2.0（`~/.platformio/penv/Scripts/pio.exe`）
- 平台 `espressif32@7.0.1`（已安装），Arduino 框架

## 编译与刷写

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run -d tools/esp32_witness -t upload --upload-port COM17
```

构建输出在 `build/esp32_witness`（git 忽略）。

## 运行

```powershell
python tools/esp32_witness/capture_serial.py COM17 70 evidence/esp32-witness-run.log
```

同时由 Windows 侧发帧，例如：

```powershell
$env:DEVOURER_PID='0xc820'
$env:DEVOURER_CHANNEL='6'
$env:DEVOURER_TX_FRAMES='20'
$env:DEVOURER_TX_GAP_US='50000'
$env:DEVOURER_TX_RATE='1M'
build\devourer-8821c\Release\txdemo.exe
```

固件匹配发送地址 `57:42:75:05:d6:00`（devourer 的 canonical TX SA），输出：

```json
{"ev":"witness.frame","t_ms":...,"len":104,"rssi":-10,"type":0,"sub":4,"seq":0,"hash":"9126b0f8","addr2":"57:42:75:05:d6:00"}
```

## 已发现的坑

- **txdemo 必须做完整 USB reset**。跳过 reset 时 USB 会报告 20 帧提交成功、0 失败，但空口一帧都没有。重复测试时不要用 `DEVOURER_SKIP_RESET=1`。
- 见证计数有时每轮 +20、有时 +40；唯一序号才是可靠指标。
- ESP32-S3 只有 2.4 GHz：看不到 5 GHz，也不能证明硬件 ACK/BlockAck。

结果汇总见 `evidence/tx-witness-summary.json`。
