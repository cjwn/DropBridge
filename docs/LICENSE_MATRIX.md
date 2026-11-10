# 许可证与再分发核查（Phase 0 任务 4）

状态：**静态核查完成，未做法律结论**。Phase 0（2026-10-27）的核查对象为当时 `docs/sources.lock.json` 锁定的五个提交（其中 devourer 为 `f7e0731`），以及 WinAWDL 锁定的 devourer 子模块 `8d936de2a3805ce126e5236349afe40e645aaf9b`。本轮没有把任何上游实现复制进主仓库，也没有修改上游文件。

> **2026-11-10 更新**：按当前组件（托盘 `DropBridge.exe`、`airbridge_radio.exe`、`radio_pipe_bridge.exe`、`awdld.exe`、`awdl_netd.exe`、嵌入式 Python + receiver、`beacon.ps1`、Zadig）改写了「用途」列、§2、§4、§5，并补了 lwIP、Python、cryptography、`patches/` 等条目和 §7「包内实际附带的许可证文本」；本仓库自身许可证由所有者在同日定为 MIT。本次更新在没有 `upstream/` 和 `build/` 的 Linux 上进行，只依据本仓库里的文件、证据和记录；标「未在本仓库内核对」的条目是通用常识，不是核查结论。devourer 已在 Q50（2026-10-30，提交 `5331892`）升到 `34a9e6f`，它的 `LICENSE` 没有在新提交上重新核对。

## 1. 组件矩阵

| 组件 | 版本 | 许可证 | 依据 | 是否在目标运行路径 |
| --- | --- | --- | --- | --- |
| WinAWDL `app/`（上游的 .NET 10 WPF 应用） | `2196fb4` | MIT | 根 `LICENSE`；README「Licensing」声明除 `radio`、`awdl` 外全部 MIT | 不使用（曾是 Phase 4 候选；托盘改为本仓库自写的 `DropBridge.exe`） |
| WinAWDL `scripts/`、`installer/`、`tools/`、`probes/` | `2196fb4` | MIT | 同上；`installer` 文件头有 SPDX | 构建/安装参考；`scripts/airdrop-beacon.ps1` 以 `src/beacon/airdrop-beacon.ps1` 的形式进入发布物（`backend\beacon.ps1`，Q56；2026-11-10 起文件头注释有本仓库改动） |
| WinAWDL `awdl/` | `2196fb4` | GPL-3.0-or-later | `awdl/CMakeLists.txt:1` SPDX；`awdl/LICENSE`；README 说明基于 OWL | `awdld.exe`（打 `patches/awdl-ipc-host.patch`，走 IPC 后端，不用 Wintun；补丁新增文件头为 GPL-3.0-or-later SPDX） |
| WinAWDL `radio/` | `2196fb4` | GPL-2.0-only | `radio/CMakeLists.txt:1` SPDX；`radio/LICENSE`；README 说明链接 devourer | 不使用：当时只支持 8852C + PCIe；`airbridge_radio.exe` 由本仓库 `src/radio_usb` 构建 |
| `radio/radio_link.h` | `2196fb4` | MIT | 文件头 SPDX；README 明确为进程间协议，供两侧共同包含 | IPC 接缝；本仓库 `src/radio_usb/radio_link.h` 文件头同为 `SPDX-License-Identifier: MIT` |
| WinAWDL `airdrop/` | `2196fb4` | MIT | `airdrop/LICENSE`（adapt 自 omdrop，保留其 MIT 声明）；`identity.py` 文件头有 MIT SPDX（见 `patches/airdrop-identity.patch` 的上下文），其余文件的文件头未在本仓库内核对 | `backend\airdrop\*.py`（`receiver.py` 等，打 `patches/airdrop-identity.patch` 和 `patches/awdl-ipc-host.patch` 中 `airdrop/` 的部分） |
| WinAWDL `driver/regproxy/` | `2196fb4` | MIT | `regproxy.c`、`regproxy_ioctl.h`、`.inf` 均有 SPDX | 不使用（PCIe 路线） |
| devourer（OpenIPC） | `34a9e6f`（锁定；Phase 0 核查时为 `f7e0731`） | GPL-2.0 | `f7e0731` 的 `LICENSE`（GPL-2.0 全文）；CLAUDE.md 未声明例外。`34a9e6f` 未重新核对 | 链接进 `airbridge_radio.exe`（打 `patches/devourer-airbridge.patch`，补丁新增文件头为 GPL-2.0-only SPDX） |
| devourer 固件数组 `hal/*_fw.c` | 同上（在 `f7e0731` 上核查） | 无独立授权声明 | `hal8821c_fw.c` 头部注明从 `morrownr/8821cu-20210916` 提取（Realtek FW v5.12.0.4） | 静态链接进 `airbridge_radio.exe` |
| OWL | `0ee3263` | GPL-3.0 | `COPYING` | `awdl` 的上游。实际构建用的是 WinAWDL 的 OWL 子模块 `da255a7`（打 `patches/owl-airbridge.patch`），打包脚本复制的也是该子模块的 `COPYING`；`da255a7` 的许可证文件没有单独核对 |
| OpenDrop | `11fe7ba` | GPL-3.0 | `LICENSE` | 仅参考/候选，未使用 |
| SwooshDrop | `c0eaff1` | MIT | `LICENSE`（前端；第三方部分另计） | 仅行为参考，不进入路线 |
| 本仓库自写代码和文档 | 当前 | MIT（所有者 2026-11-10 决定） | 仓库根 `LICENSE`（MIT，「Copyright (c) 2026 DropBridge contributors」）；源文件不逐个加 SPDX，`src/radio_usb/radio_link.h` 原本就带 MIT SPDX | `src/supervisor`（`DropBridge.exe`）、`src/sfx`（单文件 stub）、`src/radio_usb` 的自写文件、`src/awdl_netd`、`src/beacon`、`tools/`、`packaging/` 脚本、文档。只覆盖本仓库的原创部分，不改变第三方部分和与之结合的二进制的许可证（见下面几行和 §2）；`patches/` 不在此列 |
| `patches/` 下的补丁 | 当前 | 随所修改的上游 | 每个补丁都是所改上游的衍生作品，不因放在本仓库而成为 MIT | `devourer-airbridge.patch`：GPL-2.0；`owl-airbridge.patch`、`awdl-ipc-host.patch` 中 `awdl/` 的部分：GPL-3.0-or-later；`airdrop-identity.patch`、`awdl-ipc-host.patch` 中 `airdrop/` 的部分：MIT |
| lwIP（含 contrib 的 win32 移植） | 未锁定；Q50 构建用 `STABLE-2_2_1_RELEASE`（`77dcd25`） | BSD-3-Clause（**未在本仓库内核对**） | 本仓库没有 lwIP 源码或许可证文件；版本见 `evidence/compile-windows-2026-10-30.log` | 源码由 `src/awdl_netd/CMakeLists.txt` 直接编译进 `awdl_netd.exe` |
| CPython embeddable | 未锁定；Q50 构建用 3.12.10 x64 | PSF-2.0（**未在本仓库内核对**） | 本仓库没有其许可证文件 | `backend\python\`（整个 `build\python-embed` 原样复制） |
| cryptography 及依赖 cffi、pycparser | 未锁定；Q50 构建用 cryptography 50.0.2、cffi 2.1.1、pycparser 3.12 | **未在本仓库内核对**（cryptography 通常为 Apache-2.0 或 BSD-3-Clause 双许可，其 Windows wheel 通常静态包含 OpenSSL；cffi 通常为 MIT；pycparser 通常为 BSD-3-Clause） | 本仓库没有其许可证文件 | `backend\python\` |
| Zadig 2.9（含 libwdi） | 2.9.788，SHA-256 `4ecaa95d…` | GPL-3.0 | `packaging/setup/licenses/zadig-GPL-3.0.txt` 全文；`packaging/setup/licenses/ZADIG.txt` 记版本、哈希、上游和源码书面要约 | `setup\zadig-2.9.exe`，只在手动绑定时使用 |

## 2. 进程与链接边界

- `airbridge_radio.exe` 链接 devourer（GPL-2.0），因此该可执行文件整体按 **GPL-2.0-only** 分发；分发时必须同时提供对应源码（含 `src/radio_usb`、`patches/devourer-airbridge.patch` 和构建脚本）。
- `awdld.exe` 基于 OWL，按 **GPL-3.0-or-later** 分发。
- 两者**不链接**：awdld 通过命名管道连 `radio_pipe_bridge.exe`，桥再经本地 TCP 49876 连 radio，传的是 `radio_link.h`（MIT）定义的消息，桥只搬字节。这是当前唯一能避免 GPL-2.0-only 与 GPL-3.0-or-later 直接合并的边界。
- 发布物（文件夹 + zip，托盘负责开机自启和拉起各进程）把 `airbridge_radio.exe`、`awdld.exe` 和应用层放在一起，是否被认定为单纯的 aggregate 需要法律确认；README 的自述不能当作结论。
- 任何 `#include` devourer 头并链接其库的新组件都会落入 GPL-2.0；只包含 `radio_link.h` 的客户端不会。
- 本仓库自写的源码（托盘、桥、netd、radio 的自写部分等）自 2026-11-10 起为 MIT；应用层里来自 WinAWDL `airdrop/` 的部分也是 MIT。MIT 与 GPL 兼容，所以 `airbridge_radio.exe` 作为分发的二进制仍整体按 GPL-2.0 分发（因为链接 devourer），`awdld.exe` 仍按 GPL-3.0-or-later 分发，两者没有冲突。捆绑 GPL 组件的发布物整体不能宣称「全部 MIT」。

## 3. 固件再分发风险

`hal8821c_fw.c` 的头部写着：从 `morrownr/8821cu-20210916` 提取的 Realtek 固件（FW v5.12.0.4，138984 字节）。该源仓库以 GPL-2.0 发行驱动代码，但 **Realtek 固件本体通常另有再分发条款，上游没有附授权文本**。

- 研究阶段：保留在 `upstream/`（未纳入主仓库）可以接受。
- 面向用户发布：固件数组一旦静态链接进 `airbridge_radio.exe`，就等于再分发 Realtek 固件。发布前必须三选一：
  1. 取得明确再分发许可；
  2. 改为运行时由用户提供固件文件，发布物不含固件；
  3. 只发布源码与构建脚本，由用户自行构建。
- 在解决之前，不得在 README、安装器或宣传中声称许可证已清理完毕。

## 4. 运行时第三方依赖

| 依赖 | 许可证 | 处理 |
| --- | --- | --- |
| libusb（`Setup-BuildDeps.ps1` 用 vcpkg `libusb:x64-windows`；Q50 构建用 MSVC 直接编译的 1.0.30） | LGPL-2.1+（以实际安装版本为准） | 以 `backend\libusb-1.0.dll` 动态链接随包分发；**包内没有附 LGPL 文本**（见 §7） |
| lwIP | BSD-3-Clause（未在本仓库内核对） | 编译进 `awdl_netd.exe`；BSD 条款要求二进制分发附版权声明，**包内没有附**（见 §7） |
| 嵌入式 Python 3.12 + cryptography / cffi / pycparser | 见 §1（均未在本仓库内核对） | 整个 `build\python-embed` 原样复制到 `backend\python\`；其中是否带各自的许可证文件取决于手动准备的目录，本仓库无法核对 |
| VC++ 运行库（`msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`） | Microsoft 再分发条款（未在本仓库内核对） | `Package-Distributable.ps1` 从构建机的 `System32` 复制到 `backend\` 和 `backend\python\`；未附任何条款文本 |
| .NET Framework 4.8 | 系统自带 | 托盘和单文件 stub 用系统自带的 csc 编译、运行在系统自带的 .NET Framework 4.8 上，不随包分发 |
| Wintun DLL | Wintun 自有许可 | **历史**：WinAWDL 原方案运行时下载 Wintun。当前 awdld 走 IPC 后端，`Build-Awdl.ps1` 在 Q70 去掉了 Wintun，发布物不含 Wintun |
| .NET 10 运行时 | MIT | **历史**：只与不使用的 WinAWDL `app/` 有关，发布物不含 |

## 5. 发布前清单（全部未完成）

- [ ] 逐文件核对 `awdl`、`radio`、`airdrop` 的 SPDX 覆盖率：目前 `awdl/` 顶层仅 1 处 SPDX、`radio/` 10 处，目录级 `LICENSE` 仍在，但不足以保证逐文件一致。
- [ ] 固件再分发授权或改为运行时加载。
- [ ] 确认 libusb 链接方式与 LGPL 合规，并在包内附 LGPL 文本。
- [ ] ~~Wintun 许可文本随包提供~~：当前链路不用 Wintun（Q70），此项不再适用。
- [ ] 包内附 lwIP 的版权声明和 BSD 条款；锁定 lwIP 版本。
- [ ] 核对嵌入式 Python、cryptography（及其内含的 OpenSSL）、cffi、pycparser 的许可证，确认包内带全所需文本；锁定版本。
- [x] 包内附 WinAWDL `airdrop/LICENSE`（omdrop 的 MIT 声明）：2026-11-10 起打包脚本复制为 `licenses\WinAWDL-airdrop-LICENSE.txt`（脚本未在 Windows 上运行验证）。
- [ ] 确认 VC++ 运行库的再分发方式。
- [ ] 在 `34a9e6f` 上重新核对 devourer 的 `LICENSE`，在 `da255a7` 上核对 OWL 子模块的 `COPYING`。
- [ ] 准备 GPL 源码提供方式（`src/radio_usb` + devourer + 补丁 + 构建脚本 + 修改记录；awdld 对应的 WinAWDL `awdl/`、OWL 及补丁）。
- [ ] 安装包聚合边界的法律确认。
- [ ] 名称与商标：产品名 DropBridge（Q49 前为 AirBridge32）不得暗示 Apple 授权或官方兼容认证。
- [x] 打包脚本把仓库根 `LICENSE`、`NOTICE` 复制进包内（2026-11-10，`licenses\DropBridge-LICENSE.txt`、`DropBridge-NOTICE.txt`；脚本未在 Windows 上运行验证）。

## 6. 结论

目标路线在许可证上是「GPL-2.0 无线进程 + GPL-3.0 AWDL 进程 + MIT 应用层（WinAWDL `airdrop/` 和本仓库自写代码）」，靠进程边界隔离，技术上可实施；但**固件授权和安装包聚合边界尚未解决**，所以 Phase 0 的许可证项只能算「已识别风险」，不能算通过。

## 7. 包内实际附带的许可证文本

依据 `tools/Package-Distributable.ps1`（文件夹 + zip）和 `tools/Package-Headless.ps1`（单文件 exe 的载荷），2026-11-10 读脚本得出，没有打开实际的包核对：

| 文件 | 来源 | 条件 |
| --- | --- | --- |
| `licenses\DropBridge-LICENSE.txt`、`licenses\DropBridge-NOTICE.txt` | 仓库根 `LICENSE`（MIT）、`NOTICE`（适用范围） | 源文件存在才复制（下同） |
| `licenses\WinAWDL-LICENSE.txt` | `upstream\winawdl\LICENSE`（WinAWDL 根，MIT） | |
| `licenses\WinAWDL-airdrop-LICENSE.txt` | `upstream\winawdl\airdrop\LICENSE`（含 omdrop 的 MIT 声明） | |
| `licenses\devourer-LICENSE.txt` | `upstream\devourer\LICENSE`（GPL-2.0） | |
| `licenses\OWL-COPYING.txt` | `upstream\winawdl\upstream\owl\COPYING`（GPL-3.0） | |
| `setup\licenses\zadig-GPL-3.0.txt`、`setup\licenses\ZADIG.txt` | `packaging/setup/licenses/`（已提交） | 总是复制 |
| `backend\airdrop\*.py` 自带的 SPDX 文件头（至少 `identity.py` 有） | WinAWDL `airdrop/` | 随源码一起 |
| `backend\python\` 里的任何许可证文件 | `build\python-embed` 原样复制 | 取决于手动准备的目录，未核对 |

2026-11-10 之前，脚本按原文件名复制，WinAWDL 和 devourer 的 `LICENSE` 同名，后者覆盖前者，包里缺 WinAWDL 的 MIT 文本；同日改为各自改名复制，并加入本仓库的 `LICENSE`、`NOTICE` 和 WinAWDL `airdrop/LICENSE`。此前打出的包仍有这个缺口。

没有附带的：libusb 的 LGPL 文本、lwIP 的 BSD 声明、VC++ 运行库条款、Realtek 固件的任何授权文本（上游本来就没有）。这些都是发布前要处理的缺口，见 §5。
