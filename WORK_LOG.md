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
