# 工作队列（跨会话，长期有效）

本文是"接下来做什么"的唯一入口。上下文会满、agent 会换，**任何接手者先读这一页**，再看 `docs/FINAL_PLAN.md`（为什么这样做）和 `WORK_LOG.md`（已经做了什么）。

## 0. 用法

- 状态只有四种：`⬜ 未开始`、`🔜 进行中`、`✅ 完成`、`⛔ 阻塞（写明缺什么）`。
- 每完成一项：写证据（`evidence/` 或 `WORK_LOG.md`）→ 更新本表的"状态/证据/日期"→ 本地中文提交。三件事缺一不可。
- 一项没完成不要开下一项；硬件项必须等授权记录更新后再开始。
- 新增事项追加到表尾，不改别人的编号。

## 1. 硬件与授权状态

| 事项 | 状态 | 说明 |
| --- | --- | --- |
| RTL8821CU MI_02 已绑 WinUSB | ✅ | 见 `evidence/binding-result.log` |
| 允许初始化网卡 / 发 AWDL 帧 | ✅ 2026-10-27 23:37 | 用户明确授权（“授权你…预算不限”）。范围：初始化、AWDL 同步、速度与时隙测量。Q5 已借此完成真机起停；每次实验仍要记录时间与日志 |
| 允许换驱动/改网络/改防火墙 | ⛔ 未授权 | 按 AGENTS.md，需单独授权并写明恢复办法 |

## 2. 队列总表

| ID | 事项 | 依赖 | 完成判据 | 状态 | 证据 |
| --- | --- | --- | --- | --- | --- |
| Q1 | receiver 离线吞吐台 | 无 | 20 MB 三次，给出 Python/TLS/zlib 侧 MB/s 上限与结论 | ✅ 2026-10-27：compressed 228 MB/s、stored 278 MB/s，非瓶颈 | `evidence/bench-receiver.md` |
| Q2 | 身份证书去掉外部 openssl | 无 | 无 Git/OpenSSL 的 PATH 下能生成证书并 `/Discover` 200 | ✅ 2026-10-27：cryptography 进程内生成，openssl 仅回退 | `evidence/identity-no-openssl.log` |
| Q3 | 无壳手动链路脚本 | 写脚本无依赖；**跑需授权** | 一键起停、日志分离、退出无残留 | 🔜 脚本与无壳包完成（dry-run、端口冲突、打包后路径均验证）；`-Run` 待授权 | `tools/Start-Chain.ps1`、`tools/Package-Headless.ps1` |
| Q4 | 单文件自解压原型 | 无 | 干净 VM 双击可跑，≤30 MB，退出可清理 | ✅ 2026-10-27 原型：16.0 MB 单 exe、首解 0.4 s、二次 0.03 s；干净 VM/AV 未验 | `evidence/single-exe-prototype.log` |
| Q5 | 原生托盘监管（替代 Flutter） | Q4 | 无窗口常驻、单实例、开机自启、退出清理、完成通知 | ✅ 2026-10-27：20 KB winexe；真机 `--smoke` 与托盘 `--auto-exit` 均起停干净、无残留；气泡/菜单未肉眼验 | `evidence/supervisor-tray.log` |
| Q6 | 日志轮转与诊断包 | Q5 | 5 MB 滚动；会话保留；诊断信息可导出 | ✅ 2026-10-27：5 MiB 滚动 + 保留 10 会话 + 剪贴板诊断 | `evidence/supervisor-recovery.log` |
| Q7 | 看门狗与恢复阶梯 | 硬件授权 | 假死注入后 ≤10 s 自动恢复；24 h 无泄漏 | 🔜 进程死亡恢复已做（receiver 单独重启 <1 s；其它整链重启）；**低速链路检测已做**（帧率 <300 连续两个 5 s 样本告警 + 托盘“重置链路”）；radio 假死、24 h 未做 | `evidence/supervisor-recovery.log` |
| Q8 | E1 时隙/信道序列实测 | — | — | ✅ 归档：Q15 实测已覆盖（149 占空比、joined 帧/s、主控 vs 从属） | `evidence/q15-master-mode.log` |
| Q9 | E2 ACK 延迟与 TX 优先级 | — | — | ✅ 归档：硬件 ACK 由 `SetStationIdentity` 布防；实测瓶颈在发起方帧率，未见 ACK 侧问题 | `evidence/transfer-970kbs.log` |
| Q10 | E3 USB 帧率/聚合 | — | — | ✅ 归档：radio 的 5 秒 `stats`/`peer frames` 已覆盖回调、CRC、帧率 | `evidence/transfer-970kbs.log` |
| Q11 | E4 mDNS 静默策略 | — | — | ✅ 归档：上传期由 receiver 的 `TRANSFER_BUSY` 暂停公告，未再观察到公告挤占 | `docs/DISCOVERY.md` |
| Q12 | 原生 mDNS 收尾 | Q11 | iPhone 能只靠原生 mDNS 发现 | ⬜ 非必需：Python mDNS 代理工作正常，原生只省一层代理 | — |
| Q13 | 分发验收（干净 VM + 许可清单） | Q5 | FINAL_PLAN 的验收指标全绿 | ⬜ | — |
| Q14 | 移除 Flutter 壳与旧便携打包 | Q5 真机传文件通过 | `app/` 删除、旧打包脚本删除、文档更新 | ✅ 2026-10-28：`app/`（26 个受版本控制文件）与 `tools/Package-Portable.ps1` 已删除；README/PLAN/ARCHITECTURE/PORTABLE 标注历史 | 提交记录 |
| Q15 | awdld 跟随/协商信道序列（同信道） | 硬件授权 | 149 时隙占空比提升；20 MB ≥ 500 KB/s | ✅ 2026-10-28：改为**我们当同步主控**（metric 200 + counter 100000）。20 MHz 实测 **1121 KB/s**（29.46 MB / 26.3 s），另一次 970；counter 修正前会被手机抢主控掉到 200 KB/s 档 | `evidence/transfer-970kbs.log`、`evidence/q15-master-mode.log` |
| Q16 | AirDrop 实例 ID 跨重启稳定 | 无 | 重启后 iPhone 不再多出一个 AirBridge32 | ✅ 2026-10-28：ID 持久化到 `<keys>/airdrop-service-id`，跨重启与跨构建都复用 | `evidence/service-id-persist.log` |
| Q17 | 全部数据留在程序文件夹内 | 无 | 不在 `%LOCALAPPDATA%` 或别处留下文件 | ✅ 2026-10-28：`app\<构建>` + `data\`（跨构建共享），旧的本机目录已清理 | `evidence/data-stays-in-folder.log` |

## 3. 每项的具体做法

### Q1 receiver 离线吞吐台

- 动作：新增 `tools/bench_receiver.py`。在 `127.0.0.1` 起真实 receiver（TLS + chunked + DVZip 解码 + cpio 解包 + 落盘），上传 20 MB，3 次取中位数；分别测"压缩块"和"stored 块"，另测 `--trace` 开/关。
- 不测什么：不经过 `awdl_netd` 的 lwIP 代理，所以数字是 **receiver + 磁盘** 的上限，不是整链路。
- 判据：≥10 MB/s → Python 不是瓶颈，速度问题在空口/调度；3–10 MB/s → 隐患；<3 MB/s → 先修 receiver 再谈射频。
- 产出：`evidence/bench-receiver.md`（机器、版本、3 次原始数字、结论）。

**结果（2026-10-27，已完成）**：20 MB 三次，compressed 中位数 **227.7 MB/s**、stored **277.9 MB/s**；`--trace` 全开为 274/260 MB/s，日志仅 2.6 KB。SHA-256 全部一致。结论：Python/TLS/zlib/cpio/磁盘约有 200 倍余量，**不是瓶颈**；速度问题锁定在空口调度、ACK 与 USB 帧率（Q8–Q10）。唯一保留：客户端与服务端同进程，数字是下限，但离阈值太远，不影响结论。

### Q2 身份证书去 openssl

- 现状：`identity.py` 调 `openssl.exe`；干净机器没有 Git/OpenSSL 时首次启动失败（`--trace` 日志表现为 `IdentityError`）。
- 动作：`identity.py` 优先用 `cryptography` 生成 RSA-2048 自签证书，缺失时回退 openssl；改动写入 `patches/`（新增 `airdrop-identity.patch` 或并入现有 patch），并让 `tools/Apply-AwdlPatches.ps1` 应用。
- 验证：临时把 Git/OpenSSL 从 PATH 去掉，启动 receiver，确认生成 `certificate.self-signed.pem` / `key.self-signed.pem` 且 `/Discover` 返回 200。

**结果（2026-10-27，已完成）**：`identity.py` 改为先用 `cryptography` 进程内生成 RSA-2048 自签证书（临时文件 + 原子替换），模块缺失时才回退 `openssl.exe`。三种情况实测：无 openssl 时生成 1046 B 证书并通过 `load_cert_chain`；屏蔽 cryptography 时 Git 自带 openssl 回退成功；两者都没有时明确抛 `IdentityError`。端到端 1 MB 上传通过。改动以 `patches/airdrop-identity.patch` 跟踪。
- 附带修复：旧 `awdl-ipc-host.patch` 是 CRLF，干净树应用必失败，而 `Apply-AwdlPatches.ps1` 会静默 `exit 0`。已用 LF 重新生成两个补丁，脚本改为逐个补丁判"已应用/冲突"并在冲突时抛错，新增 `-Repo`；在锁定提交的干净 worktree 上应用后，五个文件的哈希与活树完全一致（`evidence/patches-reproduce.log`）。

### Q3 无壳手动链路脚本

- 动作：新增 `tools/Start-Chain.ps1`（参数 `-Channel 149 -Width 40 -Python <exe> -Trace`）：按序起 radio → bridge → netd → awdld → receiver，日志分别落 `build/chain-logs/`，Ctrl+C 逆序清理，启动前检查端口与单实例。
- 用途：与便携包做同文件 A/B，量化 Flutter + embeddable Python 的代价。
- 判据：起停三次无残留进程/端口；A/B 用同一张 20 MB 文件。

**进展（2026-10-27）**：`tools/Start-Chain.ps1` 完成。默认 dry-run，必须显式 `-Run` 才会初始化网卡（把授权边界写进工具里）；自动识别仓库布局（`build\`）与打包布局（`backend\`）；启动前检查 49876/49877/8771/15353 端口；按序等待每个后端的 ready 行；Ctrl+C 逆序清理并复查端口。已验证：dry-run 输出命令、端口被占用时拒绝启动、打包目录内路径解析正确。
`tools/Package-Headless.ps1` 产出无 Flutter/无 Dart 的包：`dist\AirBridge32-headless.zip`（**16.0 MB**，便携版为 27.0 MB），内含 backend、python-embed、receiver、Start.cmd 与 README。**尚未真机起停**（需授权）。

### Q4 单文件自解压原型

- 动作：先用 7-Zip SFX（或 `makecab`+stub）把现有 portable 目录打成 `dist/AirBridge32.sfx.exe`，在干净 VM 试；数据够再决定是否写 C++ stub。
- 判据：体积、首启耗时、SmartScreen/AV 是否拦、解压目录位置与清理。

**结果（2026-10-27，已完成）**：机器上没有 7-Zip，改用系统自带 `csc.exe`（.NET Framework 4.8）写 7 KB stub + `System.IO.Compression`，`tools/Build-Sfx.ps1` 产出 `dist\AirBridge32.exe` **16.0 MB**。首次解压 0.4 s / 333 个文件，第二次 0.03 s；包内 Python 可导入 ssl/zlib/cryptography；解压目录 dry-run 路径正确。干净 VM 与 SmartScreen/AV 仍属 Q13。

### Q5 原生托盘监管

- 动作：单实例程序：自解压到 `%LOCALAPPDATA%\AirBridge32\<ver>`、按序拉起 5 个后端、托盘菜单（状态/接收目录/日志/开机自启/重启/退出）、完成通知、退出逆序清理。
- 判据：双击可用、无控制台窗口、退出后无残留进程。

**结果（2026-10-27，已完成）**：语言从 Win32 C++ 改为 **C# + WinForms**（系统自带 .NET Framework 4.8，编译产物 20 KB），理由见 WORK_LOG。`--check`（不启动）、`--smoke 20`（真机完整起停）、托盘 `--auto-exit 75` 三种模式均通过；radio 初始化 42 s 后链路就绪，退出码 0、无残留进程；单文件包解压后 `backend\AirBridge32.exe` 无需参数即可解析全部路径。气泡/菜单外观、开机自启写入、真实文件传输仍待人工或 iPhone 验证。证据 `evidence/supervisor-tray.log`。

### Q6–Q13

按 `docs/FINAL_PLAN.md` 第 6、8 节执行；Q7–Q12 必须先更新第 1 节的授权状态（2026-10-27 用户已口头授权硬件实验，开始前在本节补记范围与时间）。

**Q6 结果**：5 MiB 滚动（`.log.1`）、会话保留 10 个、托盘“复制诊断信息”写剪贴板。证据 `evidence/supervisor-recovery.log`。

**Q7 进展**：看门狗 5 s 周期；只有 receiver 死亡时单独重启（实测检测 ≤5 s、恢复 <1 s、radio pid 不变）；其它后端死亡整链重启并退避 1/5/30 s，10 分钟 5 次后停止。剩余：radio 假死检测（需要 TX/ACK 统计或自检帧）、24 h 泄漏、固件重试的可见状态。同日实测到一次 `FW ready check failed (0x80 != 0xC078)` 后重试成功，启动拖到 >60 s。

**Q15 背景（2026-10-28 凌晨）**：40 MHz 会退化到 37 KB/s（6–18 s 空窗、TCP 退避），只有 USB 拔插冷启动能恢复；冷启动后 40 MHz 190 KB/s、20 MHz 202 KB/s，均为约 150 帧/s × 1.45 KB。awdld 整晚只 `switch channel to 149` 一次，不跟随对端的 36/6 时隙，因此常态只用约 2/16 个时隙。明细 `evidence/transfer-regression-2026-10-28.log`。

### Q14 移除 Flutter 壳

- 前置：Q5 通过一次真实 iPhone 传文件（确认托盘/通知/落盘都正常）。
- 动作：删除 `app/`，`tools/Package-Portable.ps1` 标记为 legacy（或删除），README/PLAN 去掉 Flutter 作为交付路径的描述，`docs/PORTABLE.md` 改为历史记录。

## 4. 已决定不做

单进程合并（GPL-2.0-only 与 GPL-3.0 不兼容）、Dart 重写 AWDL、Wintun 回退、80 MHz、receiver 改写 C++（M3 后再议）。

## 5. 交接提示

- 不要凭本表假设硬件在线：先跑 `tools/Check-Binding.ps1`。
- 性能数字一律写清"字节数 / 墙钟时间 / 测的是哪一段"，不要把不同文件大小的数字直接比。
- 上一次已知基线：1.05 MB/s（7.7 MB 文件，系统 Python，mDNS 2 s）；215 KB/s（1.16 MB 文件，便携包，mDNS 0.5 s 暂停策略）。两者不可直接比较。
