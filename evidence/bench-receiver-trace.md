# Receiver 离线吞吐台（Q1）

- 时间：2026-10-27 23:25:11 +0800
- 机器：Windows-11-10.0.22631-SP0 / Python 3.12.10
- 负载：20 MB 随机字节，cpio + DVZip，128 KB 分块经 TLS 上传
- 计时：服务端 `/Upload` 开始到 `stored`（不含客户端收尾）
- 不包含：awdl_netd 的 lwIP 代理与 AWDL 空口

| 模式 | 运行 | 秒 | MB/s | SHA-256 |
| --- | --- | --- | --- | --- |
| compressed | 1 | 0.07 | 274.2 | ok |
| compressed | 2 | 0.08 | 259.8 | ok |

## 结论
- 中位数：compressed 267.0 MB/s
- ≥10 MB/s：Python/TLS/zlib/磁盘不是瓶颈。速度问题在空口调度、ACK 与 USB 帧率（Q8–Q10）。
- 客户端与服务端同进程，测得的数字是下限；若结论接近阈值，改用独立进程客户端复核。
