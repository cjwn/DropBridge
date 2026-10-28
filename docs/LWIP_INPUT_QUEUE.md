# Q31：lwIP 输入队列深度（8 → 64）与本机代理吞吐

2026-10-28；基于 `68449b6`（Q29 清理之后）。**只有本机回环**：没有 radio、
awdld、USB、空口或手机，没有改驱动、系统网络、信道、窗口或合包参数。

## 1. 结论先行

- 新加的长跑测试台（`tools/test_netd_tcp.py --case soak`）第一次跑就暴露丢帧：
  `input_fail` 非零，且一旦丢帧，测试端因为不重传会直接卡死。
- 根因是 **`tcpip_inpkt()` 从 `MEMP_NUM_TCPIP_MSG_INPKT` 池取 `struct tcpip_msg`，
  上游默认只有 8 个槽**。槽满时 `g_netif.input()` 返回 `ERR_MEM`，这一帧被丢掉，
  并被计入 `input_fail`；TCP 只能等对端重传。
- 8 个槽约等于 8 个 1360 B 段（≈11 KB）在途。实测：8 KB 在途还正常，
  **11 KB 开始丢数据，16 KB 在途直接塌成 0.01 MB/s**（1 MB 用了 85.1 s、
  28 次回退重传、142 帧被丢）。
- 改成 64 个槽后，48 段（64 KB）在途仍零丢帧：16 KB / 32 KB / 64 KB 分别为
  2.49 / 4.06 / 24.39 MB/s（多次运行 16.8–26.8 MB/s）。此时瓶颈已经是
  Python 测试端和本机调度，不是 netd 的输入边界。
- **边界**：这只证明本机 netd 的输入边界被抬高，并给出代理吞吐的下限；
  **没有证明真机提速**。真机 0.10–0.32 MB/s 的断流与此模式相似，但那是假设，
  要按第 7 节做一次单变量真机对照才能说。

## 2. 机制

`awdl_netd` 的 IPC 读线程把每个 AWDL 帧（原始 IPv6 包）复制进 pbuf 后调用
`g_netif.input()`（= `tcpip_input`）。`tcpip_input` → `tcpip_inpkt()`：

```c
msg = (struct tcpip_msg *)memp_malloc(MEMP_TCPIP_MSG_INPKT);
if (msg == NULL) { return ERR_MEM; }    /* lwip/src/api/tcpip.c:268 */
```

所以“输入队列”的真实深度是 memp 池的条数，不是 mbox 的长度（win32 port 的
mbox 是 100 项环形队列，`sys_mbox_new` 忽略传入 size）。默认 8 条来自
`lwip/src/include/lwip/opt.h`；本工程 `lwipopts.h` 里 `TCP_WND` 是 240 KB，
即允许对端在途约 177 段——接收窗口与输入队列差了一个数量级，窗口一开
就可能整窗撞上 8 条的上限。

丢帧的后果不是“慢一点”：TCP 在洞后面的所有段都要等重传，本机测试里
`retransmits` 从 0 跳到 3 → 28，goodput 从 2.1 MB/s 掉到 0.01 MB/s。
丢的也不一定是数据帧——纯 ACK 帧同样占这个池，丢 ACK 不会丢字节，
只让两端互相等（有一次 8 KB 点丢了 17 帧但上载字节仍完整）。所以
**`input_fail` 不能直接当数据丢失**，数据是否缺要看重传次数和最终字节数。

## 3. 改动

| 文件 | 改动 |
| --- | --- |
| `src/awdl_netd/lwipopts.h` | `MEMP_NUM_TCPIP_MSG_INPKT` 8 → 64，带 `#ifndef` 便于后续单变量覆盖；注释写明原因与 100 项环的上限 |
| `src/awdl_netd/CMakeLists.txt` | 注册 CTest `netd_proxy_soak`：3 个连接 × 4 MB，12 段在途，超时 180 s |
| `tools/test_netd_tcp.py` | 新增 `--case soak`：多连接顺序复用同一 netd 进程，字节逐字节校验（双向），零丢帧断言、ACK 停顿看门狗（8 s）、go-back-N 重传、零窗口探测、句柄/私有字节增长断言 |

内存代价：64 × `struct tcpip_msg`（约 40 B）≈ 3 KB。实测 netd 工作集 7.0–7.1 MB、
私有 2.3 MB，与改动前一致；3 个连接的句柄数始终 121，无增长。

## 4. 本机结果（`--mb 4 --cycles 1 --stall 0 --batch 8`，MB=10^6 B）

| netd | 在途窗口 | 上行 goodput | 回退重传 | netd 丢弃 (alloc/copy/input/tx) |
| --- | --- | --- | --- | --- |
| Q29（槽=8） | 5.5 KB（4 段） | 2.02 MB/s | 0 | 0, 0, 0, 0 |
| Q29（槽=8） | 8 KB（6 段） | 2.15 MB/s | 0 | 0, 0, 0, 0 |
| Q29（槽=8） | 11 KB（8 段） | 0.38 MB/s | 3 | 0, 0, 4, 0 |
| Q29（槽=8） | 16 KB（12 段） | 150 s 未完成（4 MB 那一轮被测试台中止） | — | — |
| Q29（槽=8） | 16 KB（12 段，1 MB） | **0.01 MB/s** | 28 | 0, 0, **142**, 0 |
| 本改动（槽=64） | 16 KB | 2.49 MB/s | 0 | 0, 0, 0, 0 |
| 本改动（槽=64） | 32 KB | 4.06 MB/s | 0 | 0, 0, 0, 0 |
| 本改动（槽=64） | 64 KB | 24.39 MB/s（多次 16.8–26.8） | 0 | 0, 0, 0, 0 |

CTest 改动后 **6/6 通过**（新增的 `netd_proxy_soak` 15.3 s）。下行 0.25 MB
响应在两个版本上都逐字节一致，延迟 15–31 ms。

`stall=1.2 s`（接收端故意停读）时每个连接会出现 1 次回退重传——那是 TCP
对停读的正常反应，不是丢帧；所以 CTest 用默认值时不把 `retransmits` 当断言。

## 5. 判读边界

- 不是无线证据：没有 radio、awdld、USB、空口或 iPhone，也没有经过 TLS/AirDrop。
- 不是真机速度承诺：本机 16.78 MB/s 只说明 netd 与 Python 测试端之间不再被
  8 个槽卡住；真机上限仍由空口、帧率、ACK 与手机 TCP 决定。
- 8 个槽在本机是“过窄”，但**是否也是真机的主因尚未证明**——真机 930 帧/s
  是持续速度，8 条池处理得过来；只有 USB/合包把多帧挤进同一毫秒的突发时才会撞上。
  这条假设需要真机交叉验证（`input_fail` 在真机会话里是否为 0）。
- `input_fail` 是“投递到 lwIP 失败”，不等于 TCP 层丢弃，也不等于手机重传。
- 5 s 快照的丢帧计数是累计值；本表取自每个进程退出前的最后一次快照。

## 6. 复现

```powershell
$cmake = 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build build/awdl_netd --config Release --parallel 8
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir build/awdl_netd -C Release --output-on-failure

# 单次长跑（默认 16 KB 在途、1.2 s 接收端停读）
python tools\test_netd_tcp.py build\awdl_netd\Release\awdl_netd.exe --case soak --mb 8 --cycles 4

# 抬高在途窗口（需要更深队列；旧 8 槽二进制会在这里丢帧/塌速）
python tools\test_netd_tcp.py build\awdl_netd\Release\awdl_netd.exe --case soak --mb 4 --inflight-kb 64

# A/B 扫表（8 槽 = Q29 测试包里的 netd）
powershell -File build\diagnostics\Sweep-Queue.ps1
```

`--allow-drops` 只用于故意超载的扫表；默认配置下丢帧会让测试失败。

## 7. 下一次真机对照（尚未执行）

`build/diagnostics/ipc-queue/test-package/` 已按 Q29 同一方式准备：从冻结包
`dist/app/20261028-143130/backend/` 复制，**334 个文件集合相同，只有
`awdl_netd.exe` 不同**；两次 `supervisor --check`（fixed 149/20，`-j 400` 与
`-j 0`）都是 exit 0、无 MISSING。入口 `Start-Q31-Test.cmd`（**会初始化 USB
并发 AWDL 帧，本轮未运行**）。

现在有两个只差 netd 的候选包：

| 包 | netd | 相对冻结基线多出 |
| --- | --- | --- |
| `build/diagnostics/proxy-cleanup/test-package`（Q29） | `06264244…` | Q29 连接清理 |
| `build/diagnostics/ipc-queue/test-package`（Q31） | `9cc25b27…` | Q29 清理 + 64 槽输入队列 |

（MSVC 不可复现：同一份源码只改注释重建也会换哈希，本次中途出现过
`788db05e…`。以 `evidence/lwip-input-queue-2026-10-28.log` 里记录的
最终构建哈希为准。）

建议：**先用 Q31 包**（一次手机传输），沿用 Q27 的 fixed 149/20、`-j 0`，
只换 netd。判读：

- 若速率明显回到 1 MB/s 以上或不再断流 → 输入队列是主因之一，再用 Q29 包
  跑一次对照，把清理与队列分开归因。
- 若仍 0.1–0.3 MB/s 或继续 408 → 队列不是真机主因；下一次会话应统计
  `input_fail` 是否为 0（为 0 说明真机没有撞这个边界），转向空口/ACK/合包。

真机不看本机 goodput，只看：文件字节/墙钟时间、是否完整、`input_fail`、
`retransmits`、同一 flow 的 TCP 窗口与 ACK 推进。

## 8. 证据

- `evidence/lwip-input-queue-2026-10-28.log`：三个 netd 二进制哈希、扫表、
  两次塌速明细、CTest 6/6、两份 `--check` 计划。
- `build/diagnostics/queue-sweep/`：每一轮的原始 stdout（`*.log`）与 `summary.json`。
- `build/diagnostics/ipc-queue/`：测试包的 `--check` 计划、入口脚本。

## 9. 未做

- 未在真机/空口验证；未测 24 h；未测多连接并发（当前 `MEMP_NUM_TCP_PCB=16`、
  代理按连接起线程，并发上限未测）。
- 未改 `TCP_WND`、`PBUF_POOL_SIZE`、`TCP_SND_BUF` 或合包参数；如果真机仍有
  丢帧，下一个该量的是这些（一次只动一个）。

## 10. Q32 补充：满窗与长跑（同一天，仍只有本机）

Q31 只测到 64 KB 在途就停了；这里把在途窗口推到满窗（`TCP_WND` = 240 KB），
再跑一次 256 MB 长跑，确认 64 槽不是靠“窗口小”才过关的。

| 在途窗口 | goodput | 重传 | netd 丢弃 |
| --- | --- | --- | --- |
| 80 KB | 17.33 MB/s | 0 | 0 |
| 96 KB | 16.78 MB/s | 0 | 0 |
| 128 KB | 16.78 MB/s | 0 | 0 |
| 192 KB | 29.75 MB/s | 0 | 0 |
| 240 KB（满窗） | 5.77 MB/s | 0 | 0（首次尝试见下） |

- 80–192 KB 在途全部零丢帧，说明 64 个槽不是“在途窗口上限”，它吸收的是
  IPC 读线程与 lwIP 线程之间的**瞬时错位**；只要 lwIP 线程跟得上，在途数据
  可以远大于 64 段。
- 240 KB 满窗第一次跑挂住并丢 115 帧，但那是**测试台缺陷**：数据全部发完后
  尾部丢段不再重传，测试端只能死等 ACK。补上 go-back-N 尾部重传后同一配置
  0 丢帧跑完。240 KB 属于临界（按 20k 段/s 过驱动），不能当作队列上限。
- 256 MB 单连接（64 KB 在途）：11.657 s、**23.03 MB/s**、199,742 帧、0 丢帧、
  句柄恒为 121、私有字节恒为 2.3 MB，收完字节一致。这是“长时大流量不泄漏、
  不退化”的本机证据，但仍不是空口证据。
- 真机 930 帧/s 比这里的 20k 段/s 低 20 倍，所以正常链路不会撞上 64 槽；
  **唯一还需要真机验证的是 USB/合包是否会以 >64 帧的突发投递**——那才是
  8 槽版本可能被击穿的方式。

## 11. Q35 补充：生产配置（mDNS 代理开启）下的 TCP 负载

之前六个测试都在 `AWDL_NETD_MDNS_PROXY` 未设置时跑，而监管启动 netd 时是
**开着** mDNS 代理的：多两条 UDP 路径、多两个 lwIP netconn、多两个线程。
补一个生产配置回归：

| 配置 | 2 × 16 MB，64 KB 在途 | netd 句柄 | 私有字节 | 丢弃 |
| --- | --- | --- | --- | --- |
| mDNS 代理关（旧测试配置） | 4.05 / 4.11 MB/s | 121 | 2.3 MB | 0 |
| mDNS 代理开（生产配置） | 3.65 / 4.13 MB/s | 126 | 2.4 MB | 0 |

同一命令只差 `--mdns`：吞吐在噪声内，句柄多 5 个（两个 UDP socket + 两个线程）
且跨连接不增长，字节仍逐字节一致。CTest 增加 `netd_proxy_soak_mdns`，
**7/7 通过**。

注意：该用例会占用本地 UDP 15353 和 TCP 8771，**不能与正在运行的真机链路同时跑**。

## 12. Q36 补充：100 个连接的反复建立/清理

Q29 修的是“异常连接不回收”，此前只在 3–4 个连接上验证过。这一轮跑
100 个顺序连接（每个 1 MB、32 KB 在途、接收端每次停读 0.3 s）：

- 100 个连接**全部逐字节一致**、零丢帧、零重传风暴，结束后代理 `active=0`；
- netd 句柄 121 → 122 后稳定（前 50 个 121、后 50 个 122），私有字节
  2.3 MB 不变，工作集 7.0–7.1 MB；
- 单连接速率 1.60–1.68 MB/s（1 MB 含 0.3 s 停读，扣掉停读约 3 MB/s）。

即连接清理路径在 100 次反复下不泄漏、不残留、不退化。仍未覆盖：并发连接、
异常断开后的重连竞态、以及真机上的空闲超时。

证据：[netd-churn-2026-10-28.log](../evidence/netd-churn-2026-10-28.log)。

## 13. Q37 真机：Q31 包第一次上手机（2026-10-28 23:56）

**第一次用 Q31 包（64 槽 + Q29 清理）做真机单变量对照**，配置沿用 Q27：
fixed 149/20、`-j 0`、metric 200 / counter 100000，同一视频、同一 `dist\data`。

| 项 | 结果 |
| --- | --- |
| 文件 | `7545-7.MP4` 29,456,959 B，**完整落盘**，SHA-256 `5d057f9c…`（与历史副本一致） |
| 耗时 | 23:56:32.142 → 23:57:48.927 = **76.79 s → 0.3836 MB/s** |
| 结束状态 | `stored` + `POST /Upload 200`，**没有 408**，手机没有重试 |
| netd 收帧 | 22,075 帧 / 31.29 MB，`input_fail=76`（0.34%），`alloc/copy/tx` 全 0 |
| 丢帧分布 | 早期 654 帧时已丢 45，1,223 帧时丢 55，全期 76——**集中在起步突发** |
| 手机行为 | 开局 `overlap` 涨到 155（在重传）；稳定后约 560 帧/s（5 s 窗口约 0.9 MB/s） |

同一配置的历史对照：Q27（8 槽、无 Q29 清理）258.1 / 284.6 s = **0.114 / 0.104 MB/s**。
即本轮 **0.3836 MB/s 是 Q27 的 3.4–3.7 倍**，且从“两次里一次 408”变成一次完整落盘。
仍低于 1.12 MB/s 的历史最好，但那次是 social 时隙 + 合并开启，不是同一变量。

判读：

- 队列加深与清理修复合起来在真机上**确有收益**，此前“0.1–0.3 MB/s 是射频退化”的
  说法不再成立为唯一解释；
- 64 槽**仍不够**：手机开局会把初始窗口整窗突发，76 帧因此被丢，触发重传与
  慢启动回退。稳态 560 帧/s 距 930 帧/s 的健康值还有距离；
- `-j 0` 让 `joined` 恒为 0，单帧只承载约 1.4 KB，本身就压低了上限——下一轮
  应单独把合并打开对照，而不是同时改队列。

证据：[q31-phone-fixed-j0-2026-10-28.log](../evidence/q31-phone-fixed-j0-2026-10-28.log)；
冻结会话（日志 + 分析 + 清单）：`build/diagnostics/20261028-235816-q31-fixed-j0-phone/`。
