# PC → iPhone 发送（已终止：确认无法实现）

> **2026-11-04 结论**：用户确认 Windows → iPhone 发送无法实现，Q48 终止，DropBridge 只做接收。下文是排查过程的记录，保留备查。

2026-10-30。目标：电脑主动把文件发给 iPhone。iPhone 侧设为「所有人」。
本文记录**已经打通的层**、**当前卡点**和下一步，避免下一步人重走一遍。

## 1. 结论先说

| 层 | 状态 |
| --- | --- |
| 链路（radio/awdld） | ✅ 双向可用（能收帧，也能发帧） |
| lwIP 出站 TCP | ✅ 新加：`awdl_netd` 的 outbound proxy（`127.0.0.1:8772`） |
| 手机地址 | ✅ 能拿到：从手机 mDNS **查询**的源地址得到（EUI-64 由 MAC 推出，实测 `FE80::64E2:EFF:FEB5:8E78`，与 awdld 里的 peer MAC 一致） |
| 手机 AirDrop **端口** | ❌ 未知。对 8771 发 SYN 收到 **RST**，说明端口不对；端口只在手机自己的 mDNS **应答**（SRV）里 |
| 手机的 mDNS 应答 | ❌ 一直收不到。收得到它的查询（`_airdrop` PTR，36/64 B），收不到它的 answer |
| 证书 | 未验证（还没走到 TLS） |

**当前卡点：手机不公告/不应答自己的 AirDrop 服务，所以拿不到它的 SRV 端口，无法发起 `/Ask`。**

## 2. 已经修掉的真实缺陷（对接收方向也有价值）

- **mDNS 组播从未加入**：`awdl_netd` 开了 `LWIP_IPV6_MLD`，但没人加入 `ff02::fb`，
  lwIP 在 `ip6_input()` 里就把手机的 mDNS 组播丢掉了。表现是 `rx_seen` 在涨、
  UDP 代理一个包都收不到。现在启动 mDNS 代理前 `mld6_joingroup_netif(ff02::fb)`，
  手机一开面板就能在 netd 日志里看到它的 mDNS 查询（含源地址）。
- **应答没有路由给查询者**：代理原来只把 AWDL 侧包转给“最后一个本地发送者”，
  浏览器和公告器会互相抢。现在记录“最后一个提问者”，应答优先回给它。
- 新增 `AWDL_NETD_MDNS_DEBUG=1`：打印每个 mDNS 包的收发方向、大小、源地址、
  是否路由，定位这类问题不用再猜。

## 3. 新增的发送通道

```text
tools\airdrop_send.py  ──(127.0.0.1:8772, "CONNECT <ipv6> <port>\n")──►  awdl_netd
        │                                                                   │
        │  复用的上游协议（MIT）: sender.py / transfer.py / cpio.py / identity.py
        ▼                                                                   ▼
   TLS + POST /Ask + /Upload  ◄──────────── lwIP connect() ──────────►  awdld ──► radio
```

- `awdl_netd` 的 outbound proxy 默认**关闭**，只有监管加 `--allow-send` 时才起
  （`AWDL_NETD_OUTBOUND=8772`），所以接收版的产品路径没有多一个本地监听。
- 本地协议：连上后先发一行 `CONNECT <ipv6> <port>\n`，之后字节原样双向转发，
  TLS 端到端。头不会发给对端。
- `tools/airdrop_send.py`：
  - 发现：`mdns.Instances` 通过 netd 的 mDNS 代理（15353）发 PTR/PTR+SRV 查询；
  - 直连：`--address <ipv6>` 跳过发现，直接对 8771 发 `/Ask`（用于已经知道地址时）；
  - 连接：`ProxyConnection` 替换上游 `sender.Connection` 的 Windows 4 元组连接。
- 离线回归：`python tools/test_netd_tcp.py <netd> --case outbound` 验证 CONNECT 解析
  与 lwIP 发出 SYN（不做假 TCP 服务端，数据面要靠真机）。

## 4. 真机观察（2026-10-30 00:12–00:50）

| 时间 | 手机状态 | 现象 |
| --- | --- | --- |
| 00:12:5x | 刚设成「所有人」，未开面板 | `rx_seen=14` 短暂出现，说明 AWDL 被拉起过 |
| 00:18 / 00:22 / 00:50 | 共享面板在前台 | `peers 1`，手机持续发 mDNS **查询**，源 `FE80::64E2:EFF:FEB5:8E78`；**0 个应答** |
| 00:50:01 | 对 `FE80::64E2:EFF:FEB5:8E78:8771` 发 SYN | **收到 RST**（`tcp flow=2 ... rst=1`），说明地址可达、端口不对 |
| 00:50+ | 共享面板关闭 | `peers 0`、`rx_seen=0`，手机退出 AWDL |

判读：iPhone 在「共享面板打开」时是**发送/浏览**状态（发查询、不公告自己）；
我们需要的「接收者」状态需要在**面板关闭、AirDrop 开着、手机解锁**时观察。
这一点还没有验证——下一步就是这个单变量实验。

## 5. 下一步（按顺序）

1. **面板关闭**、手机解锁亮屏、AirDrop「所有人」，电脑侧跑 60 s 浏览，
   看能否收到 `_airdrop._tcp` 的 **answer**（含 SRV 端口）。
   - 收到 → 用 `tools\airdrop_send.py --address <addr>`（端口来自 SRV）走 `/Ask`，
     看手机是否弹窗；弹窗即成功，再谈产品化。
   - 仍收不到 → 说明 iPhone 在「仅所有人」时也不主动公告，必须有蓝牙 beacon
     （上游 `sender.py` 的 Beacon，需要 MI_00 蓝牙恢复）或由手机先发起一次。
2. 若手机弹窗但 TLS 被拒（日志 `CERTIFICATE`/`UNKNOWN_CA`），则是证书策略问题：
   iPhone 对非 Apple 证书的接受度需要在「所有人」下再确认。
3. 真正的产品化（不是本次试验）要：把发送入口放进托盘（选择文件/手机）、
   服务端 SRV 端口解析、超时与取消、以及许可与签名问题。

## 7. 2026-10-30 01:00 追加：蓝牙 beacon 打通了发现，卡在证书

用户提示“不是有蓝牙唤醒吗”，于是启用上游 `upstream/winawdl/scripts/airdrop-beacon.ps1`
（Apple 的 AirDrop BLE 广播，`-DryRun` 输出 `would advertise 20 bytes`，实跑输出
`advertising`）。**注意：它用的是机器上的 Intel AX211 蓝牙（状态 OK），不是
Realtek 复合设备的 MI_00。**

结果：

| 环节 | 结果 |
| --- | --- |
| beacon 叫醒手机 | ✅ 手机进入 AWDL（`peers 1`） |
| 手机公告 | ✅ **180 条 mDNS 应答**（297 B，带 SRV 地址与端口） |
| 发现 + 地址/端口 | ✅ `fe80::64e2:eff:feb5:8e78`（与 peer MAC 的 EUI-64 一致） |
| 出站 TCP + TLS | ✅ 握手到达手机，手机主动回 alert |
| iPhone 接受证书 | ❌ `Unwilling`：上游把 TLS alert 里的 `CERTIFICATE`/`UNKNOWN_CA` 解释为“对方不收这台电脑的证书” |

上游 `sender.py` 自己的注释就记录过这个现象：*"An iPhone has been seen to, with
'certificate unknown'; it is probably what one visible to its contacts only does,
since nothing vouches for a certificate made here."* 用户确认 AirDrop **一直**是
「所有人」，所以不只是“仅联系人”的问题：**iOS 对发送方证书的校验比 macOS 严，
自签证书在「所有人」下也可能被拒**。这正是最初判断的唯一未知数，现在有了第一次实测证据。

下一步（若还要继续）：
1. 抓**确切的 TLS alert**（`build/diagnostics/send/tls_probe.py`：浏览→直连→打印异常
   与证书信息）。`unknown ca` 与 `certificate unknown` 指向不同结论；
2. 若 alert 指向“证书结构不被接受”，可试换证书参数（CN/keyUsage/有效期）——但
   iOS 大概率还要求发送方的 Apple 签名验证记录（`SenderRecordData`），那就无解；
3. 若 alert 指向“没找到发送方的 mDNS 记录”，则要保证 `/Ask` 之前手机已看到我们的
   公告（把 `pk` 与证书绑定关系核对一遍）。

MI_00（Realtek 蓝牙）在“重启”之后仍是 `CM_PROB_FAILED_ADD`——本轮用户报告的重启
实际没有发生（系统 uptime 显示 11 h 36 min）；`pnputil /restart-device` 也无效。
这不影响 beacon（走 Intel 蓝牙）。

## 8. 复现命令

```powershell
# 带发送能力的测试包（只换 netd，334 文件集合不变）
powershell -File tools\Build-Supervisor.ps1 -Test
# 起链路（--allow-send 让 netd 额外监听 8772）
& '.\build\supervisor\Release\AirBridge32.exe' --root '.\build\diagnostics\send\test-package' `
    --data (Resolve-Path '.\dist\data') --allow-send --smoke 1800

# 发现 + 发送（手机在可发现状态时）
python tools\airdrop_send.py --search 20 packaging\README.txt
# 已知地址时直连（端口先按 8771 试）
python tools\airdrop_send.py --address FE80::xxxx --search 1 packaging\README.txt
```
