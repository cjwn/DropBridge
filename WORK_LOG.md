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
