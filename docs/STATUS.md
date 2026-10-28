# 当前状态与交接（2026-10-28 14:05）

> Q25/Q29 后续：逐连接 TCP 观测与代理异常清理完成，真实 lwIP 本机回归 5/5；独立测试目录仅换 netd、未启动真机、未替换旧包。见 [TCP 诊断](TCP_DIAGNOSTICS.md)、[代理修复](TCP_PROXY_CLEANUP.md)。

> **最新完整交接：[HANDOFF.md](../HANDOFF.md)**，已汇总正常/异常时间线、蓝牙异常边界、代码和证据入口。以下正文为较早快照，遇到冲突优先核查新交接及工作队列。

> Q27 后续：关闭 TCP 合并两次完整但仅约 114/104 KB/s，默认保持开启；链路已停止，未证明稳定性修复。见 [关闭合并测试](TCP_MERGE_TEST.md)。

> Q28 后续：IPC 失败伪报成功、断链假活已修复，新增丢弃计数和发送超时；本机真实回环/缓冲池回归通过，旧版假活有对照复现。新组件仅在 build，原测试包未替换，未做手机实验。见 [本机修复](NETD_OFFLINE_FIX.md)。

> **后续更新优先看 [失败会话复盘与真机结果](STALL_DIAGNOSIS.md)**：最新 fixed 测试 1/2 完成（成功 0.323 MB/s，第二次 408），social 未发现接收端，未完成有效 A/B。链路已停止，下一步采集 TCP 载荷/ACK/窗口。下文“射频退化、需要冷复位”等旧判断尚未被最新证据证实；历史记录见 [14:44 简报](RESEARCH_2026-10-28.md)。

> 给接手的人看：这份文档回答“现在是什么、能做什么、卡在哪、怎么复现”。历史过程见 `WORK_LOG.md`，任务队列见 `WORK_QUEUE.md`，最终形态决定见 `FINAL_PLAN.md`。

## 1. 一句话

AirBridge32 现在是一个**单文件 exe + 原生托盘**的 Windows AirDrop 接收端：双击后全部数据留在 exe 所在文件夹内，iPhone 用系统隔空投送即可传文件。**最好实测 1121 KB/s（29.46 MB / 26.3 s），传输起步 930 帧/s**（约 1.2 MB/s）。已修复“主控被抢、序列不跟随、换台太慢”三个确定性速度问题；剩下的不稳定来自射频状态退化，需要一次冷复位才能恢复，自动化恢复尚未完成。

## 2. 交付形态

```text
<任意可写文件夹>\
  AirBridge32.exe          唯一入口（自解压 stub + 托盘监管）
  app\<构建时间戳>\        解压的后端，仅保留最近 2 个构建
  data\                    全部固化数据，跨构建共享
    keys\                  TLS 自签证书、airdrop-service-id（实例 ID 持久化）
    received\              收到的文件
    logs\<会话>\            各进程日志
    awdl_netd.addr
```

- 不在 `%LOCALAPPDATA%` 或其它机器目录写任何东西；
- 无需安装、无需 Python/Flutter、无需管理员（唯一需要管理员的是一次性 WinUSB 绑定，见 `docs/BINDING_RUNBOOK.md`）；
- 文件夹必须可写，放 Program Files 会直接报错而不是写到别处。

## 3. 进程与数据路径

| 进程 | 许可证 | 职责 |
| --- | --- | --- |
| `AirBridge32.exe`（托盘/监管） | MIT | 单实例、按序拉起后端、健康检查、低速/主控告警、开机自启、退出清理 |
| `airbridge_radio.exe` | GPL-2.0 | RTL8821CU（devourer/libusb），USB 独占，`radio_link` TCP 49876 |
| `radio_pipe_bridge.exe` | MIT | 命名管道 ↔ radio TCP |
| `awdld.exe` | GPL-3.0 | OWL 的 AWDL 控制面，原始 IPv6 到 49877 |
| `awdl_netd.exe` | MIT | lwIP、mDNS 代理、TCP 8771 代理 |
| `python.exe` + `receiver.py` | MIT | AirDrop TLS/HTTP/DVZip 接收端 |

数据路径：iPhone → AWDL → radio → bridge → awdld → awdl_netd(lwIP) → receiver → `data\received`。

## 4. 关键配置（都由监管进程固定，不需要环境变量）

| 项 | 值 | 原因 |
| --- | --- | --- |
| 信道/带宽 | 149 / 20 MHz | 40 MHz 实测更慢（172–190 KB/s）且会退化，已弃用 |
| 选举 | `AWDL_ELECTION_METRIC=200`、`AWDL_ELECTION_COUNTER=100000` | counter/metric 更高者当同步主控；不设会被 iPhone 在几分钟后抢走，速度掉到 200 KB/s 档 |
| 信道序列 | `AWDL_CHANSEQ=social`：第 8 时隙 6 信道，其余 149 | 与 Apple 的序列形状一致，iPhone 一加入就跟随 |
| 换台 | `fastretune_fw=2`（固件 H2C 0x1D）+ 一律 `FastRetune` | 跨频段 65–90 ms → 0.22–0.28 ms |
| 实例 ID | `<data>\keys\airdrop-service-id` | 否则每次启动换 ID，iPhone 列表里越积越多 AirBridge32 |
| 低速检测 | 连续两个 5 s 样本 < 300 帧/s → 托盘告警 + “重置链路” | 阈值来自实测：健康 750–930 帧/s，慢速 15–280 帧/s |

## 5. 速度实测（同一张 29.46 MB 视频，除注明外）

| 场景 | 速率 | 备注 |
| --- | --- | --- |
| 默认（从属，未修正选举） | 202 KB/s | 149/20 基线 |
| 跟随对端序列 | 101 KB/s | 否定：手机只在它的 149 时隙发数据 |
| 我们当主控（metric 200） | 336 → 363 → 970 → 952 KB/s | 方差大，counter 未修正时会被抢主控 |
| counter 修正后 | **1121 KB/s**（26.26 s） | 目前最好 |
| 社交时隙 + 固件快切 | 起步 930 帧/s，约 20 s 后发送方静默、408 超时 | 软件侧已验证，失败原因是射频退化 |
| 40 MHz（各种组合） | 172–190 KB/s | 已弃用 |

## 6. 今天修掉的问题（均有提交）

| 问题 | 修法 | 提交 |
| --- | --- | --- |
| iPhone 用 election counter 抢主控，速度掉档 | `AWDL_ELECTION_COUNTER=100000` | `1cedcdf` |
| iPhone 不跟随我们的信道序列（要 2–3 分钟才切） | 第 8 时隙留 6 信道 | `3037619` |
| 跨频段换台 65–90 ms 吃掉时隙 | 固件快切 + 统一 `FastRetune` | `3037619` |
| 每次启动换实例 ID，AirDrop 列表出现多个 AirBridge32 | ID 持久化 | `cc3b026` |
| 数据写到 `%LOCALAPPDATA%` | `app\` + `data\` 全部留在文件夹内 | `53074ee` |
| 慢速时无感知 | 低速检测 + 托盘“重置链路” | `416b6f8` |
| 主控被抢无感知 | 主控监控 + 告警 | `9a30bac` |
| Flutter 壳 328 MB | 删除 `app/` 与旧打包脚本 | `8468332` |

## 7. 未解决

1. **射频状态退化**（最重要）：用一段时间后 RSSI 掉约 8 个单位、数据帧归零只剩保活帧，传输中断。进程重启无效；软件 USB 复位（`pnputil /restart-device` MI_02）能复位 WiFi，但会弄坏同复合设备的蓝牙（Problem 31），且蓝牙无法软件恢复。已加温度遥测（`thermal=raw/delta`）用于定位，恢复阶梯尚未实现。
2. **蓝牙接口当前是 Error**：上述软件复位造成，需重启系统才能恢复（不影响 AirDrop）。
3. **修复后的稳定性没跑够**：社交时隙 + 快切之后只跑过一次（起步 930 帧/s 后因退化中断），尚未完成“连续 3 次 ≥ 700 KB/s”的验收。
4. **固件再分发授权**：radio 静态链接 Realtek 固件，对外发布前必须三选一（取得许可 / 运行时由用户提供 / 只发源码）。
5. **代码签名、干净 VM 验收**：未做。

## 8. 常驻采样（1 小时，跨 4 次链路重启）

`tools/Monitor-Soak.ps1` 每 60 s 采样一次，`build/soak/soak.csv`：

| 进程 | 工作集 | 私有 | 句柄 |
| --- | --- | --- | --- |
| AirBridge32 | 33–37 MB | 75–79 MB | ~421 |
| airbridge_radio | 7.4 MB | 1.8 MB | 125 |
| awdl_netd | 7.1 MB | 2.2 MB | 131 |
| awdld | 4.5 MB | 9.1 MB | 88 |
| receiver (python) | 26.6 MB | 16.7 MB | 201 |

1 小时内没有增长趋势；句柄与内存随进程重启归零，未见泄漏。端口 49876/49877/8771 全程在听。

## 9. 怎么跑、怎么验

```powershell
# 构建（一次性依赖见 docs/BUILD_WINDOWS.md）
D:\VSBuildTools\...\cmake.exe --build build\radio_usb --config Release
powershell -File tools\Build-Awdl.ps1
powershell -File tools\Build-Supervisor.ps1
powershell -File tools\Package-Headless.ps1
powershell -File tools\Build-Sfx.ps1 -SkipPayload     # 产出 dist\AirBridge32.exe

# 运行：双击 dist\AirBridge32.exe（或复制到任意可写文件夹再双击）

# 观测
python tools\analyze_session.py --latest dist          # 传输速率与帧率
Get-Content dist\data\logs\<会话>\radio.log            # 每 5 s：cb/crc/frames/rssi/thermal
select-string dist\data\logs\<会话>\awdld.log -pattern 'sync master','channel sequence'

# 自检
python tools\check_project.py
powershell -File tools\Check-Binding.ps1               # 只读检查 WinUSB 绑定
```

## 10. 建议的下一步

1. **健康状态机**：传输中帧率 < 50 帧/s 持续 10 s → 判退化 → 恢复阶梯（整链重启 → 冷却后重启 → 提示用户），并记录哪一级有效。
2. **抓一次带温度曲线的退化**：确认是热、USB 电源还是固件状态；再决定预防策略（降温/禁用选择性挂起/周期重校准）。
3. **稳定性验收**：连续 3 次 29 MB ≥ 700 KB/s、零人工干预；把中位数写进 `FINAL_PLAN` 的验收表。
4. **重启系统**清掉蓝牙 Error，再做一次完整回归。
5. 分发收尾：固件授权决策、代码签名、干净 VM。

## 11. 证据索引

- `evidence/transfer-970kbs.log`：970 / 251 / 952 / 1121 KB/s 多次实测与帧率
- `evidence/q15-master-mode.log`：主控 vs 从属 vs 跟随 vs 40 MHz
- `evidence/awdl-sequence-and-fastretune.log`：社交时隙、0.25 ms 快切、930 帧/s 起步与 408
- `evidence/software-usb-reset.log`：软件 USB 复位的副作用与五种失败恢复
- `evidence/supervisor-tray.log`、`evidence/supervisor-recovery.log`：托盘、恢复、日志
- `evidence/data-stays-in-folder.log`、`evidence/single-exe-any-folder.log`：便携布局与分发
- `evidence/bench-receiver.md`：接收端离线 228 MB/s（证明 Python 不是瓶颈）
