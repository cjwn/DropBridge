# DropBridge

让 Windows 电脑直接接收 iPhone 系统自带的「隔空投送」（AirDrop）。不用在 iPhone 上装 App，也不依赖 WSL2 或虚拟机。前提是插一张 **RTL8821CU 芯片的 USB 无线网卡**，DropBridge 只用这张卡。

> **状态：内部测试版。** iPhone → Windows 已经能真机传文件。Windows → iPhone 已确认无法实现，本项目只做接收。目前还不能对外分发，原因见[发布前还缺什么](#发布前还缺什么)。

## 现在能做到什么

| 项目 | 情况 | 证据 |
| --- | --- | --- |
| iPhone → Windows 传文件 | ✅ 真机通过。29,456,959 B 视频 26.84 s（**1.10 MB/s**），哈希一致；1.2 MB 图片 1.23 s | WORK_QUEUE Q46、Q45、Q54 |
| 最好成绩 | 1121 KB/s（29.46 MB / 26.3 s） | `evidence/q15-master-mode.log` |
| 连续稳定 | ⚠️ 没有完全解决。第一次传完以后可能找不到设备；两台手机同时在线时会降到约 0.1 MB/s（Q51，原因还没查清） | `evidence/discovery-recovery-2026-10-30.log` |
| Windows → iPhone | ⛔ 已确认无法实现，不再推进（Q48）。发现、出站 TCP 和 TLS 握手都已走通，但 iPhone 以 TLS alert 拒收本项目的自签证书 | `docs/SEND_PATH.md` |

速度都是按真实文件字节数除以墙钟时间算的，不是 PHY 速率。

## 使用方法

1. 把 `DropBridge-portable.zip` 解压到任意可写目录，不要放进 Program Files。
2. 插上网卡，双击 `DropBridge.exe`。第一次运行时，程序会把网卡的 **MI_02（Wi-Fi）接口**换成 WinUSB 驱动，过程中弹一次 UAC 管理员确认。MI_00（蓝牙）接口不动。也可以手动换：包里带了 `setup\Bind-WinUSB.cmd` 和 Zadig。
3. 托盘出现图标后等待 40–90 秒，网卡初始化完成就会显示就绪。
4. 在 iPhone 上把隔空投送设为「所有人（10 分钟）」，分享时选 DropBridge。

收到文件后会弹出系统通知，图片通知带「复制图片」按钮。默认开机自启，可以在托盘菜单里关掉。

托盘菜单的具体用法见下一节。

所有数据都留在程序文件夹里，不写 `%LOCALAPPDATA%`，也不安装服务：

```text
DropBridge\
  DropBridge.exe     托盘监管程序，唯一入口
  backend\           radio / bridge / awdld / awdl_netd / python / receiver
  setup\             手动绑定驱动用的工具（Zadig + 引导脚本）
  data\              首次运行时生成
    settings.ini     设备名、接收文件夹、开关
    keys\            TLS 自签证书、AirDrop 实例 ID
    received\        收到的文件
    logs\<会话>\     各进程日志
```

升级时覆盖 `DropBridge.exe` 和 `backend\`，保留 `data\`。卸载时先在托盘里退出，再删掉整个文件夹。想换回原来的网卡驱动，按 [docs/BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md) 操作。

## 托盘右键菜单

右键单击托盘图标就会弹出菜单，各项按菜单里的顺序列在下面。**双击**托盘图标直接打开接收文件夹。把鼠标停在图标上，会显示当前的设备名称。

| 菜单项 | 作用 |
| --- | --- |
| **状态：…** | 只显示、不能点。可能是「启动中…」「运行中（ch149 / 20 MHz）」「后端退出，正在恢复…」「正在重启后端…」「启动失败，稍后自动重试」或「多次失败，已停止」。10 分钟内自动重启超过 5 次后就不再自动重试，这时要手动选「重新启动后端」 |
| **打开接收文件夹** | 在资源管理器里打开当前的接收目录 |
| **接收文件夹：<路径>…** | 弹出文件夹选择框，把收到的文件改存到别处，比如桌面或另一块硬盘。新设置写进 `data\settings.ini`，接收端随即在后台重启，不会重新初始化网卡。设置会先检查目录能否写入，写不了就不改；如果设置已保存、只是接收端重启失败，会弹出警告 |
| **打开日志** | 打开本次会话的日志目录 `data\logs\<会话>\`，里面有 radio、awdld、netd、receiver 等进程的日志 |
| **复制诊断信息** | 把程序路径、运行状态、系统版本，以及每个日志的最后 40 行复制到剪贴板，反馈问题时直接粘贴 |
| **开机自启** | 勾选后登录 Windows 时自动启动（写在当前用户的 HKCU Run 键下），取消勾选就删掉这一项。第一次成功启动时会自动打开并提示一次 |
| **安装驱动（首次使用）** | 把网卡的 MI_02 接口换成 WinUSB。会弹一次 UAC，导入驱动签名证书，再用 pnputil 安装。装好后自动重启后端；没找到网卡、没拿到管理员权限或安装失败时都会弹窗说明，安装过程记在 `data\logs\driver-install.txt`。程序启动时如果发现驱动还没装，会自动走这一步，这一项是给自动安装失败的情况重试用的 |
| **设备名称：<名字>…** | 修改 iPhone 隔空投送列表里显示的名字。长度 1–63 个字符，不能包含 `/ \ : * ? " < > \|`、控制字符或方向控制符。改完会立即通知接收端；接收端没在运行的话，名字先保存，下次启动时生效 |
| **清理通知注册信息** | 确认后删掉开始菜单里的 DropBridge 快捷方式和 `dropbridge://` 协议，删了以后通知上的「复制到剪贴板」按钮就不能用了。卸载前用这一项；只要再启动程序，就会重新注册 |
| **蓝牙 beacon（帮 iPhone 发现这台电脑）** | 默认开启。用电脑自带的蓝牙周期性发送 AirDrop 广播，让 iPhone 的 AWDL 保持唤醒；beacon 进程退出会自动重启。实测开着时，29 MB 视频能跑 1.09–1.10 MB/s，关掉只有 0.32–0.88 MB/s，而且手机可能要等打开分享面板才看得到这台电脑 |
| **为实况图生成单图副本** | 默认开启。收到实况照片（HEIC/JPG/PNG + MOV）或单张 HEIC 时，在同一目录多存一张 `<原名>-still.jpg`，方便在 Windows 上直接看。普通照片不会生成 |
| **重新启动后端** | 停掉整条链路（radio、awdld、netd、receiver）再重新拉起，网卡会重新初始化，约需 40–90 秒。手机看不到电脑或传输卡住时使用 |
| **重置链路** | 同样重启整条链路，另外还会清零低速检测计数。帧率连续偏低时托盘会弹出低速告警，用这一项重置 |
| **退出** | 先停 beacon 和文件监视，再按相反顺序停掉所有后端，然后退出。radio 会优雅释放网卡，不需要拔插 |

设备名称、接收文件夹、beacon 和实况图这四项的设置都存在 `data\settings.ini`，是纯文本，可以手动编辑，下次启动时生效。

收到文件时会弹出系统通知。图片的通知会显示缩略图，并带一个「复制到剪贴板」按钮，复制时位图和文件一起放进剪贴板。

## 工作原理

```text
iPhone ──AWDL 空口──▶ RTL8821CU (WinUSB)
  └▶ airbridge_radio   devourer 用户态驱动，libusb          GPL-2.0
  └▶ radio_pipe_bridge 命名管道 ↔ TCP                        MIT
  └▶ awdld             OWL 的 AWDL 控制面（选举/时隙/信道）  GPL-3.0
  └▶ awdl_netd         lwIP 用户态网络栈 + mDNS/TCP 代理     MIT
  └▶ receiver.py       AirDrop TLS/HTTP/DVZip 接收           MIT
  └▶ data\received
```

`DropBridge.exe`（C# WinForms，用系统自带的 .NET Framework 4.8）负责按顺序拉起各进程，并做健康检查、静默判死、低速告警、作业对象清理和通知。各组件分成独立进程运行，是为了让 GPL-2.0-only 和 GPL-3.0 的代码互不混合。

| 关键配置 | 值 | 原因 |
| --- | --- | --- |
| 信道 / 带宽 | 149 / 20 MHz | 实测 40 MHz 更慢，用久了还会退化 |
| 同步主控 | 由 Windows 担任（metric 200，counter 100000） | 主控被 iPhone 抢走后速度会掉到约 200 KB/s |
| 信道序列 | social：第 8 时隙用信道 6，其余用 149 | 这样 iPhone 加入后会立刻跟随 |
| 空口流数 | `-s 1` | 网卡是 1T1R，`-s 1` 和 `-s 2` 真机测试结果相同 |

## 从源码构建

需要 Windows 10/11 x64、Visual Studio（MSVC）和 PowerShell。在仓库根目录执行：

```powershell
.\tools\Setup-BuildDeps.ps1 -WithOpenSsl   # 一次性：vcpkg + libusb + OpenSSL，全部放在 build\deps
.\tools\Build-Devourer.ps1                 # devourer（只编 8821C）+ 无硬件自检
.\tools\Build-Radio.ps1
.\tools\Build-Awdl.ps1
.\tools\Build-Supervisor.ps1 -Test         # 托盘程序 + 单元测试
.\tools\Package-Distributable.ps1          # 输出 dist\DropBridge\ 和 portable zip
```

上游源码放在 `upstream/`，不纳入主仓库。按 [docs/sources.lock.json](docs/sources.lock.json) 里的 URL 和 commit 克隆，再执行 `tools\Apply-AwdlPatches.ps1` 应用 `patches/`。

常用的诊断工具：

```powershell
.\tools\Inspect-Hardware.ps1        # 只读：枚举 USB，识别网卡
.\tools\Check-Binding.ps1           # 只读：检查 MI_02 当前用的是哪个驱动
.\tools\Start-Chain.ps1             # 用控制台起整条链路；默认 dry-run，加 -Run 才会发射无线帧
.\tools\Collect-Session.ps1         # 冻结一次会话的日志和哈希
python .\tools\analyze_session.py --latest <包目录>   # 从日志算真实 MB/s、帧率和丢帧
python .\tools\check_project.py     # 检查文档、证据和版本锁是否一致
```

只读工具不会初始化网卡、换驱动或发射无线帧。会发射无线帧的操作需要用户授权，范围见 [AGENTS.md](AGENTS.md) 和 WORK_QUEUE 第 1 节。

## 发布前还缺什么

1. **Realtek 固件的再分发授权**：radio 静态链接了这份固件。对外发布前只能三选一：取得授权、改为运行时由用户自己提供、或者只发布源码。
2. **代码签名**：没有签名，SmartScreen 会弹警告。
3. **干净机器验收**（Q13）：在全新的 Windows 11 上，从解压到收到文件完整走一遍。
4. **稳定性**：Q51 第一次传完后找不到设备、多台手机同时在线时降速；还要跑 24 h 常驻测试（Q7）。
5. **WinUSB 回滚演练。**

另外，网卡同一复合设备上的蓝牙接口（MI_00）在 2026-10-28 以后一直是 Problem 31。AirDrop 不受影响，因为 beacon 走的是本机自带的蓝牙。

## 文档

- [docs/WORK_QUEUE.md](docs/WORK_QUEUE.md)：**进度以这里为准**，每一项都附有证据。
- [WORK_LOG.md](WORK_LOG.md)：完整的工作记录。
- [docs/FINAL_PLAN.md](docs/FINAL_PLAN.md)：最终方案和验收指标。
- [docs/PLAN.md](docs/PLAN.md)、[docs/PHASE0_AUDIT.md](docs/PHASE0_AUDIT.md)：阶段计划、源码审计和 Go / No-Go 结论。
- [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)：分发形态。
- [docs/BINDING_RUNBOOK.md](docs/BINDING_RUNBOOK.md)：驱动绑定和恢复。
- [docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md)：许可证核查。
- [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md)：构建细节。
- [docs/STATUS.md](docs/STATUS.md)：当前状态快照。
- [docs/SEND_PATH.md](docs/SEND_PATH.md)：发送方向的排查记录（结论：无法实现）。
- 排障记录：[STALL_DIAGNOSIS](docs/STALL_DIAGNOSIS.md)、[LWIP_INPUT_QUEUE](docs/LWIP_INPUT_QUEUE.md)、[TCP_DIAGNOSTICS](docs/TCP_DIAGNOSTICS.md)。
- 历史快照（可能已过时）：[HANDOFF.md](HANDOFF.md)、[docs/HANDOFF_2026-10-28.md](docs/HANDOFF_2026-10-28.md)。
- [AGENTS.md](AGENTS.md)：开发约定。

## 名称

项目最初暂定名为 AirBridge32，2026-10-30 起对用户显示的名字统一为 DropBridge（Q49）。内部组件（如 `airbridge_radio.exe`）的名字没有改，所以日志和进程列表里仍会看到 airbridge。

## 许可证

本仓库没有给第三方代码套用统一的许可证，各组件的许可证见上方工作原理里的进程图和 [docs/LICENSE_MATRIX.md](docs/LICENSE_MATRIX.md)。`upstream/` 里是原样保留的研究副本，不随本仓库发布。
