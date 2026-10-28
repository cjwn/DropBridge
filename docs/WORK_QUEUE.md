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

### 2026-10-28 独立源码复查后的修复项

| ID | 事项 | 状态与完成判据 | 证据 |
| --- | --- | --- | --- |
| Q18 | 链路统计与监测线程异常 | ✅ radio/监管编译、离线回归通过：上传数据分类、有效 RSSI、并发窗口计数；未据此声称实机提速 | `evidence/radio-telemetry-fix.log` |
| Q19 | 换台完成确认与跨频段补偿同步 | ✅ 实现与离线验证：C2H 有界等待后回读真实信道，失败断开链路；跨频段恢复功率表/天线配置并更新补偿上下文；实机效果独立记 Q20 | `evidence/channel-confirmation-offline.log`、`patches/devourer-airbridge.patch` |
| Q20 | 修复版连续传输验收 | 🔜 未通过。Q24 fixed 第二次 408、social 未发现；最新 Q27 关闭合并两次完整但仅 0.114/0.104 MB/s，不据此认定断流已修复。链路已退出，仍需 Q25 TCP 载荷/ACK/窗口证据 | `docs/STALL_DIAGNOSIS.md`、`docs/TCP_MERGE_TEST.md` |
| Q21 | 修正真实传输分析工具 | ✅ 识别监管时间前缀，区分成功/失败/进行中，区分落盘与传输体字节；三项离线回归通过 | `tools/test_analyze_session.py` |
| Q22 | 失败会话分层时间线复盘 | ✅ 2026-10-28：离线确认控制面仍活动、上传数据停顿；本次上传最大换台 2175 us，温度 29/+3；后续补齐接收空窗仍有少量数据帧的诊断，六项分析回归通过 | `docs/STALL_DIAGNOSIS.md`、`evidence/stall-context-2026-10-28.json` |
| Q23 | 固定信道 / 社交时隙单变量入口 | ✅ 2026-10-28：监管 `--chanseq` / 脚本 `-ChannelSequence`；固定模式真实移除 presence 开关，两模式禁用继承的跟随。真实子进程离线测试、两组 `--check`、非法参数拒绝通过；真机结果见 Q24 | `evidence/chanseq-offline-2026-10-28.log`、`src/supervisor/channel_sequence_test.cs` |
| Q24 | 固定 149 / social 真机排障 | ✅ 实验与负结果记录完成，非验收通过：fixed 1/2 完成，第二次 408 后停止第三次；social 用户两次报告不可见，零 Discover/Upload。仅关闭跳频不足以解决，不能排除历史射频状态；原始日志已冻结备份 | `evidence/fixed-channel-transfer-2026-10-28.json`、`evidence/social-discovery-2026-10-28.json` |
| Q25 | TCP 数据面被动诊断 | 🔜 逐连接载荷/seq/ACK/原始窗口/重叠与跳跃迹象、四个代理边界计数与本机回放已完成；Q29 已修复复现的异常清理，最终 CTest 5/5。只换 netd 的独立测试目录已准备，下一步沿用 Q27 fixed/-j 0 做少量真机对照；真实 iPhone 根因仍未定 | `docs/TCP_DIAGNOSTICS.md`、`docs/TCP_PROXY_CLEANUP.md` |
| Q26 | 汇总给下一位 AI 的完整交接 | ✅ 2026-10-28：集中记录架构、当前状态、三组结果、蓝牙/共存线索、正常与异常时间线、源码版本/证据/命令和授权边界；强调 13:43 断流早于约 13:47 蓝牙 Error，下一步先做状态对比再开展 Q25 | `HANDOFF.md`；仅文档整理、无新硬件实验 |
| Q27 | 关闭 TCP 合并实测 | ✅ 实验记录完成，非验收通过：fixed 149/20、相同原后端、`-j 0`，29.46 MB 两次完整 258.107/284.562 s（0.114/0.104 MB/s），接收哈希一致；回程数据帧更多，非纯 ACK 计数。默认仍开启，原始日志已冻结，链路停止 | `docs/TCP_MERGE_TEST.md`、`evidence/tcp-merge-off-2026-10-28.json` |
| Q28 | 无需手机的 netd IPC 修复与回归 | ✅ 发送失败返回 ERR_IF，建立后断链退出 2，分配/投递丢弃计数，5 s socket 发送无进展超时。真实池/回环及五种进程情形 CTest 2/2；同一 EOF 在旧版复现 >3 s 假活。仅本机构建未替换原包，未声称真机提速/断流修复 | `docs/NETD_OFFLINE_FIX.md`、`evidence/netd-offline-2026-10-28.log` |
| Q29 | TCP 代理回压/复位异常清理 | ✅ 本地 send 5 s 无进展超时、两侧 recv 1 s 轮询检查退出标记、join 后关闭；正常空闲/FIN 响应保留。真实回压+RST、远端 RST、本地 RST 和往返回归最终 CTest 5/5；334 后端文件对照仅换 netd，两个 --check 通过，未启动真机 | `docs/TCP_PROXY_CLEANUP.md`、`evidence/tcp-proxy-cleanup-2026-10-28.log` |
| Q30 | 最新状态交接快照 | ✅ 2026-10-28：整合至 240f88c，现场只读复查 MI_02 OK/WinUSB、MI_00 Error/31，项目进程与指定监听为空；复核新旧版本/334 文件清单，明确首次真机沿用 Q27 fixed/-j 0，仅换 netd。文档/证据整理，无新无线实验 | `docs/HANDOFF_2026-10-28.md`、`evidence/handoff-check-2026-10-28.json` |
| Q31 | lwIP 输入队列深度与代理吞吐 | ✅ 本机完成、**未经真机**：新增 soak 台暴露 `tcpip_inpkt` 只用 `MEMP_NUM_TCPIP_MSG_INPKT` 默认 8 槽，约 8 段（11 KB）在途即丢帧；16 KB 在途塌到 0.01 MB/s（28 次重传/142 帧丢弃）。改为 64 后 64 KB 在途零丢帧、本机 2.6–16.8 MB/s，CTest 6/6（新增 netd_proxy_soak）。**不声称真机提速**；仅换 netd 的 Q31 测试包已备好待真机对照 | `docs/LWIP_INPUT_QUEUE.md`、`evidence/lwip-input-queue-2026-10-28.log`、`evidence/lwip-input-queue-2026-10-28.json` |
| Q32 | 满窗/长跑边界与测试台修正 | ✅ 本机：64 槽下 80/96/128/192 KB 在途全部零丢帧，240 KB 满窗也零丢帧（首次挂住是测试台未重传尾部，已加 go-back-N 尾部重传）；256 MB 单连接 11.657 s / 23.03 MB/s、199,742 帧、0 丢帧、句柄 121 不变、私有 2.3 MB 不变。结论：64 槽吸收的是线程错位而非窗口上限 | `evidence/lwip-window-2026-10-28.log`、`docs/LWIP_INPUT_QUEUE.md` §10 |
| Q33 | 会话分析汇总 netd 丢帧与代理计数 | ✅ 本机：`analyze_session.py` 解析 netd 的 `ipc stats` / `tcp flow` / `proxy flow`，输出丢帧（alloc/copy/input/tx）、逐 flow 载荷与失败数、代理双向字节与结束阶段，`input_fail` 非零时显式告警并说明不是空口证据；`--json` 也带 `netd`；回归 9/9 | `tools/analyze_session.py`、`tools/test_analyze_session.py` |
| Q34 | 会话冻结与汇总脚本 | ✅ 本机：`tools/Collect-Session.ps1` 把会话日志复制到 `build\diagnostics\<时间>-<tag>\`，跑分析并写 `manifest.json`（HEAD、未提交文件、日志与收到文件哈希、当前后端二进制哈希），**不复制密钥/收到的文件、不启动链路**；用 20261028-194820 实测，两份 29.46 MB 文件哈希一致 | `tools/Collect-Session.ps1`、`docs/HANDOFF_2026-10-28.md` §8 |
| Q35 | 生产配置（mDNS 代理开启）下的 TCP 回归 | ✅ 本机：`--mdns` 让 netd 以 `AWDL_NETD_MDNS_PROXY=1` 起（与监管一致），2×16 MB / 64 KB 在途吞吐 3.65/4.13 MB/s、零丢弃、句柄 126 且跨连接不增长、字节一致；CTest 增加 `netd_proxy_soak_mdns`，**7/7** | `evidence/netd-mdns-soak-2026-10-28.log`、`docs/LWIP_INPUT_QUEUE.md` §11 |
| Q36 | 100 连接反复建立/清理 | ✅ 本机：100 个顺序连接（每个 1 MB、32 KB 在途、每次停读 0.3 s）全部字节一致、零丢弃，结束 `active=0`；句柄 121→122 后稳定、私有 2.3 MB 不变，单连接 1.60–1.68 MB/s（含 0.3 s 停读）。未覆盖并发与异常重连竞态 | `evidence/netd-churn-2026-10-28.log`、`docs/LWIP_INPUT_QUEUE.md` §12 |
| Q37 | Q31 包真机单变量（fixed 149/20、-j 0） | ✅ **真机完成**：29,456,959 B 完整落盘、76.79 s = **0.3836 MB/s**、无 408、哈希一致。同一配置 Q27 老 netd 为 0.114/0.104 MB/s → **3.4–3.7 倍**。netd `input_fail=76/22,075`（0.34%，集中起步突发），`overlap` 涨到 155 说明手机在重传。仍未达 1.12 MB/s（那次是 social + 合并开启） | `evidence/q31-phone-fixed-j0-2026-10-28.log`、`docs/LWIP_INPUT_QUEUE.md` §13 |
| Q38 | 输入投递改为背压等待（不丢帧） | ✅ **真机通过**：`g_netif.input()` 返回 `ERR_MEM` 时按 1 ms/次最多等 500 ms 后重投同一 pbuf，超时才丢；新增 `input_retry`。同一包同一参数只换 netd（`9cc25b27`→`300aac25`）：Q37 76.79 s / 0.3836 MB/s / `input_fail=76` → Q38 **34.58 s / 0.8518 MB/s / `input_fail=0` / `input_retry=103`**，落盘哈希一致、无 408。本机 A/B 与 CTest 7/7 亦通过 | `evidence/netd-input-backpressure-2026-10-28.log`、`evidence/q38-phone-fixed-j0-2026-10-28.log`、`docs/LWIP_INPUT_QUEUE.md` §14 |
| Q39 | 合并开启（fixed）真机负结果 | ✅ 负结果记录：同包同 fixed 149/20 只开合并（`join_us=400`），118 s 只到 4.69 MB（约 40 KB/s），操作者中止；fps 33、RSSI 73、`overlap=211`、`repeat_ack=189`，netd `input_fail=0/retry=0`（不是软件丢帧） | `evidence/q38-phone-fixed-merge-on-2026-10-29.log` |
| Q40 | 合并开关真机 A/B（射频计数器归因） | ✅ 真机：合并关 0.8518 / 0.6917 MB/s（两轮），合并开约 40 KB/s。devourer 计数器：CRC 11.4%/12.1% vs **40.3%**，手机帧率中位数 121/625 vs **33**，RSSI 区间重叠（73–80）。对照轮在合并轮之后仍 0.69 MB/s、500–561 fps、零丢帧 → 射频未退化，是合并让手机发聚合帧、同 MCS（中位数 19）下 CRC 升到 3.4 倍。结论：fixed 149/20 保持 `-j 0` | `evidence/q40-phone-merge-ab-2026-10-29.log` |
| Q41 | 默认配置（social + 合并）真机 | ✅ 真机：**31.91 s = 0.9231 MB/s**、落盘哈希一致、无 408、`input_fail=0`、670 帧/s（峰值 826）、CRC 15.8%。当晚五轮里最快。合并不是普遍有害而是与信道序列耦合（social 开、fixed 关），监管默认值正确；仍与历史最好 1.12 MB/s 差一轮噪声的量级。需要 3 次取中位数才算验收 | `evidence/q41-phone-social-merge-2026-10-29.log`、`docs/LWIP_INPUT_QUEUE.md` §15 |
| Q42 | 单文件包带上 Q38 netd | ✅ 离线重打包：`dist\AirBridge32.exe` 从 build 重新生成（16,824,320 B，`bf20e9f2…`），payload 内 netd 为 `300aac25…`、监管 `3ec4d28b…`；旧 exe 备份为 `dist\AirBridge32-pre-q38.exe`；`--extract` 到 `dist\app\20261029-002027` 后 `--check` exit 0、无 MISSING，计划为 social + 合并（即 Q41 实测 0.9231 MB/s 的配置）。未做干净 VM/签名/真机双击验证 | `evidence/sfx-q38-package-2026-10-29.log` |
| Q43 | receiver 对 `/Discover` 分块解析过严 | ⬜ 未开始：Q39 会话里两次出现 `WARNING /Discover rejected: invalid or overlong chunk-size line`（00:05:46、00:06:44），同一时刻上传仍能进行，所以不是当晚慢的原因；但发现/重连路径可能因此失败。待查 chunk 边界处理与手机实际报文，用冻结日志复现，不要求手机 | `evidence/q38-phone-fixed-merge-on-2026-10-29.log` |
| Q44 | 一文件夹 + 压缩包的托盘完整版 | ✅ 本机：`tools/Package-Distributable.ps1` 产出 `dist\AirBridge32\`（333 文件 36.5 MB，根目录 `AirBridge32.exe` 即托盘程序）+ `dist\AirBridge32-portable.zip`（16.1 MB）。默认根解析、开机自启、托盘图标已适配文件夹形态。干净解压后 `--check` exit 0 无 MISSING；`--auto-exit 150` 43 s 就绪、单实例、退出无残留、四端口全空、`%LOCALAPPDATA%` 未创建、数据全在文件夹内 | `docs/DISTRIBUTION.md`、`evidence/distributable-folder-2026-10-29.log` |

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

- 最新完整上下文见 [HANDOFF.md](../HANDOFF.md)。蓝牙异常与正常关闭开关不同；已知低速及 13:43 断流早于约 13:47 的蓝牙异常，不能把全部历史问题归因于蓝牙，也不能直接排除共存/遗留状态。
- 不要凭本表假设硬件在线：先跑 `tools/Check-Binding.ps1`。
- 性能数字一律写清"字节数 / 墙钟时间 / 测的是哪一段"，不要把不同文件大小的数字直接比。
- 上一次已知基线：1.05 MB/s（7.7 MB 文件，系统 Python，mDNS 2 s）；215 KB/s（1.16 MB 文件，便携包，mDNS 0.5 s 暂停策略）。两者不可直接比较。
