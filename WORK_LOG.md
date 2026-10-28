# 工作记录 / 下一位 Agent 交接

## 2026-10-27：研究工程建立

用户要求规划并搭建项目，先做 Phase 0；每次完成改动后本地中文 Git 提交。

完成：任务书完整归档；五个公开上游克隆到 upstream/ 并锁版本；WinAWDL 的专用 devourer 子模块已克隆；审计报告、阶段计划、AGENTS.md、只读 USB 枚举脚本和工程检查脚本已建立。

实测：VSBuildTools 位于 D:/VSBuildTools；CMake 识别 MSVC x64 后缺 PkgConfig，日志 evidence/build-configure.log。没有编译成功或运行无线测试。USB PnP 只读枚举成功，但未找到可确认的 RTL8821CU；本地设备实例结果被 .gitignore 排除。

发现：WinAWDL radio 与 PCIe transport/tx_kick 耦合；运行中换频道被忽略；OpenIPC devourer 有 C8821C USB 真实实现，但不能据此声称用户网卡 Windows AirDrop 通过。SwooshDrop 依赖 WSL2，不适用最终运行路线。

边界：没有运行上游安装器/硬件脚本，没有改网络或驱动，没有加载固件、发射帧或复制第三方实现进入项目。主仓库无远程推送。

接续顺序：README → PHASE0_AUDIT → PLAN → sources.lock.json。先确认接入的目标设备，补齐本地构建依赖，再申请针对具体网卡的绑定实验。Phase 0 尚未全部完成，不能把本轮文档交付当成 AirDrop 可用。

验证命令：python tools/check_project.py；tools/Inspect-Hardware.ps1；git diff --check；git status --short。

## 2026-10-27：Phase 0 续审（Windows 构建通过、目标网卡接入）

用户要求全面扫描项目并接手。按约定先读 README、PHASE0_AUDIT、PLAN、WORK_LOG，再推进不依赖驱动变更的 Phase 0 项。

完成：

- 在 `build/deps` 隔离安装 vcpkg 2026-09-26、libusb 1.0.30、pkgconf 3.0.3；没有 `vcpkg integrate install`，没有改 PATH。
- C8821C-only 配置与编译通过（MSVC 19.44.35228.0、SDK 10.0.26100.0、CMake 3.31.6）；产物 `devourer.lib`、`rxdemo.exe`、`txdemo.exe`；`ctest` 71/71 通过，3 项跳过；日志 `evidence/build-8821c-{configure,build,ctest}.log`。
- 关键构建坑：CMake 的 PkgConfig 不会自动加 `--msvc-syntax`，必须设置 `PKG_CONFIG_ARGN`，否则 MSVC 链接器无法使用 `-L/-l`。
- 新增可复现脚本 `tools/Setup-BuildDeps.ps1`、`tools/Build-Devourer.ps1`，已端到端跑通（exit 0）。
- 目标网卡于 12:41 接入并完成只读身份采集：`0bda:c820`，复合设备；MI_00 蓝牙（`oem154.inf` 1.5.1012.3），MI_02 Wi-Fi 厂商接口 `ff/ff/ff`（`netrtwlanu.inf` 1030.38.712.2019，`RtlWlanu`，适配器 `WLAN 2` 未连接）。**芯片 ID 未读**。摘要 `evidence/hardware-identity.json`，完整实例路径只在本地忽略文件。
- 契约对照：`AcceptBlockAck`/`EndBlockAck` 是 fork 为 8852C 新增、上游从未包含，且只有 Kestrel 实现；Jaguar2 没有实现 → 8821C 接收方向硬件 BlockAck 不可用；radio 与 `PcieTransport::tx_kick` 耦合，USB 必须新增 radio 主程序。fork 基于 OpenIPC `5be2b72` 加 13 个补丁，比上游头落后约两周。见 `docs/RADIO_CONTRACT.md`。
- 许可证矩阵 `docs/LICENSE_MATRIX.md`：GPL-2.0 radio + GPL-3.0 awdl + MIT 应用层靠进程边界隔离；Realtek 固件再分发未解决。
- `tools/Inspect-Hardware.ps1` 增加目标 VID/PID 检测并改为 ASCII 输出（原中文在 PS 5.1 无 BOM 下会乱码）；`tools/check_project.py` 扩展到新文档/证据。
- 文档同步：README、PHASE0_AUDIT（I 节）、PLAN、BUILD_WINDOWS、HARDWARE_INTAKE、RADIO_CONTRACT、LICENSE_MATRIX。

未做（等待明确授权）：

- 未做芯片 ID 读取、WinUSB 绑定、任何固件下载或无线帧发送；没有改驱动、网络或防火墙。

下一步：先确认是否授权对 MI_02 做 WinUSB 绑定并执行一次最小初始化。建议把网卡直连主板 USB 口（当前经 `05e3:0608` hub）。授权后按 PLAN Phase 1 顺序：芯片 ID → 固定频道 RX → 有限 TX → 独立见证与 TX report → 换信道 → 拔插恢复。

验证命令：python tools/check_project.py；tools/Inspect-Hardware.ps1；tools/Build-Devourer.ps1；git status --short。

## 2026-10-27：绑定手册、只读 USB 探针、OpenSSL 自检

用户确认交付形态：**用户看到一个程序**（一个安装包/托盘入口）即可，后台允许多进程。已写入 `docs/PLAN.md` 的架构决定：GPL-2.0 无线进程 + GPL-3.0 AWDL 进程 + MIT 应用层，不承诺单进程单 exe。

完成：

- `docs/BINDING_RUNBOOK.md`：Zadig 绑定 MI_02 到 WinUSB 的逐步操作、只读校验、回滚和失败处理。AI 不执行绑定；需要管理员权限和用户授权。
- `tools/Check-Binding.ps1`：只读报告 MI_02 的驱动/服务/问题码，退出码 0=WinUSB、1=仍是厂商驱动、2=设备不在。当前基线为 `RtlWlanu`。
- `tools/usb_probe/` + `tools/Build-UsbProbe.ps1`：libusb 只读探针，只做 open + 打印描述符/端点 + claim/release，不初始化芯片、不写寄存器、不发帧。
- 绑定前基线（`evidence/usb-open-baseline.log`）：设备可枚举（bus 1、address 19、High Speed、`bcdUSB 2.00`），`libusb_open` 返回 `LIBUSB_ERROR_NOT_FOUND`，与厂商驱动占用 MI_02 的预期一致。
- OpenSSL 3.6.5 装入 `build/deps` 后重建：`ctest` 74/74 通过（3 项跳过），`supplicant`、`station_sm`、`ccmp_framing` 三项加密自检通过；`sta_client_headless` 是 Linux-only（TAP），Windows 不构建。产物哈希不变。
- 首次重建遇到 `CMake error : Cannot restore timestamp ... generate.stamp`（MSB8066），重跑一次即通过；已写入 `docs/BUILD_WINDOWS.md` 的“已知构建异常”。
- 更新 README、PLAN、PHASE0_AUDIT、BUILD_WINDOWS、HARDWARE_INTAKE、check_project.py。

未做：

- 没有绑定 WinUSB、没有读芯片 ID、没有 Init/固件下载、没有任何 TX；没有改驱动/网络/防火墙。

下一步：用户以管理员按运行手册绑定 MI_02 → 我用 `usb_probe.exe` 验证 open/claim → 再单独申请 `Init` 授权。

## 2026-10-27：WinUSB 绑定成功，libusb 可访问 interface 2

用户授权并由用户完成 UAC 与 Zadig 界面操作；AI 负责下载校验、监测与验证。

完成：

- 从官方 release 下载 `zadig-2.9.exe`（SHA-256 `4ECAA95D…82039B`，Authenticode 签名有效，签名者 Akeo Consulting）。
- 用户以管理员在 Zadig 2.9.788 中选择 `802.11ac NIC (Interface 2)`（USB ID `0BDA C820 02`），`RtlWlanu` → `WinUSB`，执行 Replace Driver。
- 14 秒后监测到 MI_02 变为 `oem155.inf`（libwdi 6.1.7600.16385），服务 `WinUSB`。
- 蓝牙 MI_00 仍为 OK；`WLAN 2` 网络适配器消失（预期）。
- `tools/Check-Binding.ps1` 退出码 0；`usb_probe.exe` 输出 `open OK`，成功 claim interface 2；接口 2 端点：bulk IN `0x84`、bulk OUT `0x05/0x06/0x08`、interrupt IN `0x87`。
- 证据：`evidence/binding-result.log`、`evidence/hardware-identity.json` 的 `binding` 段。

未做：没有 Init、固件下载或任何无线帧；回滚步骤尚未演练。

下一步：只读读取 `SYS_CFG2 0x00FC` 确认芯片 ID，再决定是否申请 Init 授权。

## 2026-10-27：只读确认芯片 ID = 0x09（RTL8821C）

- 扩展 `usb_probe` 支持 `--read8` / `--read32`：一次 Realtek vendor control read（`0xC0`、`bRequest 5`），不写寄存器、不下载固件、不发帧。
- 运行 `usb_probe.exe 0bda c820 --read8 00FC`，在 claim interface 2 之后读取 `SYS_CFG2`，得到 `read8 0x00fc = 0x09`，随后 release/close。退出码 0。
- 证据：`evidence/chip-id-read.log`；`evidence/hardware-identity.json` 的 `chipIdentity` 已标为 `verified`、值 `0x09`、含义 RTL8821C（Jaguar2）。
- 结论：Phase 0 的「真实设备身份」完成（VID/PID、接口、驱动、libusb 访问、芯片 ID）。仍未做：WinUSB 回滚演练、`Init`/固件下载、任何 RX/TX。
- 下一步：回滚演练（恢复到 `netrtwlanu.inf`）以关闭 Phase 0；之后申请最小 `Init` 授权进入 Phase 1。

## 2026-10-27：首次 bring-up 成功（频道 6，零发帧）

用户单独授权后执行 `chipstate --init --channel 6 --peek 0x00FC`，退出码 0。

- USB：锁定 `1-8.3`，接口 2，`0x84` bulk IN、`0x05/0x06/0x08` bulk OUT、`0x87` interrupt IN。
- 芯片：`8821C cut=4 (UMC) 1T1R`，`SYS_CFG1=0x00494537`；Boot from EFUSE / Autoload OK。
- 固件：下载 OK，FW boot ready `0xC078`。
- 射频：EFUSE `rfe_type=0x22`，BB/AGC/RF 表加载，信道 6（20 MHz），IQK 通过，coex WL-only，RX 使能，`ready for TX`。
- 回读 `0x00FC=0x09`；**零发帧**。工具 `teardown_power_down=false`，退出后芯片仍初始化。
- 证据：`evidence/init-first.log`；`hardware-identity.json` 增加 `initialization` 段。

未做：RX 循环、TX、换信道、拔插恢复、WinUSB 回滚演练。

下一步：写一个有界、可干净停止的 RX 探针（或用 rxdemo 的可控停止方式），在频道 6 做有限时长被动接收；需要单独确认后再跑。

## 2026-10-27：两次限时被动 RX（频道 6 / 36，零发帧）

用上游 `doctor --listen-secs`（限时 RX smoke，干净停止，不发帧）：

- 频道 6（2.4 GHz，15 s）：750 帧，500 FCS 正常 / 250 CRC 错，判定 `HEALTHY`。
- 频道 36（5 GHz，12 s）：6286 帧，4956 FCS 正常 / 1330 CRC 错，判定 `HEALTHY`；出现 12668 字节的大完成，说明芯片侧聚合接收在工作。
- 证据：`evidence/rx-first.log`、`evidence/rx-second-5g.log`；`hardware-identity.json` 增加 `rxSmoke` 两段。

判断：接收路径真实可用；CRC 比例（2.4 GHz 33%、5 GHz 21%）偏高，但只是单次样本，不能当链路质量结论，需要同信道三次重复和更干净的对照。接收方向 BlockAck 仍未验证。

未做：任何 TX、换信道、拔插恢复、WinUSB 回滚。TX 需要独立见证，不能用 USB 提交成功代替空口结果。

下一步：同信道 RX 三次重复；同时准备 TX 见证方案（第二台设备或抓包手段），否则不进入 TX。

## 2026-10-27：频道 6 RX 三次重复

每次 10 秒，仍是 `doctor --listen-secs`，零发帧：

| run | 总帧 | FCS 正常 | CRC 错 | CRC 比例 |
| --- | --- | --- | --- | --- |
| 1 | 546 | 485 | 61 | 11% |
| 2 | 1037 | 663 | 374 | 36% |
| 3 | 502 | 425 | 77 | 15% |

结论：CRC 比例随环境流量在 11%–36% 波动，单次样本不能当链路质量；需要受控对照（固定发射源或安静信道）。接收路径本身仍判定 HEALTHY。

证据：`evidence/rx-repeat-1..3.log`、`evidence/rx-repeat-summary.log`。

下一步：TX 需要独立见证；在找到第二台可抓包设备前，不把“USB 提交成功”当作空口验证。

## 2026-10-27：ESP32-S3 见证 2.4 GHz 空口 TX 成功

用户插入 ESP32-S3-DevKitC-1（CH343，COM17），授权覆盖原固件。使用本机已有 PlatformIO 6.2.0（`~/.platformio/penv`）与 `espressif32@7.0.1`，未新装大工具链。

- 写入 `tools/esp32_witness/`：纯监听固件（频道 6、不扫描/不连接/不发包、匹配 SA `57:42:75:05:d6:00`）+ 串口抓取脚本。编译、刷写均成功。
- Windows 侧 `txdemo` 每轮发 20 个 probe request（1M，50 ms 间隔）。
- 结果：完整 USB reset 的 3 轮全部收到 **20/20 唯一序号 0..19**（长度 104，RSSI -11..-9 dBm，seq 0 哈希 `9126b0f8` 跨轮一致）。
- 负面对照：跳过 reset 的一轮，`txdemo` 报告 20 帧提交、0 失败，但空中 **0 帧**；因此完整 reset 是重复 TX 的必要条件，也证明“USB 提交成功 ≠ 空口发出”。
- 见证计数有时每轮 +20、有时 +40，疑似抓包/重发重复；唯一序号是可靠指标。
- 证据：`evidence/tx-witness-summary.json`、`evidence/esp32-witness-tx{2,3,4,5,6}.log`、`evidence/tx-{second,third,fourth,fifth,sixth}.log`、`tools/esp32_witness/README.md`。

未做：5 GHz、硬件 ACK/BlockAck、反向（ESP32 发、网卡收）、换信道、拔插恢复、WinUSB 回滚。

下一步：反向验证与 ACK 方案；ESP32 只能覆盖 2.4 GHz，5 GHz/AWDL 仍需别的见证。

## 2026-10-27：确定 Dart 应用层 + C++ radio 后端

用户要求用 Dart，接受“Dart 负责应用层与 AWDL/AirDrop、C++ 保留无线后端”的建议。

- 新增 `docs/ARCHITECTURE.md`：一个用户可见程序；Dart/Flutter 应用通过 MIT 的 `radio_link` v3 管道驱动 C++ `radio.exe`；`radio.exe` 链接 devourer、独占 USB，不解析 AWDL。
- 明确不采用纯 Dart 无线层：devourer 无 C ABI、固件/PHY/USB 时序留在 C++；FFI shim 作为以后的可选方案，不是第一阶段。
- 许可证边界：radio.exe GPL-2.0，复用的 AWDL 进程 GPL-3.0，Dart 应用只走 MIT 管道；固件再分发仍未解决。
- AWDL 的目标实现是 Dart；Phase 3 可先用 C++ AWDL 作对照以尽快验证 iPhone 发现，迁移范围按实测决定。
- 更新 PLAN/README/check_project。

下一步：Phase 2，写 `radio.exe` 的 USB 后端（打开/初始化/RX/TX/换信道 + 管道），保持 `radio_link` v3。

## 2026-10-27：USB radio 后端 WIP（HELLO 通过，硬件 RX 转发待修）

- 新增 `src/radio_usb/`：C++ `airbridge_radio.exe`，链接 devourer + libusb；打开 `0bda:c820`、claim interface 2、`InitWrite`、`StartRxLoop`；实现 `radio_link` v3 的 HELLO/RX/TX/SET_CHANNEL/SET_BSSID_FILTER/SET_ADDRESS。
- 编译通过；运行后 Python 客户端能收到 HELLO（version 3、channel 6、width 20）。
- 用临时“合成 RX”消息验证了管道方向：主线程写入的 10 字节 RX，客户端能完整收到。
- **未通过：硬件 RX 转发。** RX 回调有计数，但从 RX 线程写管道时客户端在窗口内收不到；客户端关闭后写入返回 `ERROR_NO_DATA (232)`。问题集中在跨线程写或 RX 启动时序，尚未定位。
- 反复 reset 后出现一次固件启动失败（`FW ready 0x80 != 0xC078`），devourer CPU-reset 重试后恢复，`doctor` 判定 `HEALTHY`。
- 发现：Dart 的 `dart:io` 没有 Windows 命名管道接口，IPC 应改为 localhost TCP 或 stdio 长度前缀帧；`radio_link` 帧格式可保留。
- 状态与复现步骤：`docs/RADIO_USB.md`；客户端：`tools/radio_link_client.py`。
- 未做：TX over pipe、SET_CHANNEL、地址/过滤、Dart 客户端。

下一步：先定 IPC（TCP/stdio），再修 RX 转发并重测；不要在未通的情况下接 AWDL。

## 2026-10-27：radio 后端改用 localhost TCP，硬件 RX 打通

- 按 Dart 兼容性，把 IPC 从 Windows 命名管道改为 **localhost TCP 127.0.0.1:49876**，`radio_link` v3 帧格式不变；端口用 `SO_EXCLUSIVEADDRUSE`，避免 `SO_REUSEADDR` 被别的本地服务抢连。
- 修了一个协议 bug：TCP 版 HELLO 漏发 8 字节消息头，客户端把 HELLO 当成了 type=3/len=5126。
- RX 回调改为 **有界队列 + 独立写线程**，慢客户端不再阻塞 USB RX 环。
- 实测 RX：客户端 10 秒收到 **311 帧、28957 字节**，含真实 beacon，RSSI 约 -114..-121。
- TX：客户端经 TCP 发 20 帧，radio 记录 `tx sent 20 failed 0`，但 ESP32-S3 同时段空口匹配 0 帧；尚未定位（怀疑 RX 环启动时序或 TX 队列/CCA）。
- 启动耗时在反复 reset 后可达约 2 分钟，并出现过一次固件启动失败后自动重试成功。
- 证据：`evidence/radio-usb-run-rx.log`、`evidence/radio-usb-run-tx.log`、`evidence/esp32-witness-radio-tx.log`；状态见 `docs/RADIO_USB.md`。

下一步：让 RX 环先于客户端 accept 启动（或 HELLO 后等 RX 就绪）再测 TX；通过后再测换信道与 Dart 客户端。

## 2026-10-27：RX 先于 accept；TX 对照显示网卡需重新上电

- 把 RX 环提前到 `accept` 之前启动，TX 帧加 20 ms 间隔，排除“TX 早于 RX 环”的时序问题。
- 复测：TCP TX 提交 20 帧、0 失败，但 ESP32 空口仍 0 帧。
- **对照实验**：用已知能上空的 `txdemo`（`DEVOURER_SKIP_RESET=1`）再发 20 帧，ESP32 同样 0 帧；RX 仍正常（radio 侧 `rx cb=100`、客户端 47 帧/1.5 s）。
- 结论：当前不能判定后端 TX 有 bug；网卡在多次 reset 后疑似进入 TX 不工作状态，需要拔插/重新上电后再测。
- 证据：`evidence/esp32-witness-radio-tx2.log`、`evidence/esp32-witness-control-tx.log`、`evidence/radio-usb-run-tx2.log`、`evidence/tx-control.log`。

下一步：拔插 RTL8821CU，先复跑 `txdemo` 对照；确认空口恢复后，再复测 TCP TX。

## 2026-10-27：TCP RX + TX 打通（2.4 GHz，ESP32 见证 20/20）

- 拔插后 `txdemo` 对照恢复 20/20，确认之前的 0 帧是网卡 TX 假死，不是后端结论。
- 复测 TCP TX 仍 0；逐步排除 RX 并发（`AIRBRIDGE_NO_RX=1` 也 0）和 TX 速率（补 `SetTxMode("1M")` 仍 0）。
- **根因**：`tools/radio_link_client.py` 解析 `txdemo` 的 `beacon_frame` 时，把源码注释里的 `0x40 0x00`、`0x08 0x01` 当成帧字节，body 从 100 字节变成 104 字节，帧头错位；芯片收到畸形帧，`send_packet` 仍返回 true。
- 修复：解析前去掉 `//` 与 `/* */` 注释。修复后：
  - `AIRBRIDGE_NO_RX=1`：ESP32 收到 20/20 唯一序号 0..19，RSSI -10，长度 104。
  - 正常模式（RX 环同时运行）：同样 20/20，RSSI -10..-9，长度 104；radio 侧 `tx sent 20 failed 0`，RX 仍工作。
- 新增 `AIRBRIDGE_TX_RATE`（默认 1M，`SetTxMode`）与 `AIRBRIDGE_NO_RX` 测试开关。
- 证据：`evidence/esp32-witness-radio-tx6.log`、`evidence/esp32-witness-radio-tx7-full.log`、`evidence/radio-usb-run-tx7-full.log`。

下一步：TCP `SET_CHANNEL` 实测、`SET_ADDRESS`/BSSID 过滤与硬件 ACK，然后 Dart 客户端。

## 2026-10-27：SET_CHANNEL、BSSID 过滤、Dart 客户端通过

- **SET_CHANNEL**：客户端从频道 6 发 `SET_CHANNEL 36`，radio 日志 `channel -> 36`；切换后 2 秒收到 64 帧，说明运行时换信道可用。
- **BSSID 过滤**：同一会话里 baseline=64 帧；设置假 BSSID `00:11:22:33:44:55` 后 2 秒 **0 帧**；发送全零清除后 2 秒恢复 **62 帧**。全零现在表示清除过滤。
- **Dart 客户端**：新增 `tools/radio_link_client.dart`，`dart analyze` 通过；运行后收到 `HELLO version=3 channel=6 width=20`，5 秒收 **84 帧 / 14664 字节**。Dart `Socket` 可用，TCP IPC 选择成立。
- 证据：`evidence/radio-usb-run-channel.log`、`evidence/radio-usb-run-dart.log`。

仍缺：SET_ADDRESS 的硬件 ACK 效果、断连重连（当前 radio 单客户端、断开即退出）、5 GHz/AWDL。

## 2026-10-27：SET_ADDRESS/SetStationIdentity 实机 armed

- 第一次用 BSSID `57:42:75:05:d6:00` 被拒：首字节 0x57 是组播地址；devourer 要求 own 与 BSSID 都是非零单播且不同。
- 换单播 BSSID `56:42:75:05:d6:00` 后成功：`station identity armed ... (net_type=Infra)`，客户端断开时正常 cleared。
- devourer 警告 `tx.retry_limit=0`：MAC 不会重传未确认帧；AWDL 需要时应在 `DeviceConfig::tx.retry_limit` 设置。
- 仍未验证：空口 ACK（需要 iPhone/第三台观察设备）、断连重连、5 GHz。
- 证据：`evidence/radio-usb-run-address.log`、`evidence/radio-usb-run-address2.log`。

## 2026-10-27：radio 支持断连重连

- `radio.exe` 改为 accept 循环：radio 保持初始化，客户端断开后回到监听，不再退出进程。
- 每个客户端会话重建 RX 队列/过滤状态/写线程，RX 计数器按会话清零。
- 实测：同一 radio 进程连续两个客户端都收到 HELLO 并收包（1 帧 / 5 帧）；第二个客户端之后还能发 5 帧 TX（`tx sent 5 failed 0`）。
- 证据：`evidence/radio-usb-run-reconnect.log`；状态更新 `docs/RADIO_USB.md`。

下一步：`tx.retry_limit`（当前 0，不重传）、空口 ACK 验证、5 GHz/AWDL。

## 2026-10-27：retry_limit、5 GHz RX 与跨信道多客户端

- `AIRBRIDGE_RETRY_LIMIT` 默认 7（0..63）；设 7 后 `SetStationIdentity` 不再警告 `retry_limit=0`，station identity armed 正常。
- 同一 radio 进程连续两个客户端：
  - 客户端 A：`SET_CHANNEL 44`，频道 44 baseline 47 帧、假 BSSID 过滤 0 帧、清除后 63 帧（5 GHz RX 通过）。
  - 客户端 B：HELLO 显示当前频道 44；`SET_CHANNEL 6`、设置 own/BSSID、发 20 帧 TX；ESP32-S3 收到 20/20 唯一序号，RSSI -11..-9。
- 证据：`evidence/radio-usb-run-multi.log`、`evidence/esp32-witness-multi.log`；状态更新 `docs/RADIO_USB.md`。

仍缺：空口 ACK 验证、5 GHz TX 见证（ESP32 只能 2.4 GHz）、AWDL 信道调度。

## 2026-10-27：Phase 3 准备完成（awdld + Wintun + 管道桥）

- 取回 OWL 与 radiotap 子模块（`upstream/winawdl/upstream/owl`）。
- 用 WinAWDL 的 `fetch-wintun.ps1` 下载 Wintun 0.14.1，SHA-256 与 WireGuard LLC 签名校验通过。
- 用 vcpkg 已下载的便携 MinGW（GCC 13.2）构建 `build/awdl/awdld.exe`；能打印 usage。
- 新增 `src/radio_usb/radio_pipe_bridge.cpp`：命名管道 ↔ TCP 字节泵，不改 `radio_link` 协议；已用命名管道客户端验证收到 `HELLO version=3 channel=6 width=20`。
- 新增 `tools/Build-Awdl.ps1` 和 `docs/PHASE3_AWDL.md`（运行顺序、授权边界、失败处理）。
- **未运行 awdld**：它会创建 Wintun 虚拟网卡并在频道 44 发射 AWDL 帧，需要用户明确授权。

下一步：获得授权后按 PHASE3_AWDL.md 启动 radio + bridge + awdld，用 iPhone 做第一次 AWDL 发现测试。

## 2026-10-27：AWDL 控制面同步成功，数据面被沙箱 IPv6 防火墙阻塞

- 用便携 MinGW 构建 `awdld.exe`，Wintun 0.14.1 已校验；`radio_pipe_bridge` 验证命名管道 → TCP。
- 实测（iPhone 15 Pro Max / iOS 17.5，隔空投送已打开）：
  - 频道 44 只有普通 AP；频道 149 有大量 AWDL action 帧。
  - `awdld -c 149` 提权运行后创建 Wintun `awdl0`（ifIndex 68），`peers 2`，同步到 iPhone AWDL 地址 `62:65:31:c5:29:59`，`rx action` 持续增长。
  - `rx data 3 / tx data 0`；AirDrop 接收服务已广播 AirBridge32，但 iPhone 没看到设备。
- 根因：当前 DSH 会话的 Codex 沙箱离线防火墙规则（`codex_sandbox_offline_block_outbound` 等）禁止所有非回环 IPv6；`ping ::1`、链路本地、全局 IPv6 全部 `General failure`，IPv4 回环正常。详见 `evidence/awdl-sandbox-firewall.txt`。
- 未修改任何防火墙规则；测试进程已停止。
- 下一步：在沙箱外的普通管理员终端运行，或明确授权临时停用三条 block 规则并在测试后恢复。

## 2026-10-27：首次真实 AirDrop 传输成功（iPhone → Windows）

- 关闭 v2rayN/TUN 后 IPv6 恢复（`ping ::1`、链路本地均正常），确认首轮失败是 v2ray TUN 的 `EnableIPv6=false` + `StrictRoute=true` 导致，不是沙箱防火墙。
- 按 PHASE3_AWDL.md 启动 radio 149 + 桥 + 提权 awdld + `receiver.py --name AirBridge32`。
- iPhone 15 Pro Max（iOS 17.5）在隔空投送中看到 AirBridge32，依次 `POST /Discover`、`POST /Ask`、`POST /Upload`，传输一张 PNG。
- 结果：`IMG_3583.PNG`，2,911,531 字节，PNG 签名有效，1290×2796，SHA-256 `C899358AE8D761C900946DD24F49DBC41F1CE04B05C859B9C071CF8F822C48B1`，约 10.1 秒。
- AWDL 计数：`peers 1`，`rx data 2166 (2025 joined)`，`tx data 191`。
- 证据：`evidence/airdrop-first-transfer.txt`（去敏）；原始接收日志含发送者姓名/IP/令牌，已加入 `.gitignore`，不提交。
- 待办：v2ray TUN 开启时的 IPv6/link-local 排除或系统代理模式；5 GHz 空口 TX/ACK；Dart/Flutter UI 与打包。

## 2026-10-27：用户态 AWDL 数据面 PoC 骨架

- 目标：TUN 常开且不改 v2ray 默认设置时，让 AirDrop 绕过 Windows IPv6 栈。
- 取回 lwIP 2.2.2，新增 `src/awdl_netd`：IPv6-only / NO_SYS / mDNS / TCP 用户态栈；构建通过，假握手可让 netif 起来并回 mDNS/MLD 包。
- 新增 awdld 的 IPC host 后端（`host_if_ipc.c`，`-DAWDL_HOST_IPC=ON`），awdld 不再创建 Wintun；构建通过。
- `receiver.py` 增加 `--listen 127.0.0.1` 代理模式，关闭自带 mDNS，TLS/HTTP/Upload 逻辑不变。
- 由于 `upstream/` 被忽略，改动以 `patches/awdl-ipc-host.patch` + `tools/Apply-AwdlPatches.ps1` 跟踪。
- 状态：骨架/构建通过；**尚未接真实 radio/iPhone 做端到端传输**。
- 文档：`docs/USERSPACE_DATAPATH.md`。

## 2026-10-27：用户态数据面接通 awdld IPC

- 修复 `host_if_ipc.c` 未初始化 Winsock 导致的 `WSANOTINITIALISED (10093)`；awdld 现在能连上 `awdl_netd` 的 49877。
- 真实链路：radio 149 + 桥 + awdl_netd + awdld（非管理员）启动成功；awdld 日志 `host interface awdl0, index 1`，`tx data` 从 9 增长到 12，说明 mDNS/MLD 出向包已进入 AWDL。
- 当前 `peers 0`，iPhone 未处于活跃 AWDL 状态；端到端发现/传输仍待测。
- 更新 `patches/awdl-ipc-host.patch` 与 `docs/USERSPACE_DATAPATH.md`。

## 2026-10-27：用户态 mDNS 周期公告与 12 位实例

- 发现 lwIP mDNS 初始公告后不再频繁广播；改为每 2 秒 `mdns_resp_announce`，与 WinAWDL 接收端行为一致。
- 服务实例/主机名改为 12 位十六进制 ID（与 iPhone 自己的 `_airdrop` 实例格式一致），TXT 仍为 `flags=0x3fb`。
- 诊断抓到了 iPhone 的 mDNS 响应：`_airdrop._tcp.local`、12 位实例、`flags=2039`；说明 AWDL 双向数据可达。
- 但 iPhone 仍未建立 `/Discover`，当前 `peers 0`；下一步需要确认 iPhone 的 Bluetooth/AirDrop 活跃状态，或对比 NSEC/记录差异。

## 2026-10-27：复用 mdns.py 的 UDP 代理，Discover/Ask 通过

- 按“复用成功实现”的要求，mDNS 不再由 lwIP 生成：Python `receiver.py` 用原 `mdns.py` 生成记录，`awdl_netd` 只把 UDP 5353 在本地 15353 与 AWDL `ff02::fb` 之间搬运。
- `receiver.py` 新增 `ProxyAnnouncer`、`--mdns-address-file`、`--mdns-proxy-port`；仍复用原 TLS/HTTP/`/Discover`/`/Ask`/`/Upload` 逻辑。
- 实测：iPhone 能看到 AirBridge32；`POST /Discover`、`POST /Ask` 都到达 receiver 并返回 200。
- 剩余：`POST /Upload` body 卡住，临时文件 0 字节；下一步修 TCP 代理/窗口，而不是继续改 mDNS。
- 提交：`a3ce8d1` 之后的这一轮改动。

## 2026-10-27：v2ray TUN 开启时，用户态数据面完成真实 AirDrop 传输

- 把 TCP 层从 lwIP raw callbacks 改为 lwIP sockets API + 阻塞代理（`local_io.cpp` 隔离 Winsock，`lwip_proxy.cpp` 只碰 lwIP sockets）。
- mDNS 继续复用 `receiver.py` 的 `mdns.py` 记录，经 UDP 5353 ↔ 15353 代理。
- 实测环境：`singbox_tun` Up、`ping ::1` 100% loss；没有 Wintun、没有 Windows IPv6 参与。
- 结果：iPhone 15 Pro Max（iOS 17.5）通过 AirBridge32 传回 `IMG_3583-1.PNG`，2,911,531 字节，PNG 1290×2796，SHA-256 `C899358A...C48B1`，与之前成功文件一致；上传约 36 秒。
- 证据：`evidence/airdrop-userspace-tun-on.txt`（去敏）；原始接收日志未提交。
- 状态：**用户态 AWDL 数据面端到端可用**；下一步优化吞吐、清理 PoC 开关、接 Dart UI。

## 2026-10-27：吞吐对齐第一次成功（2.9 MB / 11.15 s）

- 对照第一次 Wintun/Windows 栈的 10.1 秒，调优 lwIP：`TCP_WND=240 KiB` + 窗口缩放、`TCP_MSS=1360`、`TCP_SND_BUF=32 KiB`、lwIP 和本地代理 socket 都开 `TCP_NODELAY`。
- 实测 TUN 仍为 Up：`IMG_3583-2.PNG` 2,911,531 字节，上传 **11.15 秒（~261 KB/s）**，SHA-256 与第一次成功文件一致。
- 证据：`evidence/airdrop-userspace-tuned.txt`（去敏）；原始接收日志未提交。
- 下一步：清理 `AWDL_NETD_*` PoC 调试开关，接 Dart/Flutter UI 与安装流程。

## 2026-10-27：40 MHz 将吞吐提升到 ~1.05 MB/s

- 20 MHz 下 2.9 MB/11.15 s（~261 KB/s）；80 MHz 设备不可见、发送失败。
- 40 MHz 实测：`IMG_3582.PNG` 7,710,365 字节，TUN 仍 Up，上传 **7.35 秒（~1.05 MB/s）**，约 4 倍提升。
- 结论：40 MHz 是当前 USB/devourer 路径的推荐宽度；80 MHz 暂不可用。
- 证据：`evidence/airdrop-userspace-width40.txt`（去敏）。

## 2026-10-27：80 MHz 中心信道修正后仍不可用

- `SelectedChannel.ChannelOffset` 对 80 MHz 一直传 0，中心信道被算成 149；改为按 5 GHz 80 MHz block 计算，ch149 传 offset=1（中心 155）。
- 日志确认 `channel 149 width 80 MHz offset 1`，但 iPhone 仍看不到设备、无法发送；80 MHz 在当前 RTL8821CU + devourer USB 路径不可用。
- 保持 40 MHz 为推荐；证据 `evidence/airdrop-userspace-width80-attempt.txt`。

## 2026-10-27：便携版 Flutter 壳与 ZIP

- 新增 `app/` Flutter Windows 应用：`AirBridge32.exe` 自动按顺序启动并监管 radio/bridge/awdl_netd/awdld/receiver，显示状态与日志，退出时逆序清理。
- 新增 `tools/Package-Portable.ps1`：收集后端、PyInstaller 打包 receiver、复制 VC 运行库/许可证，生成 `dist/AirBridge32-portable.zip`（约 19.6 MB）。
- 实测：从解压目录双击 `AirBridge32.exe` 能启动全部后端，iPhone 通过隔空投送传回 `IMG_3588.PNG`（2,578,074 字节）。
- 已知：便携版本次约 216 KB/s，手动同链路 40 MHz 约 1.05 MB/s；下一轮定位 receiver onefile/Flutter 监管层差异。
- 文档：`docs/PORTABLE.md`。

## 2026-10-27：便携版常驻（关闭=最小化）与 embeddable Python

- `AirBridge32.exe` 的窗口 X 改为最小化，app/radio/后端继续驻留；第二次接收不再重新初始化 radio。真正退出用界面“退出”按钮。
- 便携包 receiver 改为 **embeddable Python 3.12 + 原始 receiver.py**，不再 PyInstaller；ZIP 约 27 MB。
- 对照结论：同一个 Flutter 壳 + 系统 Python 是 2.58 MB / 3.2 s；PyInstaller/embedded 出现 12–55 s 波动，Dart 不是瓶颈。
- 下一步：原生 C++ receiver 直接并入 `awdl_netd`，去掉 Python 与本地 TCP 跳；并补单实例/托盘。

## 2026-10-27：M1 原生 mDNS 实验

- 新增 `src/awdl_netd/native_mdns.cpp`，按 `mdns.py` 构造 PTR/SRV/TXT/AAAA/NSEC，非阻塞每 2 秒发 `ff02::fb:5353`；
- `awdld` 的 `tx data` 稳定增长，确认包已发出，但 iPhone 仍看不到设备；
- 默认恢复 Python mDNS（`AWDL_NETD_MDNS_PROXY=1`），原生实现保留在 `AWDL_NETD_NATIVE_MDNS=1` 后面；
- 下一步抓原生 DNS 报文与 `mdns.py` 逐字节对比；详见 `docs/NATIVE_RECEIVER.md`。

## 2026-10-27：发现稳定性：稳定实例 ID + 更快公告

- `receiver.py` 不再每次 `secrets.token_hex(6)`；改为从 AWDL IPv6 IID 派生稳定 12 位 ID，重启不变。
- `ANNOUNCE_PERIOD_S` 2.0 → 0.5 秒；启动连发 3 次；查询双回复；`MIN_REPLY_GAP_S` 0.1 → 0.02。
- 实测用户反馈：比之前快，但还不是“一打开就在”；剩余延迟可能在 iPhone 的 AWDL/BLE 扫描。
- 文档：`docs/DISCOVERY.md`。

## 2026-10-27：上传期间暂停 mDNS，恢复速度

- 发现 0.5 秒公告会挤占 AWDL 时隙：6 MB 文件 45 秒只收到 1.76 MB（~40 KB/s）。
- `/Upload` 期间置位 `TRANSFER_BUSY`，mDNS 公告/查询回复暂停；上传结束恢复。
- 实测 1.16 MB / 5.4 秒（~215 KB/s），体感明显恢复；仍低于手动 40 MHz 的 1.05 MB/s，后续继续定位。
- 更新 `docs/DISCOVERY.md` 与 `patches/awdl-ipc-host.patch`。

## 2026-10-27：最终方案定稿（去掉 Dart 壳、单文件分发、速度四线）

用户要求以“手机端更稳、传输更快、系统更稳、分发更方便”为核心，给一个最终方案。只写决定，未改代码。

- 新增 `docs/FINAL_PLAN.md`：V1 保留 GPL 边界（radio GPL-2.0 + awdld GPL-3.0 + MIT 其余进程），删除 `app/`（Flutter/Dart）与“embeddable Python 发行版 + Flutter 监管”的启动方式；用户侧改为**单文件自解压 exe + 原生托盘监管**，Python 接收端保留并由监管进程隐藏拉起。
- 明确不做的项：单进程合并（GPL-2.0-only 与 GPL-3.0 不兼容）、Dart 重写 AWDL、Wintun、80 MHz、receiver 改 C++（均为 M3 后再议）。
- 速度按 E1 时隙/信道序列 → E2 ACK/TX 优先级 → E3 USB 帧率 → E4 mDNS 开销 四条实测线推进；依据是 23:01 日志中 iPhone 成为 sync master 后 149 仅占 16 个时隙中的 4–7 个、joined 帧约 150 帧/s，而 1.05 MB/s 约需 700 帧/s。
- 稳定性设计：看门狗指标、恢复阶梯（重开 RX → 重新 Init → USB reset → 重启 radio → 提示拔插）、单实例、USB 选择性挂起、崩溃隔离与退避重启、日志轮转与脱敏诊断包。
- 分发：单文件解压到 `%LOCALAPPDATA%`、无需管理员、驱动绑定仍走 Zadig 且回滚演练列为硬门槛；Realtek 固件再分发三选一仍需用户拍板。
- 发现一个真实分发阻塞：`identity.py` 依赖外部 `openssl.exe`，干净机器（无 Git/OpenSSL）首次启动会失败；已列为 WP1.5，改用包内 `cryptography` 进程内生成证书。
- 同步更新 `README.md` 阅读入口与 `docs/PLAN.md` 顶部口径。

未做：没有删除 `app/`、没有改任何后端代码、没有跑硬件实验（本轮只落方案）。下一步按 WP0 用同一张 20 MB 文件重建基线，再决定 WP1 的自解压外壳实现方式。

## 2026-10-27：固化工作队列，开始按 Q1 执行

用户要求把待办固化，避免上下文丢失或换 agent 后忘记；之后开始执行。

- 新增 `docs/WORK_QUEUE.md`：Q1–Q13 的可交接队列（状态/依赖/完成判据/证据），并把"本轮未授权硬件实验"单列。
- `AGENTS.md` 的必读清单加入 `docs/FINAL_PLAN.md`、`docs/WORK_QUEUE.md`，并规定交接以队列为准。
- README 阅读入口加队列链接。
- 下一步：Q1 离线吞吐台（不改射频、不发帧）；Q2 身份证书去 openssl；Q3 手动链路脚本（跑之前要授权）。

## 2026-10-27：Q1 完成——receiver 不是速度瓶颈（228 MB/s）

新增 `tools/bench_receiver.py`：在 `127.0.0.1` 起真实 receiver（TLS + chunked + DVZip + cpio + 落盘），服务端计时 `/Upload` 开始到 `stored`。

- 20 MB 随机字节、3 次：compressed 中位数 **227.7 MB/s**（152/228/236），stored 中位数 **277.9 MB/s**。SHA-256 全部一致。
- `--trace` 全开复测：274.2 / 259.8 MB/s，日志 2.6 KB —— 调试日志不是开销。
- 结论：Python/TLS/zlib/cpio/磁盘有约 200 倍余量；1.05 MB/s 只用到 0.45%。速度问题锁定在空口调度、ACK 与 USB 帧率，不再考虑“换语言/去 Python”。
- 证据：`evidence/bench-receiver.md`、`evidence/bench-receiver-trace.md`。
- 局限：客户端与服务端同进程，数字是下限；但不影响“远高于需求”的结论。
- 顺带发现：官方 `test_receiver.py` 在本机 9 个里 8 个失败（连 `::1`，本机 IPv6 回环被 TUN 挡，`WinError 10013`）；bench 用 `127.0.0.1` 绕开，测试环境需要另行修（未列入队列）。
- 队列：Q1 → ✅；下一项 Q2。

## 2026-10-27：Q2 完成——证书不再依赖外部 openssl；补丁恢复可复现

`identity.py` 改为优先用 `cryptography` 进程内生成 RSA-2048 自签证书（临时文件 + `os.replace` 原子落盘），只在模块缺失时回退原来的 `openssl.exe` 路径。

- A：PATH 去掉 Git/OpenSSL → 进程内生成，证书 1046 B、密钥 1704 B，`ssl.load_cert_chain` 通过。
- B：屏蔽 `cryptography` → 回退 Git 自带 openssl，证书 1084 B，通过。
- C：两者都没有 → 明确抛 `IdentityError`，不静默降级。
- D：端到端 1 MB 上传（PATH 无 Git/OpenSSL）→ 117.2 MB/s、SHA-256 一致。
- 证据：`evidence/identity-no-openssl.log`；改动以 `patches/airdrop-identity.patch` 跟踪（upstream 未入库）。

附带修复两个真问题：

- 旧 `patches/awdl-ipc-host.patch` 是 **CRLF**，对 LF 工作树 `apply --check` 必然失败；而 `Apply-AwdlPatches.ps1` 在补丁既不能正应用也不能反应用时仍然 `exit 0`，等于把损坏藏起来。已用 LF 重新生成两个补丁（identity 单独一个，互不重叠）。
- 脚本改为逐补丁判定"应用 / 已应用 / 冲突抛错"，新增 `-Repo`；在锁定提交的干净 worktree 上应用后，identity.py、mdns.py、receiver.py、awdl/CMakeLists.txt、host_if_ipc.c 五个文件哈希与活树**完全一致**，重复执行幂等。证据：`evidence/patches-reproduce.log`。

队列：Q2 → ✅；下一项 Q3（手动链路脚本）。

## 2026-10-27：Q3 脚本与无 Dart 包（16.0 MB）

- 新增 `tools/Start-Chain.ps1`：默认 **dry-run**，只有显式 `-Run` 才会初始化网卡并让 awdld 发帧——授权边界写进工具本身。布局自适应（仓库 `build\` / 打包 `backend\`），启动前查 49876/49877/8771/15353，逐个等 ready 行，Ctrl+C 逆序清理并复查端口，`chain.log` 记录 commit/时间/频道。
- 新增 `tools/Package-Headless.ps1`：产出无 Flutter、无 Dart 的 `dist\AirBridge32-headless.zip`，内含 backend、python-embed、receiver、`Start.cmd`、README 与许可证。
- 实测（不含射频）：dry-run 两种布局路径正确；`python -m http.server 49876` 占端口时按预期拒绝启动；打包后 `Start-Chain.ps1` 解析到 `backend\python\python.exe` 与 `backend\airdrop\receiver.py`。
- 体积：headless ZIP **16.0 MB**（解压 36.4 MB），旧便携版 27.0 MB；去掉 Flutter/Dart 省 11 MB。
- 未做：`-Run` 真机起停三次、与便携包的同文件 A/B、单文件 exe 外壳（Q4）。这三项都要硬件授权或额外外壳工作。
- 证据：`evidence/headless-package.log`。
- 队列：Q3 → 🔜（脚本与包完成，实测待授权）；Q4 载荷已就绪，外壳未做。

## 2026-10-27：Q4 原型——16.0 MB 单文件自解压 exe

机器上没有 7-Zip/WinRAR，没有装任何打包器；改用 Windows 自带 `csc.exe`（.NET Framework 4.8）编一个 7 KB 的 C# stub，`System.IO.Compression` 读内嵌 ZIP。

- `src/sfx/Program.cs`：解压到 `%LOCALAPPDATA%\AirBridge32\<exe 时间戳>\`，`extracted.txt` 作标记；默认解压后启动 `Start.cmd`；`--extract` / `--where` 便于安全测试；对 zip 条目做目录穿越校验、兼容 `Compress-Archive` 写出的反斜杠与空目录条目。
- `tools/Build-Sfx.ps1`：`dist\AirBridge32-headless` → `dist\payload.zip` → `dist\AirBridge32.exe`。
- 实测：exe **16.0 MB**（stub 仅比载荷大 7 KB）；首次解压 **0.4 s / 333 文件**，第二次 **0.03 s**；包内 Python 导入 ssl/zlib/cryptography 正常；解压目录 dry-run 路径正确。
- 结论：不需要 Flutter/Dart/Python 安装或第三方打包器即可"一个 exe"；代价仅是 .NET Framework 4.8（Win10 1903+/Win11 自带）。
- 未做：干净 VM、SmartScreen/AV、真机启动（Q13 / 硬件授权）。
- 决定：托盘监管（Q5）同样用 C# + WinForms（系统自带），不再写 Win32 C++：代码量约为 C++ 的三分之一，且能满足托盘、通知、单实例、开机自启。
- 证据：`evidence/single-exe-prototype.log`；队列 Q4 → ✅。

## 2026-10-27：Q5 完成——C# 托盘监管，真机起停干净

用户授权硬件实验（23:37）。按计划把 Flutter 的职责搬进一个 20 KB 的 winexe。

- `src/supervisor/Program.cs`：单实例（`Local\AirBridge32-tray` 互斥体）、按序拉起 radio → bridge → netd → awdld → receiver、用每个进程的 stdout/stderr ready 行判定就绪、日志按会话落 `data\logs\<stamp>\<进程>.log` 并只保留最近 10 个会话、托盘菜单（状态/接收目录/日志/开机自启/重启/退出）、接收目录 FileSystemWatcher 弹气泡、退出逆序 kill 并等待。
- 三种模式：`--check`（只校验，不启动）、`--smoke N`（真机起停，写 smoke.txt）、`--auto-exit N`（托盘测试钩子）；`--autostart-on/off` 与菜单共用注册表项。
- 实测（真实 RTL8821CU，未接 iPhone）：radio 初始化 **42 s** 后 listening，链路在 0.5 s 内全部就绪，awdld `tx action` 增长说明空口在发同步帧；`--smoke 20` 与托盘 `--auto-exit 75` 都以退出码 0 结束，进程表无残留；单文件包解压后 `backend\AirBridge32.exe` 无参数 `--check` 全部路径正确。
- 打包链更新：`Package-Headless` 把监管放进 `backend\`，`src/sfx` 解压后优先启动它（其次 `Start.cmd`）；重建后仍是 16.0 MB。
- 未验证：气泡/菜单外观、开机自启实际写入、真实文件传输与“收到文件”通知（需要 iPhone）、24 h 常驻。
- 语言决定：C# + WinForms 取代 Win32 C++（.NET Framework 4.8 是 Windows 自带，产物 20 KB）；已回写 FINAL_PLAN。
- 证据：`evidence/supervisor-tray.log`；队列 Q5 → ✅，新增 Q14（Q5 真机传文件通过后再删 `app/`）。

## 2026-10-27：Q6 完成、Q7 部分——看门狗与分级恢复

- 日志：单个 `.log` 超 5 MiB 滚动成 `.log.1`，会话目录保留 10 个；托盘新增“复制诊断信息”（root/状态/OS/各日志末 40 行，写剪贴板）。
- 看门狗 5 s 检查存活；**只有 receiver 死**时单独重启，其它后端死才整链重启（退避 1/5/30 s，10 分钟 5 次后停）。
- 实测（真机）：`Stop-Process` 杀 receiver（pid 48096）→ 5 s 内发现 → 0.2 s 内起新 receiver（32968）并 ready，`airbridge_radio` pid 前后不变（42468）。
- 对照：早先版本整链重启要 42 s（radio 固件重初始化），所以选择了选择性重启。
- 同日遇到一次已知固件启动失败：`FW ready check failed (0x80 != 0xC078)`，cpu-reset 重试后恢复，启动被拖到 >60 s；说明固件重试需要可见状态和更长超时（Q7 剩余）。
- 重新打包：headless ZIP 与单文件 exe 均 16.0 MB，监管 24 KB。
- 证据：`evidence/supervisor-recovery.log`；队列 Q6 → ✅，Q7 → 🔜（假死检测、24 h 未做）。

## 2026-10-27：端口占用快速失败 + 会话分析器

- 监管启动前先试绑 49876/49877/8771/15353；占用即失败，不碰射频。实测：`python -m http.server 8771` 占住时 `--smoke 5` 3 秒内返回 `started=False`、`error=port 8771 is already in use`；解除后 `--smoke 8` 正常，说明不误报 TIME_WAIT。
- 新增 `tools/analyze_session.py`：从监管会话目录的 `receiver.log`（`POST /Upload` 头 → `stored`）算真实 MB/s，从 `awdld.log` 算 joined 帧/s 与所需帧率；`--json` 可机读，`--latest <包根>` 取最近会话。
- 监管日志新增 `start chain root=... args=...` 一行，便于定位会话。
- 重新打包：headless ZIP 与单文件 exe 均 16.0 MB，监管 25 KB。
- 下一步：等一次真实 iPhone 传输，用分析器给出“现链路 vs 便携包”的可比数字（Q3/Q8）。

## 2026-10-28 凌晨：首次真实使用——40 MHz 退化、20 MHz 正常、冷启动才恢复

用户在托盘版上实测同一张 2.58 MB 照片五次：

| 配置 | 用时 | 速率 |
| --- | --- | --- |
| 149 / 40（首链） | 69.2 s | 37 KB/s |
| 149 / 40（同一进程再发） | 70.7 s | 36 KB/s |
| 149 / 40（整链重启） | 91.8 s | 28 KB/s |
| 149 / 20 | 12.8 s | **202 KB/s** |
| 149 / 40（USB 拔插冷启动后） | 13.6 s | **190 KB/s** |

- 慢速时 `joined` 帧 0–47/s，中间有 6–18 s 零帧空窗，但 `rx action` 仍在收（手机停发 → TCP 退避），`tx data` 同样归零。
- 快速时稳定 110–220 帧/s、每帧约 1,455 B。40 MHz PHY 单帧 <0.2 ms，实测每帧 6.7 ms → 上限是**时隙占用率**，不是带宽、不是 Python（离线 228 MB/s）。
- 环境底噪（doctor 被动 RX）：149 安静（101/154）、153 与 157 无 AP、36/40 忙但干净 → 邻居干扰解释不了 40 MHz 的 37 KB/s。
- 结论一：**40 MHz 会进入退化状态，只有 VBUS 冷启动能恢复**（同 20 MHz 下 202 KB/s 正常）。这需要产品化的链路健康检测（Q7），不能靠用户拔插。
- 结论二：冷启动后 40 MHz ≈ 20 MHz ≈ 190–200 KB/s；awdld 整晚只 `switch channel to 149` 一次，不跟随对端 36/6 时隙，常态只用约 2/16 个时隙。**最大速度杠杆是让 awdld 跟随/协商信道序列**（新增 Q15）。
- 监管新增 `--channel` / `--width` 参数，用于这种 A/B（本次 20 MHz 对照就是用它跑的）。
- 证据：`evidence/transfer-regression-2026-10-28.log`；队列 Q7 补注、新增 Q15。

## 2026-10-28：默认改 20 MHz；Q15 第一步——radio 频内快切

- 按用户要求把常态带宽改成 **149 / 20 MHz**：监管默认 `width=20`，托盘状态栏改为跟随实际配置（`BandText`），headless 包与单文件 exe 重新打包（16.0 MB），并用单文件 exe 起了一条 149/20 实例验证（00:45:35 就绪）。提交 `4a4f06a`。
- Q15 第一步：`src/radio_usb/main.cpp` 的 `SET_CHANNEL` 同频段改用 devourer `FastRetune`，跨频段仍走完整 `SetMonitorChannel`，并把耗时写进日志。
- 实测（radio 单独跑 + Python 客户端）：`149→36` 1.86 ms、`36→149` 14.75 ms、`149→44` 12.2 ms（原完整路径约 90 ms）；换台后 RX 正常（36 上 43 帧/2 s、RSSI -40）。
- 找到 Q15 主体的确切位置：OWL 的 `awdl_handle_chanseq_tlv` 把对端序列存进 `awdl_peer.sequence` 后就不管了，`state->channel.sequence` 始终是静态“全 149”，所以 `daemon/core.c` 的逐时隙换台永远只看到 149。下一步是把主控序列（含编码）写回 state，再让 core 的时隙循环驱动 SET_CHANNEL；跨频段那个 2.4 GHz 时隙先跳过。
- 重新打包并重启为 149/20（会话 20261028-005000，radio 00:50:43 就绪）。证据 `evidence/radio-fast-retune.log`；队列 Q15 → 🔜。

## 2026-10-28：Q15 主体代码完成（opt-in，待手机在线验证）

- `owl/src/rx.c`：收到信道序列 TLV 且发送者是当前同步主控时，把序列（含编码）写回 `state->channel.sequence` / `state->channel.enc`，打印 `adopted channel sequence of sync master <mac>`；由 `AWDL_FOLLOW_MASTER_SEQ` 环境变量开启，默认关闭。这样 `daemon/core.c` 的逐时隙换台才会真正按主控时隙表走。
- `src/radio_usb/main.cpp`：跨频段换台新增 `AIRBRIDGE_HOP_SKIP_CROSSBAND=1` 跳过（2.4 GHz 那个时隙用完整路径要 ~90 ms，会把超帧拖死）；同频段仍是 `FastRetune`。
- 补丁体系：新增 `patches/owl-follow-chanseq.patch`（OWL 子仓库），`tools/Apply-AwdlPatches.ps1` 改为管理三个补丁、支持子仓库、可重复执行；已验证补丁能干净应用到 OWL 锁定提交 `da255a7` 的干净 worktree。
- 测试状态：带两个开关跑过一次，但当时 `peers 0`（iPhone 没开隔空投送），收不到序列 TLV，**未验证跟随效果**。下次需要用户在 iPhone 上打开隔空投送，再看 `adopted channel sequence` 与 radio 的 `channel x -> y (…)` 是否出现，并测吞吐。
- 重新打包并恢复默认（开关不设）的 149/20 链路。证据 `evidence/q15-follow-sequence.log`；队列 Q15 → 🔜（代码完成，待验证）。

## 2026-10-28：Q15 有结论——当同步主控，336 KB/s（+66%）

- 用户开了隔空投送后先验证跟随模式：`adopted channel sequence of sync master 8e:b4…` 正常出现，awdld 确实按 36/149/6 逐槽切（109 次），但**吞吐 101 KB/s，只有默认的一半**。原因：手机只在它的 149 时隙里给我们发数据，跟着跳到 36 只是白等 + 换台开销。跟随方向否定。
- 改试反向：OWL 的选举规则是 metric 更高者当主控（`compare(peer, self) < 0` 拒绝对端）；iPhone 的 metric 是 65，我们设 `AWDL_ELECTION_METRIC=200` 后稳定成为 `sync master`（日志确认 `sync master is now <我们的地址> (met 200)`），序列变成全 149。
- 实测（同一张 2,578,074 B 照片）：**7.68 s = 336 KB/s**，传输中 joined 帧 196–266/s（默认模式 ~150/s）。**比默认快约 66%。**
- 40 MHz + 主控实测更慢：7,368,180 B / 42.7 s = 172 KB/s，与用户“40 MHz 会死”的体感一致，继续用 20 MHz。
- 落地：监管给 awdld 固定 `AWDL_ELECTION_METRIC=200`（默认即主控模式，无需环境变量）；OWL 两处改动（rx.c 跟随开关、election.c metric 覆盖）收进 `patches/owl-airbridge.patch`（重命名），`Apply-AwdlPatches.ps1` 同步更新。打包版启动确认 `sync master is now 92:06:… (met 200)`。
- 风险：强制主控会影响同一空域其他 Apple 设备的 AWDL 选举，属于产品级取舍；336 KB/s 是一次测量，需要重复三次确认。
- 证据：`evidence/q15-master-mode.log`；队列 Q15 → ✅。

## 2026-10-28：同一配置跑到 970 KB/s（≈1 MB/s）

- 打开 radio 的逐 5 秒链路计数（新增 `AIRBRIDGE_RADIO_LOG=info` 透传 + `radio: stats cb/crc/...`）后再测 29.46 MB 视频：**30.37 s = 970 KB/s**，传输中 635 帧/s（546–817）。
- 链路计数：传输期间回调 28,620 次、CRC 增加 4,291（约 15%）、小于 24 字节的帧 2,014；15% 的 CRC 仍能维持慢启动后的窗口增长。
- 同配置 20 分钟前那次是 363 KB/s（81.2 s），**两次差 2.7 倍**，期间没有任何软件改动（只把 radio 日志级别从 warn 改成 info）。01:34 那次 radio 经历过多次固件启动失败与 full power-cycle 重试才起来，之后跑出 970——与之前 40 MHz“退化态只有冷启动能恢复”的现象同源。
- 结论：**1 MB/s 可达**（970 KB/s 为实测）；但同一配置也会掉到 363 KB/s，必须做链路健康检测（Q7：帧率 < 300 帧/s 持续 10 s 判为低速，进入提示/恢复流程）。
- 顺手修了监管：radio 就绪超时从 180 s 提到 300 s（固件重试会超过 180 s），启动失败后按 1/5/30 s 退避自动重试，而不是等用户点“重新启动”。
- 证据：`evidence/transfer-970kbs.log`。

## 2026-10-28：Q7 增量——低速链路检测与“重置链路”

- radio 每 5 秒的 `stats` / `peer frames` 两行改为可走 warn 级别（`AIRBRIDGE_LINK_STATS=1`），监管始终打开。这样既保留每 5 秒的链路计数，又不会像 info 级别那样每帧打一条 `bulk_send`。
- 监管新增 `OnChildLine` 解析 `peer frames=N` → `Fps = N/5`；托盘看门狗每 5 秒采样，连续两次 < 300 帧/s 判为低速：记日志 + 弹气泡“链路速率偏低，可从托盘选重置链路”。
- 托盘新增“重置链路”（重启整链，等效冷复位射频）；`_slowSamples/_slowWarned` 在恢复或重置后清零。
- 阈值依据：昨天四次实测（970/363/251/952 KB/s）对应 750–860 与 190–280 帧/s 两档，丢包率都在 15% 左右。
- 已起链路验证：149/20、主控、warn 级别下只有两条 5 秒统计，没有每帧噪声；`sync master is now` 是我们自己。
- 下一步：抓一次慢速传输，用新采样确认是“手机降 MCS”还是“手机少发帧”，据此决定 Q7 的恢复动作是重置射频还是别的。

## 2026-10-28：选举 counter 修正（1121 KB/s）与全部数据留在程序文件夹内

- **慢速根因**：托盘报“链路速度偏低”那次，日志显示 12:35:48 手机把同步主控抢走（`new election tree: 66:b9… -> 8e:b4… (met 65, ctr 1514)`）。OWL 选举先比 counter 再比 metric，只在对方更小时拒绝；OWL 自己从不递增 counter（恒 0），而 iPhone 的 counter 每 3.14 秒 +1，于是开机几分钟后必然反超、序列回到 36/6/149、速度掉回 200 KB/s 档。给 election.c 加 `AWDL_ELECTION_COUNTER` 覆盖，监管固定传 100000；重启两分钟后仍是主控（`met 200, ctr 100000`）。
- **新纪录**：12:52 传 29.46 MB 视频 **26.26 s = 1121 KB/s**，是目前最高；counter 修正后没有再被抢主控。
- **AirDrop 列表重复**：实例 ID 从 AWDL IPv6 地址派生，而 awdld 每次启动随机 AWDL MAC，ID 每启动变一次（`b011eefffed7` → `18c2c6fffe5a`）。`stable_service_id` 改为先读 `<keys>/airdrop-service-id`；重启验证 ID 不变。
- **全部数据留在程序文件夹内**（用户新要求）：stub 解压到 `exe 目录\app\<构建>`（只留最近 2 个），`data\` 放 `keys\`（证书、实例 ID）、`received\`、`logs\`、`awdl_netd.addr`，通过 `--data` 传给监管、跨构建共享；开机自启改指向文件夹里的 stub。验证后 `%LOCALAPPDATA%\AirBridge32` 下不再生成新目录，并清掉了开发期留下的 13 个旧目录。
- 提交：`1cedcdf`（counter）、`cc3b026`（实例 ID + 1121 KB/s）、本条（便携布局）。
- 证据：`evidence/transfer-970kbs.log`、`evidence/service-id-persist.log`、`evidence/data-stays-in-folder.log`。

## 2026-10-28：删除 Flutter 壳；启动 24 h 常驻采样

- 按 Q14 删除 `app/`（Flutter 项目，26 个受版本控制文件、磁盘上 328 MB 含构建产物）与 `tools/Package-Portable.ps1`；现行打包只剩 `Package-Headless.ps1` + `Build-Sfx.ps1`。
- 文档同步：README 顶部换成当前状态（单文件 exe、托盘、1121 KB/s、20 MHz + 主控、数据留在文件夹内），阅读入口把 PORTABLE/ARCHITECTURE 标为历史；PLAN 在 Dart 决策下加“已被 2026-10-28 决定取代”；ARCHITECTURE、PORTABLE 加历史提示。
- 队列：Q14 完成；Q8–Q11 归档（Q15 的实测已覆盖时隙、帧率、ACK、mDNS 四项）；Q12 标为非必需。
- 新增 `tools/Monitor-Soak.ps1` 并已在后台启动（24 h、每 60 s 一次，CSV 落 `build\soak\soak.csv`）：记录 supervisor/radio/bridge/netd/awdld/receiver 的工作集、私有内存、句柄、CPU，以及缺失进程与监听端口。首帧：AirBridge32 33 MB 工作集 / 75 MB 私有 / 423 句柄；radio 7 MB；awdld 4 MB；receiver 25 MB；端口 49876/49877/8771 在听。
- 样本里 15353 不在列表是因为它只开 UDP，端口检查只看 TCP——不是缺失。
- 下一步：常驻采样跑满一段后看趋势；速度基线（A）等你方便时发 3 次 29 MB 视频。

## 2026-10-28：整理交接文档 docs/STATUS.md

- 新增 `docs/STATUS.md`：给接手人看的一份现状说明——交付形态与目录布局、六个进程与许可证、数据路径、关键配置（149/20、主控 metric/counter、社交时隙、固件快切、实例 ID 持久化、低速检测）、全部速度实测、今天修掉的八个问题（带提交号）、五个未解决项（射频退化、蓝牙 Error、稳定性未验收、固件授权、签名/干净 VM）、1 小时常驻采样、构建与验证命令、下一步建议、证据索引。
- README 阅读入口加入该文档；`tools/check_project.py` 的必需清单同步。
- 当前实测：最好 1121 KB/s（29.46 MB / 26.26 s）；社交时隙 + 快切后起步 930 帧/s，随后因射频退化中断；1 小时常驻采样无泄漏（AirBridge32 33–37 MB 工作集 / ~421 句柄）。
- 蓝牙接口仍是 Error（软件 USB 复位造成），需重启系统恢复，不影响 AirDrop。
