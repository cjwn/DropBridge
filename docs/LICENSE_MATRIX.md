# 许可证与再分发核查（Phase 0 任务 4）

状态：**静态核查完成，未做法律结论**。核查对象为 `docs/sources.lock.json` 锁定的五个提交，以及 WinAWDL 锁定的 devourer 子模块 `8d936de2a3805ce126e5236349afe40e645aaf9b`。本轮没有把任何上游实现复制进主仓库，也没有修改上游文件。

## 1. 组件矩阵

| 组件 | 版本 | 许可证 | 依据 | 是否在目标运行路径 |
| --- | --- | --- | --- | --- |
| WinAWDL `app/`（.NET 10 WPF） | `2196fb4` | MIT | 根 `LICENSE`；README「Licensing」声明除 `radio`、`awdl` 外全部 MIT | Phase 4 候选 |
| WinAWDL `scripts/`、`installer/`、`tools/`、`probes/` | `2196fb4` | MIT | 同上；`installer` 文件头有 SPDX | 构建/安装参考 |
| WinAWDL `awdl/` | `2196fb4` | GPL-3.0-or-later | `awdl/CMakeLists.txt:1` SPDX；`awdl/LICENSE`；README 说明基于 OWL | Phase 3 |
| WinAWDL `radio/` | `2196fb4` | GPL-2.0-only | `radio/CMakeLists.txt:1` SPDX；`radio/LICENSE`；README 说明链接 devourer | Phase 2 |
| `radio/radio_link.h` | `2196fb4` | MIT | 文件头 SPDX；README 明确为进程间协议，供两侧共同包含 | IPC 接缝 |
| WinAWDL `airdrop/` | `2196fb4` | MIT | `airdrop/LICENSE`（adapt 自 omdrop，保留其 MIT 声明） | Phase 4 |
| WinAWDL `driver/regproxy/` | `2196fb4` | MIT | `regproxy.c`、`regproxy_ioctl.h`、`.inf` 均有 SPDX | 不使用（PCIe 路线） |
| devourer（OpenIPC） | `f7e0731` | GPL-2.0 | `LICENSE`（GPL-2.0 全文）；CLAUDE.md 未声明例外 | Phase 1/2，链接进 radio |
| devourer 固件数组 `hal/*_fw.c` | 同上 | 无独立授权声明 | `hal8821c_fw.c` 头部注明从 `morrownr/8821cu-20210916` 提取（Realtek FW v5.12.0.4） | 会进入二进制 |
| OWL | `0ee3263` | GPL-3.0 | `COPYING` | `awdl` 的上游 |
| OpenDrop | `11fe7ba` | GPL-3.0 | `LICENSE` | 仅参考/候选，未使用 |
| SwooshDrop | `c0eaff1` | MIT | `LICENSE`（前端；第三方部分另计） | 仅行为参考，不进入路线 |
| 本仓库 | 当前 | 未定 | 无 `LICENSE` 文件 | 发布前必须决定 |

## 2. 进程与链接边界

- `radio.exe` 链接 `libdevourer`（GPL-2.0），因此该可执行文件整体按 **GPL-2.0-only** 分发；分发时必须同时提供对应源码（含本项目对 `radio/` 的修改和构建脚本）。
- `awdl.exe` 基于 OWL，按 **GPL-3.0-or-later** 分发。
- 两者**不链接**，只通过命名管道交换 `radio_link.h`（MIT）定义的消息。这是当前唯一能避免 GPL-2.0-only 与 GPL-3.0-or-later 直接合并的边界。
- 安装包、开机自启或托盘程序如果把 `radio.exe`、`awdl.exe` 和应用层放在同一发布物里，是否被认定为单纯的 aggregate 需要法律确认；README 的自述不能当作结论。
- 任何 `#include` devourer 头并链接其库的新组件都会落入 GPL-2.0；只包含 `radio_link.h` 的客户端不会。
- 应用层（MIT）可以独立发布；但捆绑 GPL 组件的发布物整体不能再宣称「全部 MIT」。

## 3. 固件再分发风险

`hal8821c_fw.c` 的头部写着：从 `morrownr/8821cu-20210916` 提取的 Realtek 固件（FW v5.12.0.4，138984 字节）。该源仓库以 GPL-2.0 发行驱动代码，但 **Realtek 固件本体通常另有再分发条款，上游没有附授权文本**。

- 研究阶段：保留在 `upstream/`（未纳入主仓库）可以接受。
- 面向用户发布：固件数组一旦静态链接进 `radio.exe`，就等于再分发 Realtek 固件。发布前必须三选一：
  1. 取得明确再分发许可；
  2. 改为运行时由用户提供固件文件，发布物不含固件；
  3. 只发布源码与构建脚本，由用户自行构建。
- 在解决之前，不得在 README、安装器或宣传中声称许可证已清理完毕。

## 4. 运行时第三方依赖

| 依赖 | 许可证 | 处理 |
| --- | --- | --- |
| libusb（vcpkg `libusb:x64-windows`） | LGPL-2.1+（以实际安装版本为准） | 动态链接可满足；若静态链接需核对 LGPL 义务 |
| Wintun DLL（WinAWDL 运行时下载） | Wintun 自有许可 | 分发时须附许可文本；不放入主仓库 |
| .NET 10 运行时 | MIT | 随 app 发布时保留声明 |
| Python 依赖（`airdrop/`） | 各自许可 | Phase 4 前生成 SBOM |

## 5. 发布前清单（全部未完成）

- [ ] 逐文件核对 `awdl`、`radio`、`airdrop` 的 SPDX 覆盖率：目前 `awdl/` 顶层仅 1 处 SPDX、`radio/` 10 处，目录级 `LICENSE` 仍在，但不足以保证逐文件一致。
- [ ] 固件再分发授权或改为运行时加载。
- [ ] 确认 libusb 链接方式与 LGPL 合规。
- [ ] Wintun 许可文本随包提供。
- [ ] 准备 GPL 源码提供方式（radio + devourer + 构建脚本 + 修改记录）。
- [ ] 安装包聚合边界的法律确认。
- [ ] 名称与商标：产品名 AirBridge32 不得暗示 Apple 授权或官方兼容认证。
- [ ] 为本仓库选择并加入 `LICENSE`。

## 6. 结论

目标路线在许可证上是「GPL-2.0 无线进程 + GPL-3.0 AWDL 进程 + MIT 应用层」，靠进程边界隔离，技术上可实施；但**固件授权和安装包聚合边界尚未解决**，所以 Phase 0 的许可证项只能算「已识别风险」，不能算通过。
