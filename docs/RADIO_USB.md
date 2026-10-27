# USB radio 后端（`src/radio_usb`）状态

状态：**WIP，未通过 Phase 2 验收**。当前提交保留可编译的骨架和已经验证的管道方向，硬件 RX 转发尚未打通。

## 已实现

- CMake 工程 `src/radio_usb/CMakeLists.txt`，链接 devourer + libusb，C8821C-only。
- `airbridge_radio.exe`：libusb 打开 `0bda:c820`、claim interface 2、`CreateRadio`、`InitWrite`、`StartRxLoop`。
- `radio_link` v3 帧格式（MIT）：HELLO、RX、TX、SET_CHANNEL、SET_BSSID_FILTER、SET_ADDRESS。
- 命名管道服务：客户端连接后收到 HELLO（version 3、channel、width）。
- 管道双向已被“合成 RX”自测验证：主线程写入的 10 字节 RX 消息，Python 客户端能完整收到。

## 未通过 / 已知问题

1. **硬件 RX 转发未通。** RX 回调确实被调用（日志 `rx cb=...`），但从 RX 线程 `WriteFile` 到命名管道时，客户端在窗口内收不到帧；客户端关闭后写入返回 `ERROR_NO_DATA (232)`。已排除“合成 RX”路径，问题集中在跨线程写管道或 RX 启动时序，尚未定位。
2. **启动时序。** `InitWrite` 后立即 `StartRxLoop` 的首帧可能延迟数秒；当前代码先等首帧（warmup）再接受客户端，仍未能解决转发。
3. **命名管道与 Dart 的兼容性。** Dart 的 `dart:io` 没有 Windows 命名管道 API；如果坚持 Dart 应用，IPC 应改为 **localhost TCP** 或 **stdin/stdout 长度前缀帧**。`radio_link` 帧格式可以保留。
4. **固件启动重试。** 反复 reset 后出现过 `FW ready check failed (0x80 != 0xC078)`，devourer 的 CPU-reset 重试一次后恢复；恢复后 `doctor` 判定 `HEALTHY`。测试之间建议拔插/重新上电。
5. **未做**：TX over pipe、SET_CHANNEL over pipe、BSSID/address 过滤实测、断连重连、Dart 客户端。

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

运行：`build/radio_usb/Release/airbridge_radio.exe 6 20`（初始化约 40 秒，然后等待客户端）。

测试客户端：`python tools/radio_link_client.py rx 10`。

## 下一步

1. 决定 IPC：localhost TCP 或 stdio（Dart 兼容），再重做传输层；管道原型保留作对照。
2. 用 C++ 客户端复现并定位跨线程写问题；确认后重测硬件 RX/TX。
3. 通过后再接 `SET_CHANNEL`、`SetStationIdentity` 和 Dart 应用。
