# 发布前核对清单

建立于 2026-11-10，对应提交 `44f9101` 之后的状态。**所有条目目前都未完成。**

用法：

- 每完成一项，把 `[ ]` 改成 `[x]`，在行尾写上日期和证据（日志文件、提交号、`evidence/` 下的路径）。没有证据不打勾。
- 证据要写清属于哪一层：本机编译、本机硬件测试、真实文件传输。三者不能互相替代（[AGENTS.md](../AGENTS.md)）。
- 标 **【需授权】** 的条目会初始化网卡、发射无线帧或改动驱动，动手前要先得到用户授权，并明确目标接口和恢复办法。
- 标 **【对外】** 的条目只在对外公开分发时才是硬性要求；只给自己或内部测试用的包可以暂缺，但要在发布说明里写明。
- 条目来源写在括号里，详细背景看 [WORK_QUEUE.md](WORK_QUEUE.md) 对应的行。

清单分七部分，建议按顺序做：前一部分没过，后面的结果不可信。

---

## 1. 源码能在 Windows 上编译并通过测试

Q68、Q69、Q70 三轮修复只提交了源码，没有编译过。当前代码与最后一次测过的包（Q50 / Q59–Q67）不同，所以这一部分是其余所有检查的前提。

- [ ] 按 [sources.lock.json](sources.lock.json) 恢复 `upstream/`，`python tools\check_project.py` 全部通过。
- [ ] `tools\Apply-AwdlPatches.ps1` 成功，三个补丁都能干净应用（Q70 改过 `patches/awdl-ipc-host.patch`；在 Linux 上只做过 `git apply --check`）。
- [ ] `tools\Build-Radio.ps1`：`airbridge_radio.exe`、`radio_pipe_bridge.exe` 编译通过，两项离线测试通过（Q69、Q70 改过两者）。
- [ ] `tools\Build-Awdl.ps1`：在**全新的构建目录**里编出 IPC 后端的 `awdld.exe`，`-Werror` 无告警，产物不依赖 Wintun（Q70）。
- [ ] `awdl_netd`：cmake 配置、编译通过；`ctest --test-dir build\awdl_netd -C Release` 全部通过，其中 `netd_ipc_io` 在 15 秒内结束（Q50 记录的超时，Q70 已改测试）。
- [ ] `tools\Build-Supervisor.ps1 -Test`：托盘程序用系统自带的 csc 编译通过，三项单元测试通过（Q68、Q69、Q70）。
- [ ] `tools\Build-Sfx.ps1`：单文件 stub 编译通过（Q70 改过 `src/sfx/Program.cs`）。
- [ ] 所有改过的 PowerShell 脚本至少被实际执行过一次且无语法错误：`Start-Chain.ps1`（dry-run 即可）、`Package-Distributable.ps1`、`Package-Headless.ps1`、`Build-Awdl.ps1`、`Build-Radio.ps1`、`Monitor-Soak.ps1`、`Collect-Session.ps1`、`toast.ps1`。这些在 Q70 里都没有运行过，连语法检查也没做。
- [ ] 在 `core.autocrlf=true` 的 Windows 新克隆里，`packaging\driver\airbridge-winusb.inf` 的 SHA-1 仍是 `d715e4ea3912d32b361b95624c3e9ef2396d9f9e`（Q70 的 `.gitattributes` 修复）。

## 2. 包的内容正确

- [ ] `tools\Package-Distributable.ps1` 成功，生成 `dist\DropBridge\` 和 `dist\DropBridge-portable.zip`。
- [ ] `dist\DropBridge\DropBridge.exe --check` 返回 0，输出里没有 `MISSING`。
- [ ] `VERSION.txt` 里的提交号等于要发布的提交，工作区干净（没有未提交改动混进包里）。
- [ ] 包里有 `backend\beacon.ps1`、`setup\driver\`（INF、CAT、CER 三个文件）、`setup\Bind-WinUSB.cmd`、`setup\zadig-2.9.exe`、`tools\toast.ps1`、`tools\register.ps1`。
- [ ] `licenses\` 里有六个文件且内容各不相同：`DropBridge-LICENSE.txt`、`DropBridge-NOTICE.txt`、`WinAWDL-LICENSE.txt`、`WinAWDL-airdrop-LICENSE.txt`、`devourer-LICENSE.txt`、`OWL-COPYING.txt`（2026-11-10 修复了同名覆盖，未运行验证）。
- [ ] 包里没有用户数据：没有 `data\`，没有开发机的密钥、日志、收到的文件。
- [ ] 包内 `README.txt` 与要发布的行为一致（见第 6 部分）。
- [ ] 如果也发布单文件版：`Package-Headless.ps1` + `Build-Sfx.ps1` 生成 `dist\DropBridge.exe`；解压后的 `app\<构建>\` 里同样有 `backend\beacon.ps1` 和 `setup\driver\`；信标能启动，`--install-driver` 能找到驱动包（Q70）。不发布单文件版就在发布说明里写明只发文件夹和 zip。

## 3. 在真机上能用 【需授权】

用第 2 部分打出来的包做，不要用开发目录里的构建产物。每项记下会话日志目录。

### 基本功能

- [ ] 冷启动：插上网卡，启动托盘，40–90 秒内状态变为「运行中」，六个进程都在。
- [ ] iPhone 能在隔空投送列表里看到这台电脑；发一张照片，落盘文件的哈希与原文件一致。
- [ ] 发一个约 29 MB 的视频，记录字节数和墙钟时间，速度不低于此前的基线（29,456,959 B / 26.84 s，Q46）。这一项同时确认管道桥改成重叠 I/O 之后没有退步（Q70）。
- [ ] 通知：收到图片弹出系统通知，「复制到剪贴板」可用；连续发 20 张以上照片时托盘菜单不卡（Q70）。
- [ ] 退出：点「退出」后所有进程消失，四个端口（49876、49877、8771、UDP 15353）释放，再次启动不需要拔插网卡。

### Q68–Q70 改过的行为

- [ ] 托盘内连续三次「重置链路」，radio 每次都能重新起来（Q69 停止事件复位）。
- [ ] 网卡还在初始化时点「退出」和「重置链路」各一次：进程能退出，之后不需要拔插网卡。已知限制：停止信号正好落在 `InitWrite` 期间时仍会在 15 秒后强杀（Q70 遗留），把实际观察到的结果记下来。
- [ ] 启动中途退出，没有残留进程（Q69）。
- [ ] 改设备名：改成含空格的名字、以 `-` 开头的名字各一次，iPhone 上显示新名字且仍能发现（Q68、Q69、Q70）。
- [ ] 改接收文件夹：设为 `D:\` 这样的盘符根目录、含空格的路径各收一次文件，通知上的复制按钮可用；重选当前文件夹时接收端不重启（Q68、Q70）。
- [ ] 接收文件夹所在的盘不在时启动：本次改存 `data\received` 并有提示，`settings.ini` 里的设置保留（Q70）。
- [ ] 故意让一个后端文件缺失（例如临时改名 `awdld.exe`）后启动：有错误气泡，并按 1 s / 5 s / 30 s 重试，超过 5 次后显示「多次失败，已停止」（Q70）。
- [ ] 蓝牙关闭时启动：信标反复退出后改为每 10 分钟重试，日志不被刷屏；重新打开蓝牙后信标自己恢复（Q69、Q70）。
- [ ] 托盘运行时执行 `DropBridge.exe --smoke 30`：直接失败退出，托盘的后端不受影响（Q70）。
- [ ] 大文件只在写完后通知一次（Q68）。
- [ ] 数据目录位于非 ANSI 路径下（例如中文用户名）时能正常启动（Q70 地址文件改宽字符）。
- [ ] `tools\Start-Chain.ps1 -Run` 能把整条链路拉起来；Ctrl+C 后 radio 日志里是正常收尾，网卡不需要拔插；安装路径含空格时同样可用（Q70）。

### 稳定性

- [ ] 常驻 24 小时，`tools\Monitor-Soak.ps1` 的采样里内存和句柄数没有持续上涨，期间至少完成三次传输（Q7）。
- [ ] radio 假死注入后能自动恢复（Q7、Q57）。
- [ ] 「传完一次后手机连不上」（Q51）：要么修复并验证，要么确认「重新启动后端」能恢复，并写进已知问题。
- [ ] receiver 对 `/Discover` 分块报文解析过严（Q43）：修复，或确认不影响发现和重连后写进已知问题。

## 4. 在干净的机器上能从零装好 【需授权】

开发机的网卡早就绑定了 WinUSB，「未绑定 → 已绑定」这条路径从来没有在真机上走过（Q53）。

- [ ] 准备一台没装过 DropBridge、网卡仍是 Realtek 原驱动的 Windows 11 x64（Q13）。有条件再加一台 Windows 10：README 写了支持 Windows 10，但现有测试记录全部来自 Windows 11。
- [ ] 解压 zip，双击 `DropBridge.exe`：出现确认弹窗 → UAC → 证书导入 → `pnputil` 安装成功 → 自动继续启动，`data\logs\driver-install.txt` 有完整记录。只动了 MI_02，MI_00 和 MI_01 没变。
- [ ] 不装任何额外运行库就能启动（.NET Framework 4.8、VC++ 运行库、Python 都靠系统自带或包内文件）。
- [ ] 第一次成功启动后「开机自启」被打开并提示一次；重启电脑后托盘自动出现。
- [ ] 完成一次真实传输，哈希一致。
- [ ] 自动安装失败时的退路可用：托盘「安装驱动（首次使用）」，以及 `setup\Bind-WinUSB.cmd` + Zadig 手动绑定。
- [ ] 卸载演练：取消自启 → 「清理通知注册信息」→ 退出 → 删除文件夹。之后 HKCU Run 项、开始菜单快捷方式、`dropbridge://` 协议都不在了。
- [ ] 驱动回滚演练：按 [BINDING_RUNBOOK.md](BINDING_RUNBOOK.md) 把网卡换回 Realtek 驱动，Wi-Fi 功能恢复。同时确认导入的自签证书怎么移除，并把步骤写进文档（目前卸载说明里没有这一步）。
- [ ] 升级演练：用新包覆盖旧包并保留 `data\`，设置、密钥、AirDrop 实例 ID 都还在，手机列表里没有多出一个 DropBridge。单文件版另外确认自启项仍指向 stub，且自启开关状态保持（Q69）。

## 5. 许可证与合规

本仓库自写代码是 MIT（[LICENSE](../LICENSE)、[NOTICE](../NOTICE)），但包里有 GPL 组件和第三方文件。逐项背景见 [LICENSE_MATRIX.md](LICENSE_MATRIX.md) §3–§7。

- [ ] 【对外】**Realtek 固件的再分发**：三选一并落实——取得授权；改为运行时由用户提供固件；只发布源码和构建脚本。这一项不解决，不能对外发布二进制包。
- [ ] 【对外】准备 GPL 对应源码的提供方式：`src/radio_usb` + devourer + 补丁 + 构建脚本（GPL-2.0）；WinAWDL `awdl/`、OWL + 补丁（GPL-3.0-or-later）；Zadig 的书面要约已在 `setup\licenses\ZADIG.txt`。
- [ ] 包内附 libusb 的 LGPL 文本，并确认是动态链接（`backend\libusb-1.0.dll`）。
- [ ] 包内附 lwIP 的版权声明和 BSD 条款。
- [ ] 核对内嵌 Python、cryptography（及其内含的 OpenSSL）、cffi、pycparser 的许可证，确认包内带全所需文本。目前矩阵里这几项都标着「未在本仓库内核对」。
- [ ] 确认 VC++ 运行库（从构建机 `System32` 复制的三个 DLL）的再分发方式合规。
- [ ] 在实际使用的提交上重新核对上游许可证文件：devourer `34a9e6f`、OWL 子模块 `da255a7`（Phase 0 核对的是别的提交）。
- [ ] 确认 `src/beacon/airdrop-beacon.ps1` 的来源和许可证（记录为「上游原样」，本机无法核对；文件头注释已有本仓库改动）。
- [ ] 【对外】发布包把 GPL 与 MIT 组件放在一起是否算单纯聚合，需要法律确认；发布说明里不得把整个包称为 MIT。
- [ ] 【对外】名称与商标：DropBridge 的说明文字不暗示 Apple 授权或官方兼容。
- [ ] 版权人署名确认：`LICENSE` 里暂写「DropBridge contributors」。

## 6. 版本可复现，文档与实际一致

- [ ] 锁文件与实际构建一致。目前不一致：Q50 完整构建用的 devourer 是 `f7e0731`，锁文件是 `34a9e6f`；OWL 锁的是 `0ee3263`，实际构建用子模块 `da255a7`。用锁定的版本重新构建并测试，或把锁文件改成实际使用的版本。
- [ ] 手动准备的依赖有可复现的来源：lwIP / lwip-contrib（Q50 用 `77dcd25`）、Python embeddable（3.12.10）、cryptography（50.0.2）及其依赖。写进锁文件需要先改 `tools/check_project.py`（它断言正好 5 个条目）；至少在发布说明里记下本次实际使用的版本和下载文件的哈希。
- [ ] Q70 里几项对用户可见的策略由用户确认或调整：信标每 10 分钟重试、重启计数满 10 分钟稳定运行才清零、接收端单独重启 10 分钟内最多 3 次、低于 100 帧/秒不计入低速告警。调整后同步 README 和包内 README。
- [ ] [README.md](../README.md)、`packaging/README.txt`、`packaging/setup/README.txt` 的每一步在干净机器上照做可行（第 4 部分顺带核对）。
- [ ] README 的「状态」框、验证层级表、实测速度表更新为本次发布的实测结果，去掉「Q68–Q70 尚未编译和测试」。
- [ ] [STATUS.md](STATUS.md) 更新到发布提交。
- [ ] 遗留项有明确去向：`tools/Run-AwdlAdmin.ps1` 删除或保留；radio accept 失败路径的 detach 隐患（Q70 遗留）修复或记为已知问题；「每次启动用唯一停止事件名」做或不做。
- [ ] 已知问题列表写进发布说明：只能接收（Q48）、多台 iPhone 同时在线时降速（Q51），以及第 3 部分里选择「记为已知问题」的各项。

## 7. 发布动作

- [ ] 【对外】代码签名：`DropBridge.exe` 和各后端 exe 没有签名，SmartScreen 会警告；驱动包用的是自签证书。决定是否购买证书签名；不签就在发布说明里写明预期的警告和用户需要做的操作。
- [ ] 所有改动已提交，[WORK_QUEUE.md](WORK_QUEUE.md) 里 Q68、Q69、Q70 的「证据待补」都已补上。
- [ ] 打版本标签；发布说明写明：提交号、zip 的 SHA-256、支持的硬件（USB ID `0bda:c820`）和系统、已知问题、许可证说明、源码获取方式。
- [ ] 在另一台机器上下载发布的 zip，核对 SHA-256，解压后 `DropBridge.exe --check` 通过。
- [ ] 推送远程和公开发布由用户本人执行或明确授权（[AGENTS.md](../AGENTS.md)：不自动推送）。
