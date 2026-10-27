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
