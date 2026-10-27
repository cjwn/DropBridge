# 无线层契约对照：WinAWDL radio ↔ devourer（Phase 0 任务 3）

状态：**静态源码对照完成，未编译、未上硬件**。对象为 `docs/sources.lock.json` 中的 WinAWDL `2196fb4` 与 OpenIPC devourer `f7e0731`，以及 WinAWDL 锁定的 devourer fork 子模块 `8d936de`（raph559/devourer，2026-10-12）。

## 0. 结论摘要

1. **管道协议是可用的接缝。** `radio_link` v3 是 MIT 头文件，消息为 8 字节小端头 + 载荷，上限 16384 字节；USB 后端不需要改这条线格式。
2. **radio 程序不是「换一个类」就能复用。** `serve_client()` 直接接收 `devourer::PcieTransport &bus` 并调用 `bus.tx_kick()`；`main()` 用 `CreateRadioPcie()` 和 `set_tx_kick_deferred()`。USB 没有这些调用，必须新增一个 USB radio 主程序（可以复用 `pipe_server.h`、`block_ack.h`、`rate_ladder.h`、`reorder_buffer.h`、`outgoing_session.h`、`frame_capture.h`）。
3. **接收方向硬件 BlockAck 在 8821C 上不可用。** `IRadio::AcceptBlockAck()` / `EndBlockAck()` 在锁定 fork 中只有 Kestrel 实现；Jaguar2（8821C）从未实现。OpenIPC 头部（`f7e0731`）则把这两个虚函数整个删除了（`src/IRadio.h` 相对锁定 fork 少 16 行）。因此：
   - radio 的 `answer_block_ack()` 会在 `AcceptBlockAck()` 返回 false 时提前返回，不发送 ADDBA 响应；
   - iPhone 不会得到对 Windows 的聚合许可，接收方向退化为单 MPDU；
   - 这是**功能可用、吞吐受限**的降级，不是「聚合已支持」。
4. 发送方向的 `SetStationIdentity`、`SetAmpduMode`、`GetTxStats` 在 Jaguar2 后端都有源码实现，但**均未在 Windows/USB/8821C 上实测**。
5. 频道切换在 PCIe 版被显式忽略（`winawdl_radio.cpp:695-700`）；USB 的 Jaguar2 有 `SetMonitorChannel()`，可以做成真实切换，但 AWDL 的切换截止时间必须在 Phase 1/2 实测。

## 1. 管道接缝

`upstream/winawdl/radio/radio_link.h`：

| 项 | 值 |
| --- | --- |
| 管道名 | `\\.\pipe\winawdl-radio` |
| 版本 | `RADIO_LINK_VERSION 3` |
| 头 | `uint16 type; uint16 reserved; uint32 length;`（小端） |
| 最大载荷 | 16384 字节 |
| 消息 | HELLO、RX、TX、SET_CHANNEL、SET_BSSID_FILTER、SET_ADDRESS |
| 许可证 | MIT（文件头 SPDX） |

TX/RX 载荷都是「radiotap 头 + 802.11 帧」。这与 devourer `send_packet()` 的要求一致（必须以 radiotap 开头），是两层之间最干净的契合点。

## 2. 需求映射

| # | radio 的需求 | PCIe 版实现位置 | 锁定 fork（Kestrel） | OpenIPC 头（8821C USB 后端） | 结论 |
| --- | --- | --- | --- | --- | --- |
| R1 | 命名管道服务 | `pipe_server.h` | 同左 | 无关 | 可直接复用 |
| R2 | 打开设备 | `PcieTransport::Open("win")` | `CreateRadioPcie` | libusb + `claim_interface_then_reset` | 必须新写 |
| R3 | 初始化到指定信道/带宽 | `InitWrite(SelectedChannel)` | 有 | 有 | 可复用 |
| R4 | RX 回调 + 元数据 | `StartRxLoop(cb)`，`Packet.RxAtrib` | 有 | 有 | 可复用；RSSI 约定已核对 |
| R5 | 发送 radiotap 帧 | `send_packet()` | 有 | 有 | 可复用 |
| R6 | 硬件 ACK 我们的单播帧 | `SetStationIdentity(own,bssid)` `:509` | 有（Kestrel） | Jaguar2 有 `RtlJaguar2Device.h:91` | 源码有，行为未测 |
| R7 | 发送方向 A-MPDU | `SetAmpduMode` `:591` | 有 | Jaguar2 有 `:97` | 源码有，未测 |
| R8 | 接收方向 BlockAck 应答 | `AcceptBlockAck` `:563`、`EndBlockAck` `:543/:710` | **仅 Kestrel** | **无（已从 IRadio 删除）** | **8821C 缺口** |
| R9 | TX 完成/放弃计数 | `GetTxStats()` `:470` | 有 | Jaguar2 有 `:100` | 需 RX 线程跑着才有 C2H |
| R10 | 运行时换信道 | 被忽略 `:695` | 无 | `SetMonitorChannel()` `:61` | 需要新实现并测时延 |
| R11 | 停止顺序 | `StopRxLoop` → `Stop` `:911-913` | — | 有 | 可复用，需测拔插 |

## 3. 关键代码引用

WinAWDL：

- `radio/winawdl_radio.cpp:643` `serve_client(PipeServer&, IRadio&, devourer::PcieTransport&, ...)`
- `:660` `tellings += bus.tx_kick();`
- `:861-870` `PcieTransport::Config` → `Open("win")` → `CreateRadioPcie`
- `:902` `card->set_tx_kick_deferred(...)`
- `:695-700` SET_CHANNEL 分支，日志 `"staying on channel ..., change to ... ignored"`
- `:509` `SetStationIdentity({own},{bssid})`
- `:563` `AcceptBlockAck({peer}, tid, ssn, granted)`
- `:591` `SetAmpduMode(mode)`
- `:709-710` 会话结束 `ClearAmpduMode()`、`EndBlockAck()`
- `:891` `StartRxLoop(...)`；`:911-913` `StopRxLoop()` → join → `Stop()`

devourer（OpenIPC 头）：

- `src/IRadio.h` 已无 `AcceptBlockAck` / `EndBlockAck`（与锁定 fork 的 diff 为 −16 行）
- `src/jaguar2/RtlJaguar2Device.h:55/60/61/72/73/81/91/97/100/142`：RX 循环、停止、换信道、InitWrite、发送、ACK 应答器、身份、A-MPDU、TX 统计、Stop
- `src/kestrel/RtlKestrelDevice.cpp:887`：锁定 fork 中唯一的 `AcceptBlockAck` 实现，且注释要求先 `SetStationIdentity`
- `src/RxPacket.h`：`RxAtrib.rssi` 为每径功率字节；Jaguar2 的 `FrameParserJaguar2.h:256` 注明 `dBm = value - 110`，与 radio 的 `kRssiFloorDbm = -110` 一致

## 4. 对后续阶段的决定

### Phase 1（不依赖 WinAWDL）

- 用 OpenIPC 头的 `rxdemo` / `txdemo` 直接验证 8821C USB 的初始化、RX、TX、TX report、Stop/拔插。
- 不在 Phase 1 引入 `radio`，避免把两个 API 版本和 PCIe 假设一起带进硬件调试。

### Phase 2（USB radio）

- 新增一个 USB radio 主程序，而不是改 `winawdl_radio.cpp` 的构造参数：
  - 保留管道协议与 `pipe_server.h`；
  - 设备打开改为 libusb（`UsbOpen.h` 的 `claim_interface_then_reset`）；
  - 删除 `tx_kick` / `set_tx_kick_deferred` 依赖，发送按消息同步提交；
  - `SET_CHANNEL` 调 `SetMonitorChannel()`，记录每次切换耗时；
  - `SetStationIdentity`、`SetAmpduMode`/`ClearAmpduMode`、`GetTxStats` 按当前 radio 语义保留；
  - BlockAck：先显式关闭接收方向聚合（不调用 `AcceptBlockAck`），并在日志中记录 ADDBA 请求次数；是否实现软件 BlockAck 应答，等 Phase 1 的聚合数据再决定。
- 版本策略：**Phase 2 必须只锁定一个 devourer**。当前两个候选：
  - OpenIPC `f7e0731`：8821C USB 更新，但 `IRadio` 没有 BlockAck 接收接口；
  - raph559 fork `8d936de`：有接口，但落后两周，且只有 Kestrel 实现该接口。
  任何跨版本混用都会把两个 API 的假设混在一起，不允许。

## 5. 尚未验证、不能下结论的点

- iPhone 在 ADDBA 未完成时是否仍会重发请求、是否影响发现/连接建立。
- 8821C 的 `SetStationIdentity` 是否真的让硬件回 ACK（这是 Phase 1/2 的核心验收项）。
- `SetMonitorChannel` 在 RX 循环运行时的切换耗时和丢帧（AWDL 截止时间）。
- `GetTxStats` 在 Jaguar2 + Windows USB 上是否按帧增长（C2H 解析路径）。
- 1T1R、20/40/80 MHz 在目标链路上的实际吞吐。

这些点已转成 Phase 1 的测试项，见 `docs/PLAN.md` 与 `docs/BUILD_WINDOWS.md`。
