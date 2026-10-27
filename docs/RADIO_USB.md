# USB radio 后端（`src/radio_usb`）状态

状态：**TCP 传输、硬件 RX、2.4 GHz TX 已打通；Phase 2 的核心链路可用，换信道/地址/ACK 与 Dart 客户端未测。**

## 已实现并验证

- `airbridge_radio.exe`：libusb 打开 `0bda:c820`、claim interface 2、`CreateRadio`、`InitWrite`、`SetTxMode`、`StartRxLoop`。
- `radio_link` v3 帧格式（MIT）跑在 **localhost TCP（127.0.0.1:49876）** 上；HELLO 带 8 字节消息头。
- 端口用 `SO_EXCLUSIVEADDRUSE`，避免其他本地服务抢连。
- RX：客户端 10 秒收到 **311 帧、28957 字节**，含真实 beacon；RX 回调经有界队列 + 独立写线程，慢客户端不会阻塞 USB RX 环。
- TX（RX 环同时运行）：客户端经 TCP 发 20 帧，ESP32-S3 空口收到 **20/20 唯一序号 0..19**，RSSI -10..-9，长度 104，哈希与 `txdemo` 一致。
- `AIRBRIDGE_NO_RX=1` 可跳过 RX 环，用于隔离 TX 测试；该模式同样 20/20。
- `AIRBRIDGE_TX_RATE` 默认 `1M`，通过 `SetTxMode` 在 bring-up 后设置。
- **SET_CHANNEL**：客户端从频道 6 切到 36，radio 日志 `channel -> 36`；切换后 2 秒收到 64 帧，切回/过滤测试正常。
- **BSSID 过滤**：设置 `00:11:22:33:44:55` 后 2 秒 0 帧；发送全零清除后 2 秒恢复 62 帧。全零现在表示“清除过滤”。
- **Dart 客户端**：`tools/radio_link_client.dart` 连接 `127.0.0.1:49876`，收到 `HELLO version=3`，5 秒收 **84 帧 / 14664 字节**。Dart 的 `Socket` 可用，IPC 选择成立。
- **SET_ADDRESS / SetStationIdentity**：BSSID 必须是单播（首字节偶数）且非零、与 own 不同；用 `56:42:75:05:d6:00` 成功 armed。devourer 警告 `tx.retry_limit=0`，MAC 不会重传未确认帧；空口 ACK 行为仍未验证。
- **断连重连**：radio 改为 accept 循环，保持初始化状态。实测连续两个客户端都能收到 HELLO 并收包；第二个客户端之后还能发 5 帧 TX（`tx sent 5 failed 0`），不需要重启 radio。

## TX 曾经发不出去的原因

客户端从 `upstream/devourer/examples/tx/main.cpp` 解析 `beacon_frame` 时，把源码注释里的 `0x40 0x00`、`0x08 0x01` 也当成了帧字节，导致 body 从 100 字节变成 104 字节、帧头错位。芯片收到的是畸形帧，所以 `send_packet` 返回成功但空口没有合法帧。修复方法是解析前先去掉 `//` 与 `/* */` 注释。

## 已知问题

1. **启动慢**：反复 reset 后 bring-up 约 40–120 秒，期间可能出现 `FW ready 0x80 != 0xC078`，devourer 自动重试后恢复。
2. **未测**：SET_ADDRESS 的硬件 ACK 效果、断连重连、TX report/BlockAck、5 GHz。
3. **重连已支持**：accept 循环保持 radio 初始化；客户端断开后可以立即重连，不需要重新 bring-up。
4. **固件状态**：多次 reset 后曾出现 TX 假死（`txdemo` 也发不出），拔插网卡后恢复；测试之间建议重新上电。

## 构建与运行

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

- RX 通过：311 帧/10 s。
- TX 通过：ESP32-S3 见证 20/20，`evidence/esp32-witness-radio-tx7-full.log`。

## 下一步

1. TCP `SET_CHANNEL` 实测：切到频道 36/44，记录耗时和是否还能收。
2. `SET_ADDRESS`/`SET_BSSID_FILTER` 与 `SetStationIdentity` 的空口 ACK 验证。
3. 断连/重连、Dart `Socket` 客户端、最后接 AWDL。
