# Phase 0 技术审计（2026-10-27）

## 结论与证据边界

结论：**有条件继续研究；尚未达到 Phase 0 完整通过，禁止据此宣称 RTL8821CU 已能原生 AirDrop。** 可以继续做依赖补齐、硬件确认和独立 USB 测试；暂不投入整合应用、安装器和性能优化。

本报告以 docs/sources.lock.json 的五个完整提交为准。所有下述源码路径相对于 upstream/。静态源码及上游自述不能替代本机硬件测试。审阅覆盖 README、关键源码、构建配置、最近一次提交、近期 Issue/PR 摘要；没有穷尽所有提交历史，也未验证上游测试报告的原始仪器数据。

## A. RTL8821CU 支持程度

OpenIPC/devourer 不只是列出芯片名称：

- `devourer/CMakeLists.txt:29` 提供 `DEVOURER_JAGUAR2_8821C` 开关。
- `devourer/src/WiFiDriver.cpp:94` 记录 SYS_CFG2 0x00FC 芯片 ID 为 0x09，注释说明实测对象为 CF-811AC / RTL8811CU / 0bda:c811。
- `devourer/src/WiFiDriver.cpp:279` 的 USB 工厂分支实际创建 `RtlJaguar2Device(..., ChipVariant::C8821C, ...)`。
- `devourer/src/jaguar2/ChipVariant.h` 将 RTL8811CU / RTL8821CU 归入 C8821C。
- `devourer/examples/dwelltx/main.cpp:118` 默认 PID 包括 c811、c820；`examples/common/usb_select.cpp:32` 允许指定 VID/PID。不同 demo 的默认列表并不完全一致，不能将列表当成完整兼容矩阵。

本机只读 PnP 枚举已成功，未找到可确认的目标网卡（2026-10-27 04:19Z 的状态；同日 12:41 已接入 `0bda:c820`，见 I 节，芯片 ID 仍未读）。最初沙箱内枚举被拒绝，扩展权限后的枚举成功，不能将最初错误当成硬件缺失证据。完整本地结果在 evidence/usb-devices.local.json（不提交设备实例信息）。必须接入实物后核对 VID/PID、接口、驱动，再以芯片寄存器/初始化结果交叉确认；商品型号和 VID/PID 单独均不足以确认具体硅片版本。

上游 README 的 RTL8821CU 行没有独立吞吐量实测数值，8811CU 行有。源码部分注释使用“8821CU 实测”，不能保证与用户购买的板卡、Windows、AWDL 场景一致。`tests/8821cu_hwtest.sh` 实际是 Linux 内核驱动扫描测试，不能拿它证明 Windows 用户态无线链路。

## B. devourer USB 与 Windows 构建

能力清单（以下“已实现”仅指有源码路径）：

- 设备打开/接口选择：`src/UsbOpen.cpp::find_wifi_interface`、`claim_interface_then_reset`；按 vendor interface 的 bulk IN/OUT 端点选择 Wi-Fi 接口，并带互斥锁。
- 固件与寄存器：`src/jaguar2/HalJaguar2.cpp`、`hal/hal8821c_fw.c`、`src/jaguar2/Phy8821cTables.cpp`；芯片专用固件、PHY 表和 HAL 具备源码，Windows 下载成功尚未实测。
- USB 寄存器事务、队列和端点：`src/UsbTransport.cpp` 使用 libusb control/bulk/异步 transfer；`discover_endpoints` 不应改为盲目硬编码端点。
- 监听接收：`src/jaguar2/RtlJaguar2Device.cpp:511` 的 `Init`、`:596` 的 `StartRxLoop`。
- 指定帧发送：同文件 `:777 InitWrite`、`:1456 send_packet`、`:1478 send_packets`。
- 信道切换：同文件 `:1011 SetMonitorChannel`；函数存在不等于满足 AWDL 截止时间，需测尾延迟、丢帧和并发行为。
- 发送状态：同文件 `:714 DeliverTxReport`，存在 C2H 状态解析。USB 提交成功、空口发出、对端 ACK 是三个不同事件，需逐帧关联。
- ACK/聚合：同文件 `:383 SetAckResponder`、`:473 SetAmpduMode`；`IRadio.h` 有 BlockAck 等接口。接口也有默认不支持行为，必须核验 C8821C 实际返回、回读和空口结果，不能按类接口存在就全部判通过。
- 清理：同文件 `:2302 Stop` 停止运行线程并清除部分状态，但源码明确提示不等于关闭芯片供电；`UsbTransport.cpp` 有取消/释放 transfer 和销毁顺序要求。热拔插、取消、重新打开、崩溃后恢复均待测试。

五类证据：A 芯片理论能力不作为验收；B 上述路径已实现；C 上游有部分 C8821C 家族测试/注释；D 本机 Windows 真机未测；E 本机原生 AirDrop 未测。

实际构建尝试：使用 D:/VSBuildTools 中 CMake 3.31 / MSVC 19.44，成功识别 x64 编译器和 Windows SDK 10.0.26100，随后在 `CMakeLists.txt:178 find_package(PkgConfig REQUIRED)` 失败。日志：evidence/build-configure.log。**未完成配置、编译或运行。** 缺依赖不是源码不能编译的证据。

上游 `.github/workflows/cmake-multi-platform.yml:74` 包含 Windows + vcpkg/libusb 构建路径；这是构建工作流证据，本轮没有验证对应 CI run 是否成功。下一次应使用项目隔离依赖目录、正确设置 VCPKG_ROOT、安装 libusb 和可被 CMake 找到的 pkg-config，再以单芯片配置重试；不要全局执行 `vcpkg integrate install` 来改动用户环境。

Windows 设备绑定方案：目标是让 libusb 通过 WinUSB 访问专用 Wi-Fi 接口。具体 INF/签名/绑定与恢复必须在看到实物接口后验证；不能照搬 WinAWDL 的 PCIe regproxy 安装器。绑定后应按该接口不再供普通 Windows Wi-Fi 使用来规划，保留另一网卡/有线网络。安装通常涉及管理员权限；正常进程权限要实测。此次没有替换驱动、加载固件或发射帧。

## C. WinAWDL 的专用依赖

- `.gitmodules` 指向 `raph559/devourer` 的 `winawdl/pcie-windows` 分支，并非 OpenIPC 当前主线；锁定子模块为 `8d936de2a3805ce126e5236349afe40e645aaf9b`，已取回检查。
- `radio/CMakeLists.txt:17` 关闭 Jaguar2 8821C，开启 KESTREL_8852C、PCIE，并提供 regproxy include 目录。
- `radio/winawdl_radio.cpp:861` 创建 `PcieTransport::Config`，`:863 PcieTransport::Open("win", ...)`，`:870 WiFiDriver::CreateRadioPcie`。
- 同文件 `:643 serve_client` 直接接收 `PcieTransport&`，内部 `bus.tx_kick()` 批量提交 DMA 队列；不是纯 IRadio 可替换调用。
- `upstream/devourer/src/PcieTransportWin.cpp:66` / `:83` 分配锁定物理页；`:193` 等通过 `DeviceIoControl` 访问 regproxy。`PcieDmaAx.cpp` 为 AX DMA 数据路径。
- `driver/regproxy/`、`scripts/install-driver.ps1`、`installer/install.ps1` 属 PCIe 设备绑定/权限/签名路线，不能给 USB 网卡直接运行。
- `radio/winawdl_radio.cpp` 的 SET_CHANNEL 分支记录“staying on channel ... ignored”，运行中动态换信道当前被忽略。适配 USB 时必须处理这一语义缺口。

原样无线进程不能在不加载其 PCIe 驱动的情况下完成目标工作；重写 USB 无线进程后，理论上可保留上层命名管道，避免让 AWDL/AirDrop 依赖 regproxy。后者是设计推论，未集成验证。

## D. 可复用边界与拟修改文件

最佳接缝是 **命名管道 radio_link v3**，不是把 USB 库直接塞进 PCIe 构造函数：

- `radio/radio_link.h`：8 字节 little-endian 消息头，payload 最大 16384 字节；HELLO、RX/TX（radiotap + 802.11）、SET_CHANNEL、SET_BSSID_FILTER、SET_ADDRESS。
- `awdl/win/radio_client.c` / `.h`、`io_win.c`、`netutils_win.c` 是上层入口。先保持线格式，尽量不动 AWDL。
- 替换/新增 `radio/winawdl_radio.cpp` 的设备打开、RX 生命周期、TX 提交、flush、断线恢复和信道执行；不保留 PCIe tx_kick 假设。
- 修改 `radio/CMakeLists.txt` 为 USB/C8821C 构建目标并锁定所用 devourer；原有 PCIe 目标可保留，避免强行混用两个上游 API 版本。
- 修改 `scripts/build-radio.ps1` 与启动参数，指定唯一 USB 设备身份。Phase 1 前不改 app/installer。
- `radio/pipe_server.h`、抓包格式、部分 reorder/rate/block_ack 逻辑是候选复用；先核对许可证、旧分支接口差异和硬件假设。
- 管道 v3 没有独立 TX-status 消息类型；第一阶段用本地日志关联 TX report。若上层调度确实需要反馈，再双端版本化扩展协议，不把回执强行编码为 RX。
- `airdrop/transfer.py` 和 Windows app 是应用层候选；当前不是全原生单一二进制，还包含 Python/.NET 依赖，资源占用必须测量。

## E. 最小测试程序设计

先复用 devourer 的 rxdemo/txdemo 构建入口并审阅其默认行为，再编写薄控制入口；本轮不编写未经硬件验证的适配器空壳。

操作模式按风险分离：enumerate（只读）、open/init（会写芯片状态）、rx（有限时长）、channel（明确频道）、tx（显式指定帧和次数）、close。禁止自动广播、自动扫描发射或自动修改系统驱动。

每次会话记录 UTC 时间、源码 SHA、USB VID/PID/接口/端点、芯片 ID、驱动版本、频道/带宽、输入测试文件哈希、每帧序号、RX 元信息、TX submit/report/空口 ACK、退出原因。使用独立见证接收器或用户已有抓包设备判断是否真正发射；缺见证条件时将空口结果保持待验证，不购买新网卡。

控制路径串行，RX 线程可取消，日志有界；先默认不聚合，先固定频道稳定收发，再测频道切换和 ACK。退出必须先停止 RX/取消 transfer，再释放 radio/transport，最后释放 USB handle/context。

## F. 最可能失败的三个风险

1. 购入板卡或其固件/USB 驱动行为与已测 C8821C 不同：表现为打不开、固件失败或只能接收。先确认实物并独立初始化。
2. 原始收发可用但 ACK、换信道尾延迟或时间同步不满足 AWDL：发现设备仍可能无法双向传输。必须抓取双向数据和空口回执。
3. PCIe 分支优化、旧 API 和应用层互操作存在缺口：USB 替换可能破坏队列/重试语义，现代 iOS 的发现、TLS 和身份流程仍需真机验证。先冻结版本、保持 IPC 边界，再逐层验证。

## G. 许可证与维护状态

- WinAWDL 根 LICENSE 为 MIT，但 `radio/CMakeLists.txt` 明确无线进程为 GPL-2.0-only；OWL 是 GPL-3.0，OpenDrop 根 LICENSE 是 GPL-3.0；devourer 根 LICENSE 是 GPL-2.0 文本。不可因顶层 MIT 就把整个工程当 MIT。
- SwooshDrop 的原始前端为 MIT，第三方部分另有许可，且 README 明确依赖 WSL2，本路线只作为行为和交互参考。
- 保留进程间 IPC 分界可减少直接链接混合问题，但本报告不据此宣布许可证兼容性已全部解决；还需核对逐文件 SPDX、固件再分发条款及最终发布组合。本轮仅保存独立源码研究副本，未复制实现进主仓库。
- 版本锁文件保存最近提交时间；devourer 在 10 月 26 日仍有修复，Issue #499 记录后续事项。OWL 的现代 iOS 兼容提问仍开放；OpenDrop 有归档路径处理 PR #134 和 TLS 问题记录。它们是后续核查线索，不单凭 Issue 标题下漏洞或兼容性结论。

**投入判断：Go = 仅继续低成本构建与硬件可控性验证；Hold = AWDL USB 适配与产品化；尚无永久 No-Go 的证据。**

## H. 下一步

具体任务及验收见 PLAN.md。先接入目标网卡获取硬件身份，同时补齐项目隔离的构建依赖。两项完成后才安排带驱动恢复步骤的初始化/RX；发送需明确测试帧、频道和次数。Phase 0 只有在精确设备、Windows 构建、依赖许可范围和测试方案均确认后才可以关闭。

## I. 2026-10-27 续审：构建、设备与契约

### I.1 目标设备（只读）

设备于 12:41（本地）接入，只读 PnP 结果：

- `0bda:c820`，BusReportedDeviceDesc `802.11ac NIC`，复合设备由 `usbccgp` 管理，挂载在 `Port_#0003.Hub_#0006`（父 hub `05e3:0608`，外接 hub）。
- MI_00：Bluetooth Radio，驱动 `oem154.inf` 1.5.1012.3（Realtek），服务 `BTHUSB`。
- MI_02：Wi-Fi 厂商接口（class `ff/ff/ff`），驱动 `netrtwlanu.inf` 1030.38.712.2019（供应商标记为 Microsoft），服务 `RtlWlanu`，网络适配器 `WLAN 2`，状态 Disconnected。

结论：VID/PID 和 Windows 名称符合 RTL8821CU 的预期，但**不能证明硅片版本**。随后用一次只读 vendor control read 读取 `SYS_CFG2 0x00FC = 0x09`，确认是 RTL8821C（Jaguar2）；该读取没有 Init、没有固件下载、没有发帧，证据 `evidence/chip-id-read.log`。完整实例路径与序列号只留在 `evidence/usb-devices.local.json`（git 忽略），提交的摘要在 `evidence/hardware-identity.json`。

### I.2 Windows 构建（通过）

- 源码 OpenIPC/devourer `f7e0731`；C8821C-only；MSVC 19.44.35228.0；Windows SDK 10.0.26100.0；CMake 3.31.6；vcpkg 2026-09-26；libusb 1.0.30；pkgconf 3.0.3；OpenSSL 3.6.5。
- 依赖隔离在 `build/deps`，未执行 `vcpkg integrate install`，未改 PATH。
- 关键坑：CMake 的 PkgConfig 不会自动加 `--msvc-syntax`，必须设置 `PKG_CONFIG_ARGN=--msvc-syntax`，否则 MSVC 无法使用 `-L/-l` 形式的 libusb 参数。
- 产物：`devourer.lib`、`rxdemo.exe`、`txdemo.exe` 及全部自检程序；`ctest` 74/74 通过，3 项跳过（`la_csi_math`、两项 MT7612U，均与 8821C 无关）。
- 加密自检：安装 OpenSSL 后 `supplicant`、`station_sm`、`ccmp_framing` 构建并通过；`sta_client_headless` 是 Linux-only（TAP），Windows 按设计不构建。
- 细节与哈希见 `docs/BUILD_WINDOWS.md`，日志 `evidence/build-8821c-*.log`。这证明的是**Windows 编译与无硬件自检**，不是硬件可用。

### I.3 无线层契约与 8821C 缺口

见 `docs/RADIO_CONTRACT.md`，要点：

1. 管道 `radio_link` v3（MIT）是可复用接缝；USB 后端不需要改线格式。
2. WinAWDL 的 radio 不是接口无关的：`serve_client()` 直接持有 `PcieTransport` 并调用 `tx_kick()`，`main()` 用 `CreateRadioPcie()`；USB 必须新增一个 radio 主程序，不能只替换一个类。
3. `IRadio::AcceptBlockAck()` / `EndBlockAck()` 是 fork 为 8852C 新增的补丁（`57b6f14`），OpenIPC 上游从未包含，Jaguar2（8821C）也没有实现。结果：8821C 无法让硬件应答接收方向的 BlockAck，radio 的 `answer_block_ack()` 会提前返回，iPhone 不会获得聚合许可，接收退化为单 MPDU。这是功能降级，不是「聚合已支持」。
4. `SetStationIdentity`、`SetAmpduMode`、`GetTxStats` 在 Jaguar2 有源码实现，但均未在 Windows/USB/8821C 上实测。实机 `adapter.caps` 报告 `ack_responder=0`、`station_mode=0`、`tx_retry_limit=0`、`tx_no_agg=0`；这些是能力声明（很可能是尚未做空口验证），不能据此判定实现不存在，但 Phase 2 前必须用空口测试确认。
5. 换信道：PCIe 版被显式忽略；USB 的 `SetMonitorChannel()` 可做真实切换，但 AWDL 截止时间未测。

### I.4 许可证

`docs/LICENSE_MATRIX.md`：目标路线是 GPL-2.0 无线进程 + GPL-3.0 AWDL 进程 + MIT 应用层，靠进程边界隔离。**Realtek 固件再分发授权未解决**；安装包聚合边界需要法律确认；本仓库自身还没有 LICENSE。因此这项只能算「已识别风险」，不能算通过。

### I.5 Phase 0 状态

| 任务 | 状态 |
| --- | --- |
| 1. 真实设备身份 | 完成：VID/PID、接口、驱动、libusb open/claim、芯片 ID `0x09` 均有记录；WinUSB 回滚演练未做 |
| 2. Windows 构建 | 完成：C8821C-only 编译 + 74 项自检通过（含 3 项 OpenSSL 加密自检） |
| 3. API 契约对照 | 完成（静态）：见 `docs/RADIO_CONTRACT.md` |
| 4. 许可证与固件 | 识别完成，未解决：见 `docs/LICENSE_MATRIX.md` |

退出条件：设备身份（含芯片 ID）与 Windows 构建已达成；驱动绑定已执行且 libusb 可用，但恢复路径尚未实际演练。**Phase 0 仍未关闭，只差回滚演练。**

### I.6 下一步（需要用户授权才能越过）

1. 已执行 MI_02 → WinUSB 绑定（`oem155.inf`，服务 `WinUSB`）；剩余一次回滚到 `netrtwlanu` 的演练。
2. 授权后做最小 `Init`/固件下载（会写芯片状态），再进入固定频道有限 RX。
3. 按 PLAN Phase 1 做显式有限 TX、独立见证与 TX report 关联；任何 TX 都要先说明帧、频道和次数。

### I.7 首次 bring-up（2026-10-27 14:30 +08:00）

经用户授权后运行 `chipstate --init --channel 6 --peek 0x00FC`，退出码 0：

- USB：设备锁定 `1-8.3`，接口 2，端点 `0x84` bulk IN、`0x05/0x06/0x08` bulk OUT、`0x87` interrupt IN。
- 芯片：Boot from EFUSE / Autoload OK；`8821C cut=4 (UMC) 1T1R`，`SYS_CFG1=0x00494537`。
- 固件：下载 OK，FW boot ready（`0x80 = 0xC078`）。
- 射频：EFUSE `rfe_type=0x22`，BB/AGC/RF 表加载，信道 6（20 MHz），IQK 全通过，coex 设为 WL-only，RX 使能，`ready for TX`。
- 回读 `0x00FC = 0x09`；**零发帧**。
- 工具用 `teardown_power_down=false`，退出后芯片仍处于初始化状态；下一次 RX 前建议拔插/重新上电，避免把旧状态带进新会话。
- 证据：`evidence/init-first.log`。

结论：Phase 1 的「初始化/固件」项已通过一次；尚未做接收、发送、换信道和拔插恢复。Phase 0 仍只差 WinUSB 回滚演练。

### I.8 首次被动 RX（2026-10-27 14:32–14:33）

两次 `doctor --listen-secs`，均为零发帧：

| 信道 | 频段 | 时长 | 总帧 | FCS 正常 | CRC 错 | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| 6 | 2.4 GHz | 15 s | 750 | 500 | 250（33%） | HEALTHY |
| 36 | 5 GHz | 12 s | 6286 | 4956 | 1330（21%） | HEALTHY |

- 接收路径确实在工作；5 GHz 出现了 12668 字节的大完成，说明芯片侧聚合接收是通的。
- CRC 比例偏高，但这是拥挤环境下的单次样本，**不能当链路质量结论**；需要同信道三次重复和更干净的对照，之后再判断是否需要调 CCA/RX 参数。
- 同信道重复（频道 6，每次 10 s）：546 帧 / 485 正常 / 61 错，1037 / 663 / 374，502 / 425 / 77，CRC 比例 11%–36%。单次 CRC 主要由环境流量决定，没有受控对照就不能据此判断接收机好坏。
- 接收方向的 BlockAck 协议仍未验证；这里的聚合是环境 AP 发出的，不代表我们作为接收方能应答。
- 证据：`evidence/rx-first.log`、`evidence/rx-second-5g.log`。

### I.9 2.4 GHz 空口 TX 见证（2026-10-27 15:05 +08:00）

ESP32-S3-DevKitC-1 刷入纯监听固件（频道 6，不扫描/不连接/不发包，匹配发送地址 `57:42:75:05:d6:00`）。Windows 侧 `txdemo` 每轮发 20 个 probe request（1M，间隔 50 ms）。

| 轮次 | USB reset | 空中唯一序号 | 结果 |
| --- | --- | --- | --- |
| A | 跳过 | 0..19（20 个） | 通过 |
| B | 跳过 | 无 | **失败**：USB 报告 20 提交、0 失败，空中 0 帧 |
| C | 完整 | 0..19 | 通过 |
| D | 完整 | 0..19 | 通过 |
| E | 完整 | 0..19 | 通过 |

- 收到帧：长度 104，RSSI -11..-9 dBm（同桌面近距离），类型 0/子类型 4（probe request）；序号 0 的哈希 `9126b0f8` 在三次通过轮次中一致。
- **结论：2.4 GHz 空口发射已验证。** 但完整 USB reset 是重复测试的必要条件；跳过 reset 时会出现“USB 提交成功、空中零帧”，这正是必须用见证设备的原因。
- 见证计数有时每轮 +20、有时 +40（可能是抓包/重发重复）；唯一序号才是可靠指标。
- **限制：** ESP32-S3 只有 2.4 GHz，不能证明 5 GHz、硬件 ACK/BlockAck 或 AWDL 调度；RSSI 是近距离，不代表距离/吞吐。
- 证据：`evidence/tx-witness-summary.json`、`evidence/esp32-witness-tx*.log`、`evidence/tx-*.log`；固件与步骤见 `tools/esp32_witness/`。
