# USB radio 后端（`src/radio_usb`）状态

状态：**TCP 传输与硬件 RX 已打通；TX 尚未通过空口见证，Phase 2 未完成。**

## 已实现并验证

- `airbridge_radio.exe`：libusb 打开 `0bda:c820`、claim interface 2、`CreateRadio`、`InitWrite`、`StartRxLoop`。
- `radio_link` v3 帧格式（MIT）跑在 **localhost TCP（127.0.0.1:49876）** 上；HELLO 带 8 字节消息头，客户端解析为 version 3 / channel 6 / width 20。
- RX：客户端 10 秒收到 **311 帧、28957 字节**，含真实 beacon 帧（RSSI 约 -114..-121）。RX 回调经有界队列交给独立写线程，慢客户端不会阻塞 USB RX 环。
- 端口用 `SO_EXCLUSIVEADDRUSE`；此前 `SO_REUSEADDR` 导致客户端连到别的本地服务。

## 未通过 / 已知问题

1. **TX over TCP 尚未被空口见证。** 客户端通过 TCP 发 20 帧，radio 记录 `tx sent 20 failed 0`，但 ESP32-S3 在同一时间窗口收到 0 帧。可能是 RX 环启动时序、TX 队列/CCA 或帧内容问题；尚未定位。
2. **启动慢。** 反复 reset 后 bring-up 可到约 2 分钟；期间出现一次 `FW ready 0x80 != 0xC078`，devourer 自动重试后恢复。正常一次约 40–120 秒。
3. **未测**：SET_CHANNEL、SET_BSSID_FILTER/SET_ADDRESS 的硬件效果、断连重连、TX report/ACK、Dart 客户端。
4. **Dart 兼容性**：保留 TCP 而不是命名管道，Dart `Socket` 可直接使用；`radio_link` 帧不变。

## 构建

```powershell
$env:VCPKG_ROOT = "build/deps/vcpkg"
$env:PKG_CONFIG_PATH = "build/deps/vcpkg/installed/x64-windows/lib/pkgconfig"
$env:PKG_CONFIG_ARGN = "--msvc-syntax"
cmake -S src/radio_usb -B build/radio_usb -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=build/deps/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DPKG_CONFIG_EXECUTABLE=build/deps/vcpkg/installed/x64-windows/tools/pkgconf/pkgconf.exe
cmake --build build/radio_usb --config Release --target airbridge_radio
```

运行：`build/radio_usb/Release/airbridge_radio.exe 6 20`（初始化后监听 `127.0.0.1:49876`）。

## 测试客户端

```powershell
python tools/radio_link_client.py rx 10
python tools/radio_link_client.py tx 20
```

- RX 已通过（311 帧/10 s）。
- TX 当前只验证到“radio 收到并调用 send_packet”，空口未见；ESP32 见证日志 `evidence/esp32-witness-radio-tx.log` 匹配数为 0。

## 下一步

1. 把 RX 环在 accept 之前启动（或客户端 HELLO 后等待 RX 就绪）再测 TX，排除“TX 早于 RX 环”的时序问题。
2. 若仍无空口帧，检查 `send_packet` 返回后的 TX report/队列状态，并对比 `txdemo`（已知能上空中）的调用顺序。
3. 通过后再测 SET_CHANNEL、地址/过滤、Dart 客户端，最后接 AWDL。
