# DropBridge / AirBridge32

Windows 11 原生 AirDrop 接收研究工程。目录名保持 DropBridge，AirBridge32 为任务书暂定产品名；没有 ESP32 固件要求。

**当前：Phase 3 首次真实 AirDrop 传输成功。iPhone 15 Pro Max（iOS 17.5）在隔空投送中看到 AirBridge32，并通过 AWDL/Wintun 把一张 2.9 MB PNG（1290×2796）传到 Windows；文件大小与 PNG 签名已核对。无线侧使用专用 RTL8821CU：频道 149、AWDL 控制面同步、TCP radio、命名管道桥和 AirDrop 接收服务全部跑通。测试时必须关闭 v2rayN 的 TUN（其 `EnableIPv6=false` + `StrictRoute=true` 会让 IPv6 不可用）；v2ray 兼容配置、5 GHz 空口 TX/ACK、GUI 和打包仍未完成。**

目标是让 iPhone 系统原生隔空投送通过专用 RTL8821CU USB 网卡把文件交给 Windows，不依赖 WSL2、虚拟机或 iPhone 第三方应用。

## 阅读入口

- [工作队列：下一步做什么（跨会话）](docs/WORK_QUEUE.md)
- [最终方案：AirBridge32 V1（决定）](docs/FINAL_PLAN.md)
- [原始任务书](docs/用户任务书.md)
- [源码审计及 Go / No-Go](docs/PHASE0_AUDIT.md)
- [阶段计划与验收](docs/PLAN.md)
- [Windows 构建记录](docs/BUILD_WINDOWS.md)
- [USB radio 后端状态](docs/RADIO_USB.md)
- [Phase 3：AWDL/AirDrop 实测](docs/PHASE3_AWDL.md)
- [便携版 ZIP 与启动壳](docs/PORTABLE.md)
- [目标架构：Dart 应用 + C++ radio](docs/ARCHITECTURE.md)
- [无线层契约对照](docs/RADIO_CONTRACT.md)
- [硬件接入与驱动绑定检查单](docs/HARDWARE_INTAKE.md)
- [WinUSB 绑定运行手册（Zadig）](docs/BINDING_RUNBOOK.md)
- [许可证与再分发核查](docs/LICENSE_MATRIX.md)
- [版本锁定](docs/sources.lock.json)
- [工作记录及交接](WORK_LOG.md)
- [开发约定](AGENTS.md)

## 现在可运行的工具

在项目根目录的 PowerShell 执行：

```powershell
.\tools\Inspect-Hardware.ps1      # 只读 USB 枚举，识别 0bda:c811 / c820 / 1a2b
.\tools\Check-Binding.ps1         # 只读检查 MI_02 当前驱动（WinUSB / 厂商驱动）
.\tools\Setup-BuildDeps.ps1 -WithOpenSsl   # 一次性：vcpkg + libusb + pkgconf + OpenSSL
.\tools\Build-Devourer.ps1        # 配置、编译 C8821C-only 并跑无硬件自检
.\tools\Build-UsbProbe.ps1        # 编译只读 USB 探针（open/claim，不初始化芯片）
.\tools\Start-Chain.ps1           # 无 Flutter 起停整条链路；默认 dry-run，加 -Run 才发帧
.\tools\Package-Headless.ps1      # 打无 Dart 的包：dist\AirBridge32-headless.zip（16 MB）
python .\tools\bench_receiver.py --mb 20 --runs 3   # receiver 离线吞吐（不涉及射频）
python .\tools\check_project.py   # 文档、证据与版本锁一致性
```

- 硬件检查只枚举当前 USB 设备，生成 `evidence/usb-devices.local.json`；完整设备实例路径与序列号不提交。它不会初始化无线芯片、替换驱动或发射无线帧。
- `Check-Binding.ps1` 只读；驱动绑定本身需要管理员权限和你的确认，步骤见 [BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md)。
- 构建依赖全部在 `build/deps`（git 忽略），不执行 `vcpkg integrate install`、不改 PATH、不改驱动。
- 本轮构建结果：`devourer.lib`、`rxdemo`、`txdemo` 均生成，`ctest` 74/74 通过（3 项跳过，含 3 项 OpenSSL 加密自检），日志在 `evidence/build-8821c-*.log`。

上游源码位于 `upstream/`，不作为主仓库代码发布；恢复时按 `sources.lock.json` 中 URL 克隆并 checkout 对应完整 commit。WinAWDL 的无线依赖还需 `git -C upstream/winawdl submodule update --init upstream/devourer`，版本固定在 `8d936de2a3805ce126e5236349afe40e645aaf9b`。

## 已建立的证据

- 五个上游源码已下载并锁定 SHA；WinAWDL 专用 devourer 子模块也已取回。
- `evidence/hardware-identity.json`：目标网卡身份 —— `0bda:c820`，复合设备，MI_00 蓝牙、MI_02 Wi-Fi（vendor class `ff/ff/ff`）；MI_02 已由 `netrtwlanu.inf` 换为 WinUSB（`oem155.inf`，libwdi）；芯片 ID 明确标记为未验证。
- `evidence/binding-result.log`：绑定后 `Check-Binding.ps1` 与 `usb_probe.exe` 的验证记录（open OK、claim interface 2、端点列表）。
- `evidence/chip-id-read.log`：只读读取 `SYS_CFG2 0x00FC = 0x09`，确认 RTL8821C；没有 Init、没有固件下载、没有发帧。
- `evidence/init-first.log`：第一次完整 bring-up（频道 6）：固件下载、BB/RF 表、IQK、RX 使能、`cut=4 (UMC) 1T1R`；零发帧。
- `evidence/rx-first.log`、`evidence/rx-second-5g.log`：两次限时被动 RX（频道 6 / 36）的帧数、FCS 正常/CRC 错与 `HEALTHY` 判定；零发帧。
- `evidence/tx-witness-summary.json`：ESP32-S3 对 2.4 GHz 空口 TX 的见证汇总；3/3 完整 reset 轮次收到 20/20 唯一序号。
- `evidence/esp32-witness-tx{2,3,4,5,6}.log`、`evidence/tx-{second,third,fourth,fifth,sixth}.log`：各轮原始串口与驱动日志（含一次 skip-reset 失败对照）。
- `evidence/build-8821c-*.log`：本机 MSVC 19.44 + libusb 1.0.30 + OpenSSL 3.6.5 的配置、编译和 74 项自检记录。
- `evidence/usb-open-baseline.log`：绑定 WinUSB 之前，libusb 找不到设备接口（`LIBUSB_ERROR_NOT_FOUND`）的基线记录。
- `evidence/build-configure.log`：首次配置因缺 PkgConfig 失败的原始记录，保留作历史。
- `evidence/upstream-issues.json`：近期 Issue/PR 摘要快照，每库最多八条，不代表全面审计全部历史。
- `docs/RADIO_CONTRACT.md`：管道 v3 契约与 8821C 的 BlockAck 接收缺口。
- `docs/LICENSE_MATRIX.md`：许可证矩阵；Realtek 固件再分发仍未解决。

- `evidence/airdrop-first-transfer.txt`：Phase 3 首次真实 AirDrop 传输（iPhone → Windows，2,911,531 字节 PNG，1290×2796，SHA-256 已记录；原始接收日志含个人信息，未提交）。
- `docs/PHASE3_AWDL.md`：`awdld`/Wintun/桥/接收服务的运行顺序、首次实测结果与 v2ray TUN 阻塞说明。

## 仍未完成

1. **v2ray TUN 兼容**：当前 TUN 关闭时可用；需要在 TUN 开启时把 IPv6/link-local 排除，或改用系统代理模式。
2. **5 GHz 空口 TX/ACK**：ESP32-S3 只能见证 2.4 GHz；5 GHz 的 TX 和硬件 ACK/BlockAck 仍需第三方观察设备或 iPhone 侧确认。
3. **Dart/Flutter 应用与 UI**：目前是命令行 radio + awdld + Python 接收服务，还不是用户可见的单一程序。
4. **打包/安装**：Wintun、管理员权限、防火墙、驱动回滚和卸载流程尚未产品化。
5. **WinUSB 回滚演练**：仍保留为发布前必须完成的恢复路径。

本轮不复制上游实现，不赋予第三方代码统一许可证。产品发布许可证将在复用范围确定后处理。
