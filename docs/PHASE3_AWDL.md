# Phase 3：AWDL 接入（首次真实传输成功）

状态：**首次真实 AirDrop 传输成功。** iPhone 15 Pro Max（iOS 17.5）在隔空投送中看到 AirBridge32，并把一张 2.9 MB PNG 传到 Windows；文件大小与 PNG 签名已核对。测试时必须关闭 v2rayN TUN（其 IPv6 关闭 + 严格路由会让 IPv6 不可用）。

## 2026-10-27 首次真实传输（v2ray TUN 关闭）

- 环境：RTL8821CU 频道 149、20 MHz；`awdld -c 149` 提权运行；Wintun `awdl0`；`receiver.py --name AirBridge32`。
- 结果：iPhone 依次 `POST /Discover`、`POST /Ask`、`POST /Upload`，存储 `IMG_3583.PNG`。
- 文件：2,911,531 字节，PNG 签名 `89504e470d0a1a0a`，1290×2796，SHA-256 `C899358A...C48B1`；传输约 10.1 秒。
- AWDL：`peers 1`，`rx data 2166 (2025 joined)`，`tx data 191`。
- 证据：`evidence/airdrop-first-transfer.txt`（去敏）；原始接收日志含发送者姓名/IP/令牌，未提交。
- 关键前提：v2rayN TUN 必须先完全退出。开着 TUN 时 `ping ::1` 失败、`tx data 0`，iPhone 看不到设备。

## 2026-10-27 首轮失败与根因

- 首轮 `awdld -c 149` 已与 iPhone 的 AWDL 同步（`peers 2`、`rx action` 增长），但 `rx data 3 / tx data 0`，iPhone 看不到 AirBridge32。
- 当时 `ping ::1`、链路本地 IPv6 全部 `General failure`。先怀疑 DSH 的 `codex_sandbox_offline_*` 防火墙规则；后续确认这些规则的 `LocalUser` 是 `CodexSandboxOffline`（SID -1004），当前 shell 是 `ndpyz`（-1001），不是活动原因。
- 真正原因：**v2rayN TUN 正在运行**，配置为 `EnableTun=true`、`AutoRoute=true`、`StrictRoute=true`、`EnableIPv6=false`、`EnableIPv6Address=false`，IPv6 被整体掐断。
- 完全退出 v2rayN 后 `ping ::1`、链路本地 IPv6 立即恢复；随后同一条 AWDL 链路完成真实文件传输。
- 未修改任何防火墙或 v2ray 配置。

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

## 运行顺序（已实测）

1. **先完全退出 v2rayN/TUN**（确认 `sing-box.exe` 也退出），否则 IPv6 不可用；确认另一块网卡可上网，AWDL 使用专用 RTL8821CU。
2. 启动 radio（频道 149，20 MHz）：
   `build\radio_usb\Release\airbridge_radio.exe 149 20`
3. 启动桥：
   `build\radio_usb\Release\radio_pipe_bridge.exe`
4. 启动 AWDL：
   `build\awdl\awdld.exe -c 149 -s 2 -j 400 -v`（需要管理员权限创建 Wintun；也可用 `tools\Run-AwdlAdmin.ps1 -Channel 149`）
5. 启动 AirDrop 接收服务（普通终端即可，绑定 `awdl0`）：
   `python upstream\winawdl\airdrop\receiver.py --name AirBridge32 --iface awdl0 --outdir build\received --trace`
6. iPhone：解锁，打开隔空投送，接收设为“所有人”（或联系人），停留在隔空投送界面。
7. 观察 `awdld` 日志是否出现 `peers`/`data`，以及 `receiver.py` 是否出现请求；记录 iPhone 型号、iOS 版本、时间。

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
