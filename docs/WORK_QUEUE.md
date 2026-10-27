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
| 允许初始化网卡 / 发 AWDL 帧 | ⛔ **本轮未授权** | 之前的授权只覆盖当时的单次实验；Q7–Q12 开始前必须重新取得用户明确同意 |
| 允许换驱动/改网络/改防火墙 | ⛔ 未授权 | 按 AGENTS.md，需单独授权并写明恢复办法 |

## 2. 队列总表

| ID | 事项 | 依赖 | 完成判据 | 状态 | 证据 |
| --- | --- | --- | --- | --- | --- |
| Q1 | receiver 离线吞吐台 | 无 | 20 MB 三次，给出 Python/TLS/zlib 侧 MB/s 上限与结论 | ✅ 2026-10-27：compressed 228 MB/s、stored 278 MB/s，非瓶颈 | `evidence/bench-receiver.md` |
| Q2 | 身份证书去掉外部 openssl | 无 | 无 Git/OpenSSL 的 PATH 下能生成证书并 `/Discover` 200 | ✅ 2026-10-27：cryptography 进程内生成，openssl 仅回退 | `evidence/identity-no-openssl.log` |
| Q3 | 无壳手动链路脚本 | 写脚本无依赖；**跑需授权** | 一键起停、日志分离、退出无残留 | 🔜 脚本与无壳包完成（dry-run、端口冲突、打包后路径均验证）；`-Run` 待授权 | `tools/Start-Chain.ps1`、`tools/Package-Headless.ps1` |
| Q4 | 单文件自解压原型 | 无 | 干净 VM 双击可跑，≤30 MB，退出可清理 | ⬜ 载荷已就绪：`dist\AirBridge32-headless.zip` 16.0 MB；SFX 外壳未做 | — |
| Q5 | 原生托盘监管（替代 Flutter） | Q4 | 无窗口常驻、单实例、开机自启、退出清理、完成通知 | ⬜ | — |
| Q6 | 日志轮转与诊断包 | Q5 | 5 MB×3 轮转；导出前脱敏 | ⬜ | — |
| Q7 | 看门狗与恢复阶梯 | 硬件授权 | 假死注入后 ≤10 s 自动恢复；24 h 无泄漏 | ⬜ | — |
| Q8 | E1 时隙/信道序列实测 | 硬件授权 | 149 占空比、joined 帧/s、队列深度曲线 | ⬜ | — |
| Q9 | E2 ACK 延迟与 TX 优先级 | 硬件授权 | ACK 等待分布；对照调优前后 MB/s | ⬜ | — |
| Q10 | E3 USB 帧率/聚合 | 硬件授权 | URB/s、帧/s、单帧字节 | ⬜ | — |
| Q11 | E4 mDNS 静默策略 | 硬件授权 | 上传期 mDNS 帧数为 0；发现延迟不退化 | ⬜ | — |
| Q12 | 原生 mDNS 收尾 | Q11 | iPhone 能只靠原生 mDNS 发现 | ⬜ | — |
| Q13 | 分发验收（干净 VM + 许可清单） | Q5 | FINAL_PLAN 的验收指标全绿 | ⬜ | — |

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

### Q5 原生托盘监管

- 动作：C++/Win32 单实例程序：自解压到 `%LOCALAPPDATA%\AirBridge32\<ver>`、按序拉起 6 个后端、托盘菜单（打开接收目录/查看日志/退出）、完成通知、退出逆序清理、开机自启。
- 判据：双击可用、无控制台窗口、退出后无残留进程。

### Q6–Q13

按 `docs/FINAL_PLAN.md` 第 6、8 节执行；Q7–Q12 必须先更新第 1 节的授权状态。

## 4. 已决定不做

单进程合并（GPL-2.0-only 与 GPL-3.0 不兼容）、Dart 重写 AWDL、Wintun 回退、80 MHz、receiver 改写 C++（M3 后再议）。

## 5. 交接提示

- 不要凭本表假设硬件在线：先跑 `tools/Check-Binding.ps1`。
- 性能数字一律写清"字节数 / 墙钟时间 / 测的是哪一段"，不要把不同文件大小的数字直接比。
- 上一次已知基线：1.05 MB/s（7.7 MB 文件，系统 Python，mDNS 2 s）；215 KB/s（1.16 MB 文件，便携包，mDNS 0.5 s 暂停策略）。两者不可直接比较。
