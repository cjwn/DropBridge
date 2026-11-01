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
| Q45 | 成品文件夹真机验证（小文件） | ✅ 真机：用 `dist\AirBridge32\AirBridge32.exe`（即将发出的同一个文件夹）跑默认配置，`IMG_0333.PNG` 1,207,111 B **1.23 s = 0.98 MB/s**、落盘哈希一致、无 408、`input_fail=0`；满足 FINAL_PLAN「<1 MB 小文件端到端 ≤2 s」。**29 MB 视频尚未对这份成品跑过**（接收侧二进制与 Q41 逐字节相同，只换了 supervisor） | `evidence/shipped-folder-smallfile-2026-10-29.log` |
| Q46 | 成品文件夹真机验证（29 MB 视频） | ✅ 真机：同一成品文件夹、默认配置，29,456,959 B **26.84 s = 1.0975 MB/s**（当晚最好、与历史最好 1.12 MB/s 差 2% 内）、哈希一致、无 408、`input_fail=0/retry=0`、手机 787 帧/s（峰值 891）、CRC 15.1%、约 23 KB/帧聚合。默认配置现有两轮 0.9231 / 1.0975，**还差一次取 3 次中位数** | `evidence/shipped-folder-video-2026-10-29.log`、`docs/LWIP_INPUT_QUEUE.md` §16 |
| Q47 | 发送通道基础（netd 出站代理 + mDNS 组播修复） | ✅ 本机 + 真机部分：netd 新增 opt-in 出站 TCP 代理（`--allow-send` 才起，本地 8772，`CONNECT <ipv6> <port>` 后原样转发，TLS 端到端）；修掉「开了 MLD 却从未 join `ff02::fb`」导致手机 mDNS 组播全被丢的真 bug；mDNS 应答改为优先回给最后一个提问者；新增 `AWDL_NETD_MDNS_DEBUG`（含十六进制）。离线回归 `--case outbound` 通过；真机实测能收到手机 180 条 mDNS 应答并解析出地址与端口 | `docs/SEND_PATH.md`、`tools/airdrop_send.py`、`tools/test_netd_tcp.py` |
| Q48 | PC → iPhone 发送（AirDrop /Ask + /Upload） | 🔜 **阻塞**：发现、地址/端口、出站 TCP、TLS 握手全部走通，但 iPhone 以 TLS alert **拒绝本项目自签证书**（上游 `Unwilling`）；用户确认 AirDrop 一直是「所有人」，所以不只是“仅联系人”。下一步要么抓确切 alert 判断能否改证书参数，要么确认 iOS 要求 Apple 签名的发送方验证记录（`SenderRecordData`）→ 无解则终止该需求。beacon 走 Intel 蓝牙已可用，与 MI_00 无关 | `docs/SEND_PATH.md` §7、`build/diagnostics/send/tls_probe.py` |
| Q49 | 产品名统一为 DropBridge | ✅ 离线重打包：用户可见名称（AirDrop 公告名、托盘标题、诊断标题、开机自启项、单实例互斥名）与产物名（`DropBridge.exe`、`dist\DropBridge\`、`DropBridge-portable.zip`、`DropBridge-headless.zip`、图标 `dropbridge.ico`）全部改为 DropBridge；内部组件名保持不变。验证：解压后 `--check` exit 0，`--auto-exit 45` 数据面就绪且 receiver 打印 `receiving as DropBridge`，退出无残留、`%LOCALAPPDATA%` 未创建；单文件 `--extract` 与 `--check` 通过。旧 `AirBridge32.*` 产物与 `build\supervisor\Release\AirBridge32.exe`（冻结测试包用）保留 | `evidence/rename-dropbridge-2026-10-30.log` |
| Q50 | devourer 升到 34a9e6f（上游 7 个 jaguar2 修复） | ✅ 真机 A/B：同一 29,456,959 B 视频、同一手机/插口/身份，分钟级间隔对比。发货版 `f7e0731` **92.86 s / 0.32 MB/s / 293–313 fps / 传输窗 CRC 49.3%**；新版 **36.48 s / 0.81 MB/s / 323→788 fps / CRC 12.8%**（Q46 基准 1.0975 MB/s、787 fps、15.1%）。新版帧率已追平历史最好且 CRC 更好，属上游 `88be325`+`d0a9e10`（fast_retune 的 0x838 DFS CCA / notch）与 `f46c28a` 的收益。补丁 `patches/devourer-airbridge.patch` 对新 base **零冲突套用**；`docs/sources.lock.json` 已更新到 `34a9e6f95…` | `evidence/devourer-34a9e6f-ab-2026-10-30.log` |
| Q51 | 固件启动超时（78 s 重下）处理 | ✅ 定位：`cpu-reset retry` 在新旧版本都会出现（旧版 00:13/00:24 会话同样有），`HalmacJaguar2Fw.cpp` 两版字节相同，**不是升级引入**。超时后重试=整包重下，实测 2 min 01 s vs 干净 43 s。本 fork 把就绪轮询预算 250 ms→1 s，并以 W 级打印真实启动耗时（实测 8.3–21.9 ms、1–2 次轮询），避免“只是慢”时触发重下；仍保留重试兜底。14 次连续启动干净 | `evidence/devourer-34a9e6f-ab-2026-10-30.log` |
| Q52 | 分发包内置一次性驱动绑定（Zadig + 引导） | ✅ 新增 `setup\`：`Bind-WinUSB.cmd`（自提升 → 先只读检查 → 只在需要时打开 Zadig → 10×3 s 复查 → 写 `setup.log`，全程只点名 `MI_02`）+ `Check-Binding.ps1`（只读，0=已绑 / 1=需绑定 / 3=网卡不在）+ `zadig-2.9.exe`（GPLv3，附 `licenses\zadig-GPL-3.0.txt` 全文与 `licenses\ZADIG.txt` 的版本/SHA-256/上游/源码书面要约）+ 中文 `README.txt`（含 MI_00/MI_01/MI_02 区分与恢复原驱动的方法）。离线验证：本机已绑 → exit 0；把 VID/PID 换成不存在的 → exit 3；zip 解压后 6 个文件齐全、shipped 检查脚本 exit 0、`--check` exit 0。代价：zip 16.86→22.20 MB | `evidence/setup-zadig-bundled-2026-10-31.log` |
| Q53 | 首次使用一键完成（程序自己装驱动） | ✅ 应用内安装：`RadioBinding` 用 WMI 读 `MI_02` 的 Service；未绑时自提升（一次 UAC）→ `certutil` 导入自签证书到 Root/TrustedPublisher → `pnputil /add-driver` 装自建 WinUSB 包 → 复查 → 自动继续启动；托盘加「安装驱动（首次使用）」；全程写 `data\logs\driver-install.txt`。驱动包由 `tools\Build-DriverPackage.ps1` 用 SDK 的 makecat+signtool 现做（不需要 WDK/inf2cat），`signtool verify /pa` 0 错。附带 UX 修正：未知参数现在 exit 2（以前会静默进托盘启动链路，我实测踩到）；首次启动自动设开机自启并提示一次（`data\autostart-offered` 标记，菜单可关）。**未验证**：真正的未绑→已绑转换（要先解绑唯一可用网卡，需你点头） | `evidence/inapp-driver-install-2026-10-31.log` |
| Q54 | `-s 1` vs `-s 2`（awdld 空口流宣告）真机 A/B | ✅ 同一 29,456,959 B 视频、同一手机/身份/插口、两次都开着 beacon：`-s 2` **26.89 s / 1.10 MB/s / 970 fps / CRC 3.4%**，`-s 1` **27.04 s / 1.09 MB/s / 867 fps / CRC 4.2%**，落盘哈希均与源一致、无 408。差 0.9% 属噪声 → **保留 `-s 1` 为默认**（符合 1T1R，也证明 e1ba5b7 的诊断没错，但既不提速也不治慢窗）。新增 `--streams 1\|2` 开关便于复现。**真正的发现**：两次 1.10 MB/s 都是 beacon 在广播时测到的，之前不带 beacon 的多次只有 0.32–0.88 MB/s、CRC 13–49% | `evidence/streams-ab-2026-10-31.log` |
| Q55 | 强杀父进程后子进程占端口 / 网卡楔死 | ✅ 三层修完并真机验证：① 非 radio 子进程全部放进 `CreateJobObject` + `KILL_ON_JOB_CLOSE` 的作业对象（`ChildJob`，kernel32 P/Invoke），父进程一死（含 Stop-Process 强杀）系统连带清理，端口不会残留；② **radio 故意不放作业里**，改为收 `AIRBRIDGE_PARENT_PID` 并用 `WaitForMultipleObjects` 同时等「父进程句柄」与 `Local\DropBridgeRadioStop`，任一触发就关监听走干净收尾——放作业里会被 TerminateProcess，正是楔死芯片的原因；③ `ReclaimLeftovers()` 启动时若端口被占，只回收**本包目录下**的同类进程，其它程序一律不动。实测：就绪后 `Stop-Process -Force` 强杀托盘 → 8 秒内本包进程与 49876/49877/8771 全部消失；立刻重启（**不拔插**）43 秒到数据面、`shutdown: done` / exit 0。顺带修掉固件轮询：原先按 20000 次计数（每次读可能要 15 ms）导致卡死时挂 309 秒，改为**墙钟 1 秒** | `evidence/q55-no-leftovers-2026-11-01.log` |
| Q56 | 托盘自动开蓝牙 beacon | ✅ 代码完成：`src\beacon\airdrop-beacon.ps1`（上游 MIT 原样）+ 打包到 `backend\beacon.ps1`；`TrayApp` 负责启动/看门狗重启/退出停止，输出写 `data\logs\<会话>\beacon.log`；托盘菜单「蓝牙 beacon（帮 iPhone 发现这台电脑）」默认开，关掉会写 `data\beacon-off` 记住选择；脚本缺失或无蓝牙只记日志、不影响接收。已验证：01:42:37 起链路后 0.4 s 内 `beacon.log` 打出 `advertising`。**端到端未验**：那两次 radio 卡在 libusb（见 Q57），需要拔插网卡后重测 | `evidence/beacon-in-tray-2026-10-31.log` |
| Q57 | 卡死的 radio 被当成健康进程 | ✅ 已修并真机验证：`Child.LastOutputUtc` 在每行输出时打点，`Supervisor.SilentSeconds("radio")` 暴露静默秒数，`TrayApp.CheckSilence` 在 5 s 看门狗里判断「进程活着但 >30 s 无输出」→ 记日志并整链重启；3 分钟内第二次触发则弹「网卡没响应，请拔插」。测试：用 `NtSuspendProcess` 冻结 radio 模拟假死 → 23:11:19 报 `radio silent for 32 s (hit 1)` 并重启（新 radio 23:11:35 启动）。冻结进程无法响应优雅停，只能被强杀，这正是第二次要提示拔插的原因 | `evidence/q57-q58-health-2026-10-31.log` |
| Q58 | 停链路优雅退出（修「必须拔插」+ RX 线程不再 detach） | ✅ 完成并真机验证：根因不是 devourer，而是我们自己的启动顺序——RX 线程原先在 `accept()` 之前启动，而 `StartRxLoop()` 进入时会**清掉 `_rx_stop`**，于是「先 stop、后 start」的那次停止被吞掉，join 永远等不到。修法：① RX 线程改到 `accept()` 成功 + HELLO 之后才启动（失败的迭代没有线程）；② 两条收尾路径统一为「停 → 有界等 2 s → 仍在则 `radio->Stop()` 取消传输 → 再等 2 s → 能回就 join，否则才 detach」；③ accept 失败的迭代直接跳过等待。实测整段收尾 **48 ms**、无 detach、exit 0；停→启循环（不拔插）连续通过 | `evidence/graceful-radio-stop-2026-10-31.log`、`evidence/rx-worker-join-2026-10-31.log` |
| Q59 | 软件图标换成用户的明暗两版（exe + 托盘） | ✅ 本机：黑色线条版作 exe 图标与浅色任务栏托盘，白色线条版作深色任务栏托盘；两版随 exe 内嵌，托盘读 `SystemUsesLightTheme` 选版并订阅 `UserPreferenceChanged` 跟随系统切换；单文件 stub 补上 `/win32icon`。`Build-Supervisor.ps1 -Test` 全过（新增 `src\supervisor\tray_icon_test.cs`），build / 文件夹 / 单文件三个 exe 的图标哈希一致。重新打包 headless/portable/单文件：portable 解压后 `--check` exit 0 无 MISSING、单文件 `--extract` 后 `--check` 通过；`dist\DropBridge\data` 备份后原样放回并重启就绪。未验证：真正切换系统明暗看托盘换图；黑色线条图标在深色 Explorer 上对比度低。**追加**：用户反馈 16 px 托盘发糊 → 量到 16 px 帧 solid 像素为 0（原图直接下采样），改为 `tools/hint_icon.py` 重画 16/20/24/32 为 1 px 像素帧（48 px 以上仍是原图），重建并重打包。**再追加**：用户反馈「还是细条、优先别重绘」→ 真正根因是进程 DPI 不感知，托盘要的是 16 px 而任务栏按 175 % 画 28 px，Windows 放大 16 px 帧；改为 `TrayIcons.SmallIconPixels()` 读真实 DPI 取 28 px 帧，素材恢复原图（`tools/icon_frames.py` 纯重采样九档），删掉 `hint_icon.py`。**再追加**：清掉 `dist\app` 里带旧蓝图标的旧解压（shell 复核 dist 两个 exe 已是新图标）；新增 `AppIdentity` 给通知加应用身份（AUMID + `HKCU\Software\Classes\AppUserModelId` + 包内 `DropBridge.ico` 作 `IconUri`），**2026-11-01 22:11 用户确认通知头部已显示本项图标**（标题 DropBridge） | `evidence/icon-refresh-2026-11-01.log` 第 7–11 节 |
| Q60 | 设置持久化 + 自定义设备名称 | ✅ 本机：新增 `data\settings.ini`（`deviceName`、`beacon`，纯文本可手改，没改过不落盘；旧的 `data\beacon-off` 作迁移读取）；托盘菜单加「设备名称：<名字>…」对话框，校验后写盘、更新托盘提示与通知身份，并通过 receiver 的 stdin 发 `{"do":"set","name":...}` **立即改名**不重启射频；18 处气泡标题与 `--check` 的 `device:` 行跟随。`-Test` 新增 `settings_test.cs`（PASS settings）；成品文件夹设 `客厅的电脑` + `beacon=false` 重启后 receiver 公告 `receiving as 客厅的电脑`、无 beacon.log、通知身份同步；删文件重启恢复默认。未验证：菜单对话框与立即生效要人工点一次 | `evidence/settings-device-name-2026-11-01.log` |
| Q61 | 收到图片的弹窗 + 一键复制到剪贴板 | ✅ 本机：系统 toast 给绿色 exe 加不了按钮（需注册 COM 激活器），改为右下角自绘小窗（设备名/文件名/大小/缩略图 + 「复制图片」「打开文件夹」「×」，12 s 自动消失、悬停暂停、最多 3 个、不抢焦点）。复制同时放位图与文件；HEIC/WebP 解码不了时只放文件；非图片仍走系统通知。关键坑：进程 DPI 不感知时 `Screen.WorkingArea` 给物理像素、窗口坐标是 96 dpi 虚拟化的，弹窗被放到屏幕外 → 加 `TrayIcons.RealScale()` 换算后落在右下角。验证：屏幕截图确认压在最上层；`SendMessage(BM_CLICK)` 点复制后 STA 进程读到 `image: True 720x540` + 文件路径。未验证：多显示器/任务栏不在底部 | `evidence/received-copy-popup-2026-11-01.log` |
| Q62 | 真正的系统通知按钮（toast + 协议回调） | ✅ 本机：未安装 exe 的 toast 按钮必须有 ①带 AUMID 的开始菜单快捷方式 ②`dropbridge://` 协议，否则 toast 被静默丢弃（实测未注册 AUMID 连通知设置键都不生成）。包内 `tools\toast.ps1`（PS 5.1 + WinRT，环境变量传参）与 `tools\register.ps1`（写 AUMID/协议/快捷方式，可 `-Remove`）；程序启动自检并在搬家后重写；收图片先走 toast（「收到照片」+ 文件名/大小 +「复制到剪贴板」+ 缩略图），失败退回自绘弹窗；`dropbridge://copy?file=` 由第二个实例经命名管道转给运行中的实例。验证：`Start-Process` 该 URI 后日志 `clipboard: copied ... (toast button)`、STA 读到 `image: True 1290x2796` + 文件；通知库 Payload 含 `<action content="复制到剪贴板">`。未验证：toast 横幅此刻未弹出（已入库，疑专注助手/通知设置拦截） | `evidence/toast-button-2026-11-01.log` |

| Q50 | 迁移环境后完整本机编译与打包 | ✅ 2026-10-30：恢复锁定上游与依赖，MSVC 2026 构建 radio/netd/托盘，MinGW 构建 IPC awdld；生成 portable ZIP、headless ZIP 与单文件 EXE，两种布局 `--check` exit 0。radio 2/2、AWDL 5 个测试程序、托盘 3 项、分析器 10/10 通过；netd 6/7，IPC 测试 15 秒及独立 90 秒均超时，未解决。lwIP 使用 2.2.1，区别于历史文档中的 2.2.2；不是旧包逐字节重建。未启动无线或测试手机 | `evidence/compile-windows-2026-10-30.log`、`build/*-build*.log` |

| Q51 | 首次传输后发现丢失与恢复 | 🔜 2026-10-30 真机复现：493588 B 照片成功后，后续手机 TCP/TLS 连接反复失败；六进程/端口仍在、本机 TLS/HTTP 正常、netd 无丢弃。冻结故障会话后重启原成品链路，用户确认重新出现，随后收到 1542983 B PNG。根因未定、未修改源码，不算稳定性修复 | `evidence/discovery-recovery-2026-10-30.log`、`build/diagnostics/20261030-165851-discovery-lost-after-upload` |

Q51 补充（2026-10-30）：用户确认双机在线时 16 Pro Max 两次 JPG 仅 0.1174/0.0796 MB/s；慢传窗口网卡 CRC 错误/回调增量比例 69.6%/67.0%，netd 无软件丢弃。本机仍为同步主控。15 PM 退出分享页后仍 Ask 408；同配置重启后同图 Ask 收体仍需 12.658 秒（当时 peers=1，稍后第二 peer 重现），说明不限于双机同时打开或旧进程状态。根因未定，不能把网卡总计数当作单设备丢包率或归因于 iOS 版本。见 `evidence/two-phones-16pm-2026-10-30.log`。

Q51 候选修正（2026-10-30）：确认 1T1R 网卡却被两处启动入口以 `-s 2` 宣告双流，源码改为 `-s 1` 并通过离线检查。单流候选会话无有效手机请求，不能作性能对照；恢复原成品 `-s 2` 后16 PM同图339331 B连续两次落盘（4.814/2.235秒），用户反馈流畅/短暂卡顿但正常。现保留原配置继续运行，原 ZIP/SFX 未更新，不能把成功归功于单流修正，根因仍待查。见 `evidence/one-stream-advertisement-2026-10-30.log`。

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
