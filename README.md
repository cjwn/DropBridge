# DropBridge

DropBridge 让 Windows 电脑接收 iPhone 用系统自带「隔空投送」（AirDrop）发来的文件。iPhone 上不用装 App，电脑上也不依赖 WSL2 或虚拟机。它是一个常驻托盘的程序，需要一张专用的 **RTL8821CU USB 无线网卡**：程序直接通过 WinUSB 驱动这张卡，在用户态跑 AWDL（隔空投送底层用的无线协议）。电脑原来的 Wi-Fi 不受影响。

> **状态：内部测试版，只能接收。** iPhone → Windows 已经在真机上传过文件，29,456,959 B 的视频用时 26.84 s（约 1.10 MB/s），落盘哈希与原文件一致。Windows → iPhone 发送已确认无法实现（Q48）。还有稳定性问题没解决，最近三轮源码修复（Q68–Q70）尚未在 Windows 上编译和测试，也还不能对外分发。详见[状态与限制](#状态与限制)。

## 目录

- 使用：[需要什么](#需要什么) · [安装和第一次运行](#安装和第一次运行) · [接收文件](#接收文件) · [托盘菜单](#托盘菜单) · [数据放在哪](#数据放在哪) · [升级和卸载](#升级和卸载) · [常见问题](#常见问题)
- 开发：[工作原理](#工作原理) · [仓库结构](#仓库结构) · [从源码构建](#从源码构建) · [诊断工具](#诊断工具) · [文档](#文档)
- [状态与限制](#状态与限制) · [许可证](#许可证) · [名称](#名称)

---

## 需要什么

- **Windows 10 或 11，64 位。** 托盘程序用的是系统自带的 .NET Framework 4.8，不用另装运行库。现有测试记录都来自 Windows 11。
- **一张 RTL8821CU 芯片的 USB 无线网卡，USB ID 为 `0bda:c820`。** 本项目只验证过这个 ID，自带的驱动包和检查脚本也只认它。建议直插机箱上的 USB 口，不要经过 Hub。
- **一次管理员权限**：第一次运行时要给网卡的 Wi-Fi 接口换装 WinUSB 驱动。
- **iPhone**：用系统自带的隔空投送，不装任何 App，接收范围也不需要特别设置。
- **电脑自带蓝牙（建议有）**：用来发「蓝牙 beacon」，帮 iPhone 更快发现这台电脑。没有蓝牙也能接收，但手机可能要打开分享面板后才看得到电脑，速度也明显更低（见[托盘菜单](#托盘菜单)里的说明）。

## 安装和第一次运行

1. 把 `DropBridge-portable.zip` 解压到一个可写的目录，例如 `D:\DropBridge`。不要放进 Program Files。
2. 插上网卡，双击 `DropBridge.exe`。程序不弹主窗口，只在任务栏右下角通知区域放一个图标。
3. 如果网卡还没装驱动，程序会先弹窗询问是否安装，确认后弹一次「用户账户控制」请求管理员权限，然后自动导入驱动签名证书，用 pnputil 把网卡的 **MI_02（Wi-Fi）接口**换成 WinUSB。每台电脑只做一次，过程记在 `data\logs\driver-install.txt`。
   - 只处理 MI_02。同一张网卡上的 MI_00（蓝牙）和 MI_01 不动。
   - 自动安装失败时，可以从托盘菜单选「安装驱动（首次使用）」重试，或者双击 `setup\Bind-WinUSB.cmd`，按 `setup\README.txt`（仓库里是 [packaging/setup/README.txt](packaging/setup/README.txt)）的步骤用包里带的 Zadig 手动绑定。
4. 托盘图标出现后，网卡初始化需要 40–90 秒，完成后托盘提示就绪，状态变为「运行中」。
5. 第一次成功启动时，程序会打开「开机自启」并提示一次。不需要的话在托盘菜单里取消勾选。

想把网卡换回原来的 Realtek 驱动，按 [docs/BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md) 操作。

## 接收文件

1. 在 iPhone 上分享文件，选隔空投送，列表里点 **DropBridge**（或者你在托盘菜单里改过的设备名称）。
2. 文件默认存到程序文件夹下的 `data\received\`，可以在托盘菜单里改成别的文件夹。双击托盘图标直接打开当前的接收文件夹。
3. 收到文件后会弹出系统通知。图片的通知显示缩略图，并带一个「复制到剪贴板」按钮，复制时位图和文件一起放进剪贴板。
4. 收到实况照片（HEIC/JPG/PNG + MOV）或单张 HEIC 时，默认在同一目录多存一张 `<原名>-still.jpg`，方便在 Windows 上直接查看。

## 托盘菜单

右键单击托盘图标弹出菜单，下面按菜单里的顺序逐项说明。鼠标停在图标上会显示当前的设备名称。

| 菜单项 | 作用 |
| --- | --- |
| **状态：…** | 只显示、不能点。可能是「启动中…」「运行中（ch149 / 20 MHz）」「后端退出，正在恢复…」「正在重启后端…」「启动失败，稍后自动重试」或「多次失败，已停止」。后端异常退出时会自动重启（间隔 1 s、5 s、30 s）；连续失败超过 5 次、其间没有稳定运行满 10 分钟，就停止自动重试，显示「多次失败，已停止」，这时要手动选「重新启动后端」 |
| **打开接收文件夹** | 在资源管理器里打开当前的接收目录 |
| **接收文件夹：<路径>…** | 弹出文件夹选择框，把收到的文件改存到别处，比如桌面或另一块硬盘。会先检查目录能否写入，写不了就不改；能写就存进 `data\settings.ini`，并只在后台重启接收端，不重新初始化网卡。设置已保存但接收端重启失败时会弹出警告。如果启动时这个文件夹不存在（例如移动硬盘没插），本次会改存到默认的 `data\received` 并提示，设置本身保留 |
| **打开日志** | 打开本次会话的日志目录 `data\logs\<会话>\`，里面有 radio、awdld、netd、receiver 等进程的日志 |
| **复制诊断信息** | 把程序路径、运行状态、系统版本，以及每个日志的最后 40 行复制到剪贴板，反馈问题时直接粘贴 |
| **开机自启** | 勾选后登录 Windows 时自动启动（写在当前用户的 HKCU Run 键下），取消勾选就删掉这一项 |
| **安装驱动（首次使用）** | 把网卡的 MI_02 接口换成 WinUSB：弹一次 UAC，导入驱动签名证书，再用 pnputil 安装，装好后自动重启后端。没找到网卡、没拿到管理员权限或安装失败时都会弹窗说明，过程记在 `data\logs\driver-install.txt`。程序启动时发现驱动没装会自动走这一步，这一项是给自动安装失败时重试用的 |
| **设备名称：<名字>…** | 修改 iPhone 隔空投送列表里显示的名字。长度 1–63 个字符，不能包含 `/ \ : * ? " < > \|`、控制字符或方向控制符。改完会重启接收端使新名字生效；后端没在运行时先保存，下次启动生效 |
| **清理通知注册信息** | 确认后删掉开始菜单里的 DropBridge 快捷方式和 `dropbridge://` 协议，删了以后通知上的「复制到剪贴板」按钮就不能用了。卸载前用这一项；再次启动程序会重新注册 |
| **蓝牙 beacon（帮 iPhone 发现这台电脑）** | 默认开启。用电脑自带的蓝牙周期性发送 AirDrop 广播，让 iPhone 的 AWDL 保持唤醒。beacon 进程退出会自动重启；如果反复在启动后很快退出（例如电脑没有可用的蓝牙），改为每 10 分钟重试一次。实测开着时 29 MB 视频跑到 1.09–1.10 MB/s，不开时只有 0.32–0.88 MB/s，而且手机可能要等打开分享面板才看得到这台电脑 |
| **为实况图生成单图副本** | 默认开启。见上面[接收文件](#接收文件)第 4 条，普通照片不会生成 |
| **重新启动后端** | 停掉整条链路（radio、awdld、netd、receiver）再重新拉起，网卡会重新初始化，约需 40–90 秒。手机看不到电脑或传输卡住时使用 |
| **重置链路** | 同样重启整条链路，另外清零低速检测计数。传输中帧率连续偏低时托盘会弹出「链路速率偏低」，用这一项重置 |
| **退出** | 先停 beacon 和文件监视，再按相反顺序停掉所有后端，然后退出。radio 会正常释放网卡，不需要拔插 |

设备名称、接收文件夹、beacon 和实况图副本这四项设置都存在 `data\settings.ini`，是纯文本，可以手动编辑，下次启动时生效。

## 数据放在哪

程序自己的数据都在程序文件夹里，不写 `%LOCALAPPDATA%`，也不安装服务：

```text
DropBridge\
  DropBridge.exe     托盘程序，唯一入口
  backend\           radio / bridge / awdld / awdl_netd / python / receiver / beacon.ps1
  setup\             driver\：WinUSB 驱动包（自签证书 + .cat），首次运行自动安装
                     手动绑定的备用方案：Bind-WinUSB.cmd + Zadig 2.9
  tools\             Start-Chain.ps1（控制台调试用）、toast.ps1、register.ps1
  licenses\  README.txt  VERSION.txt
  data\              首次运行时生成
    settings.ini     设备名、接收文件夹、各项开关
    keys\            TLS 自签证书、AirDrop 实例 ID（升级时要保留，否则手机列表里会多出一个 DropBridge）
    received\        默认的接收目录
    logs\<会话>\     各进程日志；driver-install.txt
```

写到系统里的只有这几样：

- 开机自启项（HKCU `...\CurrentVersion\Run`）；
- 通知按钮需要的开始菜单快捷方式 `DropBridge.lnk`、`dropbridge://` 协议和通知身份（HKCU `Software\Classes`）；
- 首次运行安装的 WinUSB 驱动包，以及给它签名用的自签证书（导入本机的「受信任的根证书颁发机构」和「受信任的发布者」）。

## 升级和卸载

- **升级**：覆盖 `DropBridge.exe` 和 `backend\`，保留 `data\`。
- **卸载**：在托盘菜单里取消「开机自启」，点「清理通知注册信息」，然后退出，再删除整个文件夹。想把网卡换回原驱动，按 [docs/BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md) 或包里 `setup\README.txt` 的「怎么恢复原驱动」操作。

## 常见问题

**手机列表里看不到 DropBridge。** 先确认托盘状态是「运行中」（启动后要等 40–90 秒）。确认「蓝牙 beacon」开着。还不行就点「重新启动后端」。可以看 `data\logs\<会话>\awdld.log`，正常时里面能看到 `peers 1`。已知问题：成功传完一次以后，手机有时会再也连不上，重启后端能恢复（Q51，根因未查清）。

**托盘提示「网卡没响应，请拔插」。** radio 进程超过 30 s 没有输出时，程序会判定网卡卡死并整链重启；3 分钟内第二次发生就会弹这个提示。拔下网卡再插回，然后点「重新启动后端」。

**托盘提示「链路速率偏低」。** 选「重置链路」后重试。两台 iPhone 同时打开隔空投送时速度会掉到约 0.1 MB/s，这是已知问题（Q51）。

**状态显示「多次失败，已停止」。** 用「复制诊断信息」或「打开日志」看原因，再手动点「重新启动后端」。

**驱动自动安装失败。** 看 `data\logs\driver-install.txt`。可以用托盘「安装驱动（首次使用）」重试，或用 `setup\Bind-WinUSB.cmd` + Zadig 手动绑定。只绑 MI_02。

**Windows 弹 SmartScreen 警告。** 程序没有代码签名，这是预期现象（见[状态与限制](#状态与限制)）。

**能从电脑发文件给 iPhone 吗？** 不能。发现、出站 TCP 和 TLS 握手都已走通，但 iPhone 用 TLS alert 拒绝了本项目的自签证书，已确认无法实现（Q48，记录见 [docs/SEND_PATH.md](docs/SEND_PATH.md)）。

**想在控制台里调试整条链路。** 用包里的 `tools\Start-Chain.ps1`。默认只打印将要执行的命令（dry-run），加 `-Run` 才会真正初始化网卡并发射无线帧。

---

## 工作原理

```text
iPhone ──AWDL 空口──▶ RTL8821CU（MI_02 绑定 WinUSB）
  └▶ airbridge_radio.exe    devourer 用户态驱动（libusb），独占 USB，TCP 49876
  └▶ radio_pipe_bridge.exe  命名管道 ↔ radio 的 TCP
  └▶ awdld.exe              OWL 的 AWDL 控制面：选举、时隙、信道序列（打过补丁，走 IPC 后端）
  └▶ awdl_netd.exe          lwIP 用户态网络栈、mDNS 代理、TCP 8771 代理
  └▶ python.exe receiver.py AirDrop 的 TLS / HTTP / DVZip 接收、落盘
  └▶ 接收文件夹
```

`DropBridge.exe`（C# WinForms，系统自带的 .NET Framework 4.8）是唯一入口：单实例；按顺序拉起各后端；看门狗（进程退出、radio 静默超过 30 s）；低速告警；用作业对象保证托盘被强杀时子进程一起退出；设置、通知、开机自启、安装驱动；另外启动 `beacon.ps1` 通过本机蓝牙发 AirDrop BLE 广播。radio 不放进作业对象，而是监视父进程和一个停止事件，自己正常收尾，避免强杀时把网卡卡死。

各组件分成独立进程，只通过管道和本地 TCP 通信。这样做的一个原因是让 GPL-2.0-only（radio 链接的 devourer）和 GPL-3.0（基于 OWL 的 awdld）的代码不在同一个可执行文件里混合。

托盘程序固定使用下面这组配置，都是真机对比后定下的：

| 项目 | 值 | 原因 |
| --- | --- | --- |
| 信道 / 带宽 | 149 / 20 MHz | 40 MHz 实测只有 172–190 KB/s，用久了还会退化 |
| 同步主控 | 由 Windows 担任（`AWDL_ELECTION_METRIC=200`、`AWDL_ELECTION_COUNTER=100000`） | 主控被 iPhone 抢走后速度会掉到约 200 KB/s |
| 信道序列 | `social`：第 8 时隙用信道 6，其余用 149 | iPhone 加入后会立刻跟随 |
| TCP 合并 | 开启 | 和 social 序列搭配最快（Q41）；在 fixed 序列下开启反而有害（Q39/Q40） |
| 空口流数 | `-s 1` | 网卡是 1T1R；真机 A/B 中 `-s 1` 和 `-s 2` 只差 0.9%（Q54） |
| lwIP 输入队列 | 64 槽，满了等待重投，不丢帧 | 原来 8 槽时会丢帧，速度塌到 0.38 MB/s，改后升到 0.85 MB/s（Q31/Q38） |

更多设计背景见 [docs/FINAL_PLAN.md](docs/FINAL_PLAN.md) 和 [docs/STATUS.md](docs/STATUS.md)。

## 仓库结构

```text
src/supervisor/    托盘程序（C#）及其测试
src/sfx/           单文件自解压 stub（C#）
src/radio_usb/     airbridge_radio（链接 devourer）、radio_pipe_bridge、radio_link.h 协议头
src/awdl_netd/     基于 lwIP 的用户态网络栈和代理（C++）
src/beacon/        蓝牙 beacon 脚本
patches/           对上游的补丁：devourer、WinAWDL（IPC 后端、身份证书）、OWL
tools/             构建、打包、诊断脚本和测试
packaging/         包内 README、WinUSB 驱动包、手动绑定工具（setup/）
assets/            图标
docs/              计划、审计、排障记录、工作队列
evidence/          日志、抓包摘要和测试记录
upstream/          上游原样副本，不纳入主仓库（.gitignore），按 docs/sources.lock.json 恢复
```

## 从源码构建

需要 Windows 10/11 x64、Visual Studio（MSVC + CMake）、Windows PowerShell 和 Git。构建脚本默认使用 `Visual Studio 17 2022` 生成器。所有依赖都放在 `build\deps\` 下，不改系统 PATH，不做全局 vcpkg 集成。

**1. 恢复上游源码。** 按 [docs/sources.lock.json](docs/sources.lock.json) 里的 URL 和 commit，把五个上游仓库克隆到 `upstream\<name>`（winawdl、devourer、swooshdrop、owl、opendrop）。winawdl 需要带子模块（OWL、radiotap）。然后应用补丁：

```powershell
.\tools\Apply-AwdlPatches.ps1      # 给 upstream\winawdl 及其 OWL 子模块打补丁
```

devourer 的补丁由 `Build-Radio.ps1` 自动处理：它在 `build\devourer-airbridge` 准备一份锁定版本的副本再打补丁，不改 `upstream\devourer`。

**2. 准备依赖。**

```powershell
.\tools\Setup-BuildDeps.ps1 -WithOpenSsl   # vcpkg + libusb + pkgconf（+ OpenSSL 用于 devourer 自检）
```

另外有两项没有脚本，需要手动准备：

- lwIP 源码放到 `build\deps\lwip`，lwip-contrib 放到 `build\deps\lwip-contrib`。版本没有锁定，最近一次完整编译（Q50）用的是 `STABLE-2_2_1_RELEASE`。
- `build\python-embed`：Python 3.12 embeddable x64，加装 `cryptography`。Q50 用的是 Python 3.12.10、cryptography 50.0.2。

**3. 编译。**

```powershell
.\tools\Build-Devourer.ps1                 # 可选：只编 8821C 的 devourer，并跑无硬件自检
.\tools\Build-Radio.ps1                    # airbridge_radio + radio_pipe_bridge + 两项离线测试
.\tools\Build-Awdl.ps1                     # awdld（MinGW，IPC 后端，不用 Wintun）
cmake -S src\awdl_netd -B build\awdl_netd -A x64
cmake --build build\awdl_netd --config Release
ctest --test-dir build\awdl_netd -C Release   # netd 测试；需要 Python 才会注册其中 5 项
.\tools\Build-Supervisor.ps1 -Test         # 托盘程序（系统自带 csc）+ 单元测试
```

**4. 打包。**

```powershell
.\tools\Package-Distributable.ps1          # dist\DropBridge\ 和 dist\DropBridge-portable.zip
dist\DropBridge\DropBridge.exe --check     # 只解析路径，不启动链路；应 exit 0、无 MISSING
```

WinUSB 驱动包（`packaging/driver/`）已经提交在仓库里，打包时直接复制。要重新生成签名，用 `tools\Build-DriverPackage.ps1`（需要 Windows SDK 的 makecat 和 signtool）。另有 `tools\Package-Headless.ps1`（无托盘的控制台包）和 `tools\Build-Sfx.ps1`（把 headless 包做成单文件自解压 `dist\DropBridge.exe`），用户交付以文件夹和 zip 为准。

构建细节和历史问题见 [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md) 和 [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)。

## 诊断工具

```powershell
.\tools\Inspect-Hardware.ps1        # 只读：枚举 USB，识别网卡
.\tools\Check-Binding.ps1           # 只读：MI_02 当前用的驱动（0 = WinUSB，1 = 其他驱动，2 = 网卡不在）
.\tools\Start-Chain.ps1             # 控制台起整条链路；默认 dry-run，加 -Run 才会初始化网卡、发射无线帧
.\tools\Collect-Session.ps1 -Latest -PackageRoot dist\DropBridge -Tag <标签>   # 把一次会话的日志冻结到 build\diagnostics 并记哈希
python .\tools\analyze_session.py --latest dist\DropBridge   # 从日志算真实 MB/s、帧率、netd 丢帧
.\tools\Monitor-Soak.ps1            # 长时间运行时采样内存、句柄和端口，只读
python .\tools\check_project.py     # 检查文档、证据和版本锁是否齐全（需要 upstream/ 已恢复）
```

只读工具不会初始化网卡、换驱动或发射无线帧。初始化网卡、发射无线帧、换驱动、改网络或防火墙都需要用户授权，范围见 [AGENTS.md](AGENTS.md) 和 [docs/WORK_QUEUE.md](docs/WORK_QUEUE.md) 第 1 节。

## 文档

- [docs/WORK_QUEUE.md](docs/WORK_QUEUE.md)：**进度以这里为准**，每一项都附有证据和验证层级。
- [WORK_LOG.md](WORK_LOG.md)：按时间倒序的工作记录。
- [docs/STATUS.md](docs/STATUS.md)：当前状态快照（截至 Q70，2026-11-10）。
- [docs/FINAL_PLAN.md](docs/FINAL_PLAN.md)：最终方案和验收指标。
- [docs/PLAN.md](docs/PLAN.md)、[docs/PHASE0_AUDIT.md](docs/PHASE0_AUDIT.md)：阶段计划、源码审计和 Go / No-Go 结论。
- [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)：分发形态。
- [docs/BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md)：驱动绑定和恢复。
- [docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md)：许可证核查。
- [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md)：构建记录。
- [docs/SEND_PATH.md](docs/SEND_PATH.md)：发送方向的排查记录（结论：无法实现）。
- 排障记录：[STALL_DIAGNOSIS](docs/STALL_DIAGNOSIS.md)、[LWIP_INPUT_QUEUE](docs/LWIP_INPUT_QUEUE.md)、[TCP_DIAGNOSTICS](docs/TCP_DIAGNOSTICS.md)。
- 历史快照（可能已过时）：[HANDOFF.md](HANDOFF.md)、[docs/HANDOFF_2026-10-28.md](docs/HANDOFF_2026-10-28.md)、[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
- [AGENTS.md](AGENTS.md)：开发约定。

---

## 状态与限制

### 验证层级

本项目把下面四层分开记录，彼此不能替代：

| 层级 | 现状 |
| --- | --- |
| 源码实现 | 接收链路、托盘监管、设置、通知、自动装驱动、beacon 均已实现 |
| 本机编译 | 最近一次完整编译和打包是 Q50（2026-10-30），托盘程序在 Q59–Q67 期间又重编过。**Q68、Q69、Q70 的修复只提交了源码，没有编译、没有运行测试**（Q69、Q70 在没有 Windows 工具链的 Linux 环境里完成）。所以当前 HEAD 的代码与最后一次测过的包并不相同 |
| 本机硬件 | 网卡初始化、收发、起停、强杀后不残留、停止时不卡死网卡（Q55/Q57/Q58）已在真机验证 |
| 真实文件传输 | 见下表，落盘哈希都与源文件一致 |

Q68–Q70 待在 Windows 上做的编译和验证清单见 [docs/WORK_QUEUE.md](docs/WORK_QUEUE.md) 对应各行。

### 实测速度

速度都是落盘的真实文件字节数除以墙钟时间，不是 PHY 速率。

| 文件 | 结果 | 出处 |
| --- | --- | --- |
| 1,207,111 B 图片，成品文件夹 | 1.23 s（0.98 MB/s） | Q45 |
| 29,456,959 B 视频，成品文件夹 | 26.84 s（1.0975 MB/s） | Q46 |
| 同一视频，`-s 2` / `-s 1`，beacon 开 | 26.89 s / 27.04 s（1.10 / 1.09 MB/s） | Q54 |
| 同一视频，历史最好 | 1121 KB/s（29.46 MB / 26.3 s） | Q15，`evidence/q15-master-mode.log` |
| 两台 iPhone 同时在线 | 0.08–0.12 MB/s | Q51 |

### 不能做 / 没解决的

1. **只能接收。** Windows → iPhone 发送已确认无法实现，不再推进（Q48）。
2. **首次传输后发现丢失、多机降速**（Q51）：传完一次后手机再发起的连接可能反复失败，重启链路能恢复；两台手机同时在线时降到约 0.1 MB/s。根因未查清。
3. **24 小时常驻测试没跑完**（Q7）。radio 假死检测已实现（Q57）。
4. **receiver 对 `/Discover` 分块报文的解析过严**（Q43），日志里有 WARNING，还没修。
5. **驱动的「未绑定 → 已绑定」转换没有真机验证过**（Q53），开发机的网卡早已绑定。
6. 开发机上这张网卡的蓝牙接口（MI_00）从 2026-10-28 起一直是 Problem 31。不影响 AirDrop，因为 beacon 用的是电脑自带的蓝牙。

### 对外分发前还缺什么

1. **Realtek 固件的再分发授权**：radio 静态链接了从上游提取的 Realtek 固件。对外发布前只能三选一：取得授权、改为运行时由用户自己提供、或只发布源码（[docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md) §3）。
2. **代码签名**：没有签名，SmartScreen 会弹警告。驱动包用的是自签证书。
3. **干净机器验收**（Q13）：在全新的 Windows 上从解压到收到文件完整走一遍。
4. **WinUSB 回滚演练。**
5. **包内许可证文本不全**：libusb 的 LGPL、lwIP 的 BSD 声明、VC++ 运行库条款都还没有随包附带，内嵌 Python 及其依赖的许可证也没核对（[docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md) §5、§7）。

## 许可证

本仓库自己写的代码和文档采用 **MIT 许可证**（见 [LICENSE](LICENSE)，2026-11-10 决定）。MIT 只覆盖本仓库的原创部分：第三方组件保持各自的许可证，`patches/` 里的补丁沿用所修改上游的许可证，链接了 GPL 代码的可执行文件仍按 GPL 分发，范围说明见 [NOTICE](NOTICE)。所以包含这些组件的发布包整体不能当作 MIT 分发。各组件的情况如下（依据 [docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md)，未做法律结论）：

| 组件 | 来源 | 许可证 |
| --- | --- | --- |
| 本仓库自写代码（托盘、stub、桥、netd、radio 的自写部分、脚本、文档） | 本仓库 | MIT |
| `patches/` 下的补丁 | 对上游的修改 | 随所修改的上游（devourer GPL-2.0；OWL、WinAWDL `awdl/` GPL-3.0-or-later；WinAWDL `airdrop/` MIT） |
| `airbridge_radio.exe` | 本仓库代码（MIT）链接 OpenIPC devourer | 因链接 devourer，二进制整体按 GPL-2.0 分发 |
| Realtek 8821C 固件（编进 radio） | devourer 内的固件数组 | 没有独立授权声明，见上一节 |
| `awdld.exe` | WinAWDL `awdl/`，基于 OWL | GPL-3.0-or-later |
| `receiver.py` 等 AirDrop 接收代码 | WinAWDL `airdrop/`（打了本仓库补丁） | MIT |
| `radio_link.h` | 进程间协议头 | MIT（文件头 SPDX） |
| libusb | vcpkg | LGPL-2.1+，动态链接 |
| Zadig 2.9（`setup\`，手动绑定用） | 原样附带 | GPL-3.0，包内附全文和源码要约 |

`upstream/` 里是上游仓库的原样研究副本，被 `.gitignore` 排除，不属于本仓库，版本锁定在 [docs/sources.lock.json](docs/sources.lock.json)。打包脚本会把本仓库的 `LICENSE`、`NOTICE`，以及 WinAWDL（根目录和 `airdrop/`）、devourer 的 LICENSE 和 OWL 的 COPYING 各自改名后复制进包内的 `licenses\`。

## 名称

项目最初暂定名为 AirBridge32，2026-10-30 起对用户显示的名字统一为 DropBridge（Q49）。内部组件（如 `airbridge_radio.exe`、驱动包 `airbridge-winusb.inf`）的名字没有改，所以日志和进程列表里仍会看到 airbridge。
