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
5. python receiver.py --name AirBridge32 --listen 127.0.0.1 --outdir build\received --trace
```

`awdl_netd` 默认只启动 IPC + netif；`AWDL_NETD_TCP=1` 启用 TCP 8771 代理，`AWDL_NETD_MDNS=1` 启用 mDNS 广播（PoC 调试开关）。

## 当前状态

- lwIP IPv6-only 静态库和 `awdl_netd.exe` 构建通过；
- 假握手（`AWDL` + 16 字节 IPv6）可让 netif 起来并返回 mDNS/MLD 包；
- `awdld` 的 IPC host 后端构建通过；
- `receiver.py --listen 127.0.0.1` 代理模式可启动；
- **真实 awdld 已通过 IPC 连上 awdl_netd**（日志 `host interface awdl0, index 1`），awdld 侧出现 `tx data 9..12`，说明 mDNS/MLD 出向包已经进入 AWDL；
- 当前 `peers 0`，iPhone 尚未处于活跃 AWDL 状态/未被发现；**端到端收发待测**。
- mDNS 已改为每 2 秒周期公告，实例名/主机名使用 12 位十六进制 ID（与 WinAWDL 接收端一致）；报文含 `_airdrop._tcp`、SRV、TXT `flags=0x3fb`、AAAA。
- 诊断显示：iPhone 活跃时能收到它的 mDNS 响应（`_airdrop._tcp`、12 位实例、`flags=2039`），但 Windows 的公告尚未让 iPhone 建立 `/Discover` 连接。

## 已知风险

- lwIP mDNS 的报文/记录是否与 iPhone 期望完全一致，需要实测；
- TCP 代理的握手、半关闭、TLS 透传需要实测；
- `awdld` 生成的 link-local 地址与 lwIP netif 的 MAC/地址必须一致；
- 目前未处理 IPv6 分片（`LWIP_IPV6_FRAG=0`），AirDrop 的 TCP MSS 应限制在 1440。
