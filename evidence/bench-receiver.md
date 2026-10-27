# Receiver 离线吞吐台（Q1）

- 时间：2026-10-27 23:25:03 +0800
- 机器：Windows-11-10.0.22631-SP0 / Python 3.12.10
- 负载：20 MB 随机字节，cpio + DVZip，128 KB 分块经 TLS 上传
- 计时：服务端 `/Upload` 开始到 `stored`（不含客户端收尾）
- 不包含：awdl_netd 的 lwIP 代理与 AWDL 空口

| 模式 | 运行 | 秒 | MB/s | SHA-256 |
| --- | --- | --- | --- | --- |
| compressed | 1 | 0.13 | 152.4 | ok |
| compressed | 2 | 0.09 | 227.7 | ok |
| compressed | 3 | 0.08 | 236.4 | ok |
| stored | 1 | 0.07 | 276.5 | ok |
| stored | 2 | 0.06 | 346.9 | ok |
| stored | 3 | 0.07 | 277.9 | ok |

## 结论
- 中位数：compressed 227.7 MB/s, stored 277.9 MB/s
- ≥10 MB/s：Python/TLS/zlib/磁盘不是瓶颈。速度问题在空口调度、ACK 与 USB 帧率（Q8–Q10）。
- 客户端与服务端同进程，测得的数字是下限；若结论接近阈值，改用独立进程客户端复核。

## 补充：调试日志不是开销

- `--trace`（receiver DEBUG 全开）复测 20 MB compressed：**274.2 / 259.8 MB/s**，日志文件仅 2.6 KB。
- 原始记录：`evidence/bench-receiver-trace.md`。
- 量级对照：1.05 MB/s 的手动基线只用到这里约 0.45%；即使做到 5 MB/s，也只占约 2%。
- 因此打印日志、TLS、zlib、cpio 解包、磁盘写入都不是当前速度上限的原因；不要再为“换语言/去 Python”投入。
