# Phase 3：AWDL 接入（准备完成，等待授权运行）

状态：**构建与桥接已就绪；尚未运行 `awdld`。** 运行会创建一个 Wintun 虚拟网卡并在频道 44 上发射 AWDL 帧，属于网络变更与无线发射，必须先获得用户授权。

## 组成

```text
awdld.exe（WinAWDL/OWL，GPL-3.0）
   │  命名管道 \\.\pipe\winawdl-radio（radio_link v3）
radio_pipe_bridge.exe（字节泵，不改协议）
   │  127.0.0.1:49876
airbridge_radio.exe（C++/devourer，独占 RTL8821CU）
```

- `awdld` 原样使用 WinAWDL 的 AWDL 实现，不改源码。
- 桥是纯字节转发；`radio_link` 帧格式不变。
- `awdl/` 是 GPL-3.0，独立进程；不链接进 Dart 应用。

## 已准备好的文件

| 文件 | 说明 |
| --- | --- |
| `build/awdl/awdld.exe` | MinGW 构建，已能打印 usage |
| `build/awdl/wintun.dll` | 0.14.1，官方 SHA-256 与 WireGuard LLC 签名校验通过 |
| `build/radio_usb/Release/airbridge_radio.exe` | TCP radio，RX/TX 已验证 |
| `build/radio_usb/Release/radio_pipe_bridge.exe` | 管道 ↔ TCP 桥，已用命名管道客户端验证 HELLO |
| `tools/Build-Awdl.ps1` | 可复现构建 awdld |
| `upstream/winawdl/upstream/owl` | OWL + radiotap 子模块已取回 |

## 运行顺序（需要授权）

1. 确认有线网络/另一块网卡可上网；AWDL 使用专用 RTL8821CU。
2. 启动 radio（先用频道 44，20 MHz）：
   `build\radio_usb\Release\airbridge_radio.exe 44 20`
3. 启动桥：
   `build\radio_usb\Release\radio_pipe_bridge.exe`
4. 启动 AWDL：
   `build\awdl\awdld.exe -c 44 -s 2 -j 400 -v`
5. iPhone：解锁，打开隔空投送，接收设为“所有人”（或联系人），停留在隔空投送界面。
6. 观察 `awdld` 日志是否出现邻居/同步；记录 iPhone 型号、iOS 版本、时间。

## 授权边界

- `awdld` 会创建 **Wintun 虚拟网卡**（新的网络接口），可能要求管理员权限。
- 会在频道 44 发射 AWDL beacon/action 帧。
- 不改现有网卡、防火墙或路由；Wintun 只用于 AWDL 数据面。
- 停止：Ctrl+C `awdld` → 关闭 bridge → 关闭 radio。必要时在设备管理器卸载 Wintun 适配器。

## 预期与失败处理

- 成功迹象：`awdld` 输出邻居/同步，iPhone 隔空投送出现 Windows 设备。
- 若无发现：先确认 radio 日志有 `channel -> 44`、bridge 有字节流、`awdld` 无 Wintun 错误；再检查 iPhone 的 AirDrop 可见性设置。
- 失败日志、原始输出和时间标记全部保留到 `evidence/`。

## 许可证

- `awdld` 与 OWL：GPL-3.0-or-later，独立进程。
- `wintun.dll`：WireGuard LLC 许可，随包附带 `LICENSE.txt`（在 `upstream/winawdl/deps/wintun/`）。
- `radio`：GPL-2.0；桥与 Dart 应用走 MIT 的 `radio_link` 帧。
