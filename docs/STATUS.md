# 当前状态（2026-11-10，截至 Q70）

> 这份文档是给接手的人看的，回答四个问题：现在是什么样、能做什么、卡在哪里、怎么复现。
> 进度和证据以 [WORK_QUEUE.md](WORK_QUEUE.md) 为准，过程记录见 [WORK_LOG.md](../WORK_LOG.md)，为什么这样设计见 [FINAL_PLAN.md](FINAL_PLAN.md)。
> 本文只摘录要点。遇到冲突时，以队列里的证据为准。

## 1. 一句话

DropBridge 是 Windows 上的 AirDrop 接收端，交付形态是一个文件夹加一个托盘程序。iPhone 用系统自带的隔空投送，通过专用的 RTL8821CU USB 网卡把文件传到电脑上。**iPhone → Windows 已在真机上稳定达到约 1.1 MB/s**（29,456,959 B 视频 26.84 s / 26.89 s，Q46 / Q54），最好成绩 1121 KB/s。还没解决的有两件事：传完一次以后有时发现不了设备，以及多台手机同时在线时降速。Windows → iPhone 发送已确认无法实现，本项目只做接收。

每项结论的验证层级不同，下表分开列出，彼此不能替代：

| 层级 | 现状 |
| --- | --- |
| 源码实现 | 接收链路、托盘监管、设置、通知、自动装驱动、beacon 均已完成；Q68、Q69、Q70 三轮审查修复只有源码改动 |
| 本机编译 | 最近一次完整编译和打包是 Q50（2026-10-30）及 Q59–Q67 的托盘重编。**Q68、Q69、Q70 都没有编译、没有运行测试**（Q69、Q70 在没有 Windows 工具链的 Linux 环境里完成），所以当前 HEAD 与最后一次测过的包并不相同 |
| 本机硬件 | 网卡初始化、RX/TX、起停与楔死修复（Q55/Q57/Q58）已在真机验证；Q68–Q70 的改动没有上过真机 |
| 真实文件传输 | Q45（1.2 MB 图片 1.23 s）、Q46（29 MB 1.0975 MB/s）、Q54（1.10 / 1.09 MB/s）的落盘哈希都与源文件一致 |

## 2. 交付形态

```text
DropBridge\                唯一形态：一个文件夹，外加同样内容的压缩包 DropBridge-portable.zip
  DropBridge.exe           托盘监管程序（C# WinForms，用系统自带的 .NET Framework 4.8）
  backend\                 radio / bridge / awdld / awdl_netd / python / receiver / beacon.ps1
  setup\                   driver\：自建 WinUSB 驱动包（自签证书 + .cat），首次运行时自动安装
                           备用的手动绑定方案：Bind-WinUSB.cmd + Zadig 2.9（GPLv3）
  tools\                   toast.ps1 / register.ps1 / Start-Chain.ps1
  licenses\  README.txt  VERSION.txt
  data\                    首次运行时生成
    settings.ini           deviceName / receivedFolder / beacon / stillCopy
    keys\                  TLS 自签证书、airdrop-service-id（保证重启后实例 ID 不变）
    received\              默认的接收目录
    logs\<会话>\           各进程日志、driver-install.txt
```

- 程序数据都在 `data\` 里。写到系统中的只有这几样：开机自启项（HKCU Run）、通知按钮需要的开始菜单快捷方式、`dropbridge://` 协议和 AUMID（HKCU\Software\Classes），以及首次运行时装的 WinUSB 驱动包和签名证书（Root / TrustedPublisher，需要一次 UAC）。
- 升级时覆盖 `DropBridge.exe` 和 `backend\`，保留 `data\`。卸载步骤：关掉开机自启，点「清理通知注册信息」，退出后删除文件夹；驱动回滚见 [BINDING_RUNBOOK.md](BINDING_RUNBOOK.md)。
- 用户可见的名字从 Q49 起统一为 DropBridge，内部组件仍叫 airbridge_*。

分发细节见 [DISTRIBUTION.md](DISTRIBUTION.md)，用户说明是 `packaging/README.txt`。

## 3. 进程与数据路径

| 进程 | 许可证 | 职责 |
| --- | --- | --- |
| `DropBridge.exe` | MIT（本仓库自写源码，2026-11-10 定） | 单实例；按顺序拉起后端；看门狗（进程死亡、radio 静默超过 30 s 判死）；低速告警；作业对象清理；设置；通知；开机自启；装驱动 |
| `airbridge_radio.exe` | 自写源码 MIT；二进制因链接 devourer 整体按 GPL-2.0 分发 | devourer + libusb 驱动 RTL8821CU，独占 USB，`radio_link` 端口 TCP 49876 |
| `radio_pipe_bridge.exe` | MIT（本仓库自写源码） | 命名管道 ↔ radio 的 TCP |
| `awdld.exe` | GPL-3.0-or-later | OWL 的 AWDL 控制面：选举、时隙、信道序列 |
| `awdl_netd.exe` | 自写源码 MIT；编译进去的 lwIP 为 BSD-3-Clause（未在本仓库内核对） | lwIP 用户态网络栈、mDNS 代理、TCP 8771 代理 |
| `python.exe` + `receiver.py` | `receiver.py` 等来自 WinAWDL `airdrop/`，MIT；Python 本体和 cryptography 见 LICENSE_MATRIX | AirDrop 的 TLS/HTTP/DVZip 接收，通过 stdin 接收改名、改文件夹的指令 |
| `beacon.ps1` | MIT（上游 WinAWDL `scripts/`；2026-11-10 起文件头注释有本仓库改动） | 通过本机自带的蓝牙发 AirDrop BLE 广播 |

数据路径：iPhone → AWDL 空口 → radio → bridge → awdld → awdl_netd（lwIP）→ receiver → 接收目录。GPL-2.0-only 和 GPL-3.0 的组件分在不同进程里运行，互不混合。许可证依据和发布前缺口见 [LICENSE_MATRIX.md](LICENSE_MATRIX.md)。

## 4. 关键配置（由监管程序固定）

| 项目 | 值 | 原因 |
| --- | --- | --- |
| 信道 / 带宽 | 149 / 20 MHz | 40 MHz 实测只有 172–190 KB/s，用久了还会退化，已弃用 |
| 同步主控 | `AWDL_ELECTION_METRIC=200`、`AWDL_ELECTION_COUNTER=100000` | 主控被 iPhone 抢走后速度会掉到约 200 KB/s |
| 信道序列 | `social`：第 8 时隙用信道 6，其余用 149 | iPhone 加入后会立即跟随 |
| TCP 合并 | 开启（`join_us`） | 和 social 序列搭配最快（Q41）；在 fixed 模式下开启反而有害（Q39/Q40） |
| 空口流数 | `-s 1` | 网卡是 1T1R；真机 A/B 中 `-s 1` 与 `-s 2` 差 0.9%，属于噪声（Q54） |
| lwIP 输入队列 | `MEMP_NUM_TCPIP_MSG_INPKT=64`，满了等待重投，不丢帧 | Q31 / Q38：8 槽时会丢帧，速度塌到 0.38 MB/s，改后升到 0.85 MB/s |
| devourer | 升级到 `34a9e6f`，固件就绪轮询放宽到 1 s | Q50 / Q51：0.32 → 0.81 MB/s，并消除 78 s 的固件重下 |
| 换台 | 固件快切 H2C 0x1D | 跨频段换台从 65–90 ms 降到 0.22–0.28 ms |

## 5. 速度里程碑（同一个 29,456,959 B 视频）

| 时间 | 配置 | 结果 | 出处 |
| --- | --- | --- | --- |
| 10-28 | 从属、未修正选举 | 202 KB/s | Q15 |
| 10-28 | 当同步主控 + 修正 counter | **1121 KB/s** | Q15 |
| 10-28 | 关闭合并（旧 netd） | 114 / 104 KB/s | Q27 |
| 10-29 | Q31 netd（8→64 槽） | 0.3836 MB/s | Q37 |
| 10-29 | Q38 背压不丢帧 | 0.8518 MB/s | Q38 |
| 10-29 | 默认 social + 合并 | 0.9231 MB/s | Q41 |
| 10-29 | 成品文件夹 | 1.0975 MB/s | Q46 |
| 10-31 | devourer 34a9e6f，旧版对照 | 0.32 → 0.81 MB/s | Q50 |
| 10-31 | `-s 2` / `-s 1`（beacon 开） | 1.10 / 1.09 MB/s | Q54 |

所有速度都是落盘字节数除以墙钟时间。

## 6. 托盘功能（Q53–Q70）

- **首次使用一键完成**（Q53）：自动安装 WinUSB 驱动，首次运行默认打开开机自启。
- **蓝牙 beacon**（Q56）：带看门狗，菜单里可以开关。
- **故障自愈**：radio 静默超过 30 s 判死并整链重启，3 分钟内第二次触发时提示拔插（Q57）；强杀父进程不会残留端口（Q55）；停止时不会楔死网卡（Q58）。
- **明暗两版图标**（Q59）：按真实 DPI 取图标帧。
- **设置持久化**（Q60）：`data\settings.ini`。改设备名、改接收文件夹立即生效（Q65）；这两项此前从未真正生效，Q66 修复。
- **通知**：系统 toast 带「复制到剪贴板」按钮，靠 `dropbridge://` 协议回调（Q62）；自绘弹窗已弃用（Q63）。
- **实况图**：自动生成 `-still.jpg` 副本（Q64）。
- **误报修复**（Q67）：上传临时文件不再触发「收到文件」通知。
- **Q68 审查修复（仅源码）**：启动参数按 CommandLineToArgvW 规则转义、重启 receiver 前先停掉旧进程、文件写完再通知。
- **Q69 审查修复（仅源码，未编译）**：radio 停止事件在每次启动前复位（原先托盘内任何重启都会让新 radio 立刻退出）；netd 的 mDNS 转发线程不再因 receiver 重启后的 `WSAECONNRESET` 永久退出；启动中途退出不再与启动并发修改子进程列表；改名、改文件夹期间看门狗不再误判 receiver 崩溃；单 exe 布局的自启标记和驱动日志改写到数据目录；beacon 快速退出时指数退避（Q70 又改成下一条的做法）；出站代理套接字加收发超时；netd 从 EUI-64 取 MAC 的字节位置更正（只按 RFC 4291 推断，未抓包确认）。
- **Q70 审查修复（仅源码，未编译）**：44 条审查问题中修 40 条、部分修 2 条、1 条不成立、1 条未实现（明细见 WORK_QUEUE Q70）。行为上的变化：
  - 自动重启间隔 1 s、5 s、30 s；连续失败超过 5 次、其间没有稳定运行满 10 分钟就停止自动重试，状态显示「多次失败，已停止」，要手动「重新启动后端」；
  - receiver 单独重启 10 分钟内超过 3 次时改为整链重启；
  - beacon 在启动后 1 分钟内连续退出 5 次后，改为每 10 分钟重试一次（不再永久停止）；
  - 启动时保存的接收文件夹不存在时，本次改存默认的 `data\received` 并提示，设置本身保留；
  - 链路速率告警按样本去重并加 100 帧/秒下限；
  - 打包与工具：单文件包补上 `beacon.ps1`、`setup\`、`setup\driver\`；驱动 INF 标 `-text`，`.cat`、`.cer` 标 binary；`Build-Awdl.ps1` 传 `-DAWDL_HOST_IPC=ON` 并去掉 Wintun；
  - netd 主线程的 lwIP 调用加核心锁；管道桥改重叠 I/O；radio 停止句柄提前到初始化之前。
  - 上面几条策略（10 分钟 beacon 重试、以「稳定运行 10 分钟」清零的重启限流、receiver 10 分钟 3 次、100 帧/秒阈值）还等用户确认。

## 7. 未解决

1. **Q51 发现丢失和多机降速**：一次传输成功后，手机再发起的 TCP/TLS 连接会反复失败，重启链路能恢复；两台手机同时在线时降到 0.08–0.12 MB/s，慢传窗口里 CRC 错误占约 67–70%。根因尚未查清。
2. **Q48 Windows → iPhone 发送：已确认无法实现，终止**（用户 2026-11-04 确认）。发现、出站 TCP 和 TLS 握手都已走通，但 iPhone 用 TLS alert 拒绝了本项目的自签证书。
3. **Q68–Q70 待验证**：全部只有源码。需要在 Windows 上运行 `Build-Supervisor.ps1 -Test`、`Build-Sfx.ps1`，用 MSVC 编译 netd/radio/bridge、MinGW 编译 awdld，跑 `ctest -R netd_`；在 `D:\`、含空格的路径、含空格的设备名下各收一次文件；托盘内多次「重置链路」确认 radio 能重新起来；启动中途退出确认没有残留进程；无蓝牙机器上看 beacon 的退避与 10 分钟重试；真机发现与传输确认管道桥的重叠 I/O 没有回退。完整清单见 [WORK_QUEUE.md](WORK_QUEUE.md) 的 Q68、Q69、Q70 各行。
   Q70 记下的遗留：停止信号在 `InitWrite` 期间到达仍会等到 15 秒后强杀；radio accept 失败路径仍可能 detach；每次启动用唯一停止事件名没有实现；`tools/Run-AwdlAdmin.ps1` 因不再拷贝 Wintun 而必然失败，不属于当前链路，是否删除待定。
4. **Q43**：receiver 解析 `/Discover` 的分块格式过严，已有 WARNING 但还没修。
5. **Q7**：radio 假死检测已由 Q57 实现，24 h 常驻测试还没跑完。
6. **发布前**：Realtek 固件再分发授权（三选一）、代码签名、干净 VM 验收（Q13）、WinUSB 回滚演练、真正从「未绑定」到「已绑定」的转换（Q53 未验证）。
7. **网卡蓝牙接口 MI_00 一直是 Problem 31**：重启和重新插拔都没能恢复。不影响 AirDrop，因为 beacon 用的是本机的 Intel 蓝牙。

10-28 时曾判断「射频状态退化，需要冷复位」，但后续证据没有证实这一点（见 [STALL_DIAGNOSIS.md](STALL_DIAGNOSIS.md)），已不作为当前结论。

## 8. 怎么跑、怎么验

```powershell
# 构建（一次性依赖见 docs/BUILD_WINDOWS.md）
.\tools\Setup-BuildDeps.ps1 -WithOpenSsl
.\tools\Build-Devourer.ps1; .\tools\Build-Radio.ps1; .\tools\Build-Awdl.ps1
cmake -S src\awdl_netd -B build\awdl_netd -A x64; cmake --build build\awdl_netd --config Release   # netd 没有构建脚本
.\tools\Build-Supervisor.ps1 -Test
.\tools\Package-Distributable.ps1        # 输出 dist\DropBridge\ 和 portable zip

# 不启动链路的自检
dist\DropBridge\DropBridge.exe --check   # 路径解析，应输出 exit 0、无 MISSING
.\tools\Check-Binding.ps1                # 只读检查 MI_02 当前驱动
python .\tools\check_project.py          # 检查文档、证据、版本锁（需要 upstream/ 已恢复）

# 一次真实传输后
.\tools\Collect-Session.ps1              # 冻结日志和哈希
python .\tools\analyze_session.py --latest dist\DropBridge   # MB/s、帧率、netd 丢帧
```

发射无线帧要在授权范围内进行（见 WORK_QUEUE 第 1 节）。换驱动、改网络或改防火墙仍需单独授权。

## 9. 证据索引（节选）

- `evidence/q15-master-mode.log`、`evidence/transfer-970kbs.log`：主控和 1121 KB/s
- `evidence/lwip-input-queue-2026-10-28.log`：Q31 输入队列
- `evidence/q38-phone-fixed-*.log`：背压不丢帧和合并 A/B（Q38–Q40）
- `evidence/discovery-recovery-2026-10-30.log`、`evidence/two-phones-16pm-2026-10-30.log`：Q51
- `evidence/inapp-driver-install-2026-10-31.log`：Q53
- `evidence/beacon-in-tray-2026-10-31.log`：Q56
- `evidence/settings-device-name-2026-11-01.log`、`evidence/toast-button-2026-11-01.log`：Q60、Q62
- `evidence/still-copy-2026-11-02.log`、`evidence/received-folder-2026-11-02.log`、`evidence/temp-file-noise-2026-11-02.log`：Q64、Q65/Q66、Q67
- `evidence/bench-receiver.md`：receiver 离线测得 228 MB/s，Python 不是瓶颈
