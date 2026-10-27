# 用户态 AWDL 数据面（PoC）

目标：在 v2ray TUN 常开、且不修改 TUN 默认设置的前提下，让 iPhone 的隔空投送仍然可用。

## 架构

```text
iPhone
  ↕ AWDL
radio.exe（RTL8821CU）
  ↕ radio_link TCP / 命名管道桥
awdld / OWL（AWDL 控制面，GPL-3.0）
  ↕ 原始 IPv6 包（本地 TCP 49877，不进 Windows IP 栈）
awdl_netd（lwIP 2.2.2，用户态）
  ├─ IPv6 / ICMPv6 / NDP
  ├─ mDNS 5353：广播 AirBridge32 / _airdrop._tcp
  └─ TCP 8771 代理 → 127.0.0.1:8771
        ↕
receiver.py（TLS / HTTP / dvzip，原样复用）
```

v2ray TUN 只拦截 Windows IP 栈；这条路径完全不经过它。

## 代码

| 路径 | 作用 |
| --- | --- |
| `src/awdl_netd/main.cpp` | lwIP netif、IPC、mDNS、TCP 8771 代理 |
| `src/awdl_netd/lwipopts.h` | IPv6-only、NO_SYS、mDNS、TCP 配置 |
| `src/awdl_netd/arch/cc.h` | MSVC 的 lwIP 移植头（packing、byte order） |
| `upstream/winawdl/awdl/win/host_if_ipc.c` | awdld 的 IPC host 后端（替代 Wintun） |
| `upstream/winawdl/awdl/CMakeLists.txt` | `-DAWDL_HOST_IPC=ON` 切换后端 |
| `upstream/winawdl/airdrop/receiver.py` | 新增 `--listen` 代理模式（只绑 127.0.0.1，关闭自带 mDNS） |

## 构建

```powershell
# lwIP + awdl_netd
cmake -S src/awdl_netd -B build/awdl_netd -G "Visual Studio 17 2022" -A x64
cmake --build build/awdl_netd --config Release --parallel 8

# awdld with IPC host backend
$mingw = "build/deps/vcpkg/downloads/tools/perl/5.42.2.1/c/bin"
cmake -S upstream/winawdl/awdl -B build/awdl -G "MinGW Makefiles" `
  "-DCMAKE_C_COMPILER=$mingw/gcc.exe" `
  "-DCMAKE_MAKE_PROGRAM=$mingw/mingw32-make.exe" `
  -DCMAKE_BUILD_TYPE=Release -DAWDL_HOST_IPC=ON
cmake --build build/awdl --parallel 8
```

## 运行（PoC）

```text
1. airbridge_radio.exe 149 20
2. radio_pipe_bridge.exe
3. awdl_netd.exe                         # 监听 49877，等待 awdld
4. awdld.exe -c 149 -s 2 -j 400 -v       # 不再需要管理员/Wintun
5. python receiver.py --name AirBridge32 --listen 127.0.0.1 --mdns-address-file build\awdl_netd.addr --outdir build\received --trace
```

`awdl_netd` 默认启动 IPC/netif/TCP 8771；`AWDL_NETD_MDNS_PROXY=1` 启用 Python mDNS 的 UDP 代理。

## 当前状态（v2ray TUN 开启时已端到端成功）

- 2026-10-27 实测：`singbox_tun` Up、`ping ::1` 100% loss 的情况下，iPhone 15 Pro Max（iOS 17.5）仍通过 AirBridge32 传回 **2,911,531 字节 PNG**，PNG 签名/尺寸/SHA-256 均正确。
- 路径：radio 149 → 桥 → awdld IPC → `awdl_netd`（lwIP IPv6 + 阻塞 sockets）→ `receiver.py`。
- mDNS 复用 `mdns.py` 原记录，经 UDP 5353 ↔ 本地 15353 代理；TCP 8771 经 lwIP sockets + 阻塞代理转发到 `receiver.py`。
- `/Discover`、`/Ask`、`/Upload` 全部通过。
- **推荐 40 MHz**：实测 7,710,365 字节 PNG 用 **7.35 秒（~1.05 MB/s）**，TUN 仍为 Up。
- 宽度对比：20 MHz 约 261 KB/s；40 MHz 约 1.05 MB/s；80 MHz 即使修正 primary-channel offset（中心 155）后仍设备不可见，不可用。
- 调优参数：`TCP_WND=240 KiB` + 窗口缩放、`TCP_MSS=1360`、`TCP_SND_BUF=32 KiB`、lwIP/本地 socket 均开 `TCP_NODELAY`。
- 证据：`evidence/airdrop-userspace-tun-on.txt`、`evidence/airdrop-userspace-tuned.txt`、`evidence/airdrop-userspace-width40.txt`、`evidence/airdrop-userspace-width80-attempt.txt`（去敏）。

## 已知限制

- 吞吐较低；TCP 窗口/ACK/重传还可调优。
- TCP MSS 固定 1024，未处理 IPv6 分片（`LWIP_IPV6_FRAG=0`）。
- 原始接收日志含姓名/IP/令牌，未提交。

## 已知风险

- lwIP mDNS 的报文/记录是否与 iPhone 期望完全一致，需要实测；
- TCP 代理的握手、半关闭、TLS 透传需要实测；
- `awdld` 生成的 link-local 地址与 lwIP netif 的 MAC/地址必须一致；
- 目前未处理 IPv6 分片（`LWIP_IPV6_FRAG=0`），AirDrop 的 TCP MSS 应限制在 1440。
