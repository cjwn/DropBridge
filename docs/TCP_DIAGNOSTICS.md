# Q25：netd 逐连接 TCP 与代理边界观测

2026-10-28；基于 `e4e34dc`，只做本机回环，无 radio/awdld/手机实验。

## 观测范围与判读

`src/awdl_netd/tcp_diag.*` 自行实现有界 IPv6/TCP 元数据解析，不复制上游
实现、不添加依赖、不保存载荷或设备 IPv6 地址。最多保留 32 个连接四元组，
按进程内 `flow` 编号关联；活跃代理不会被淘汰。旧的非活跃行按最近活动时间
淘汰，`evicted` 计数；全满时 `capacity_skips` 明示观测缺失。

- `tcp dir=rx`：来自 awdld IPC 的原始 IPv6/TCP。`accepted` 仅指投递给
  lwIP 输入队列成功；`failed` 为本机分配、复制或投递失败。它发生在 TCP
  校验与接收之前，不证明 checksum 正确、TCP 接受或接收程序已消费。
- `tcp dir=tx`：lwIP 回程输出，`accepted` 仅指完整 IPC 写成功，`failed`
  为该输出失败；不证明 awdld/无线发送成功、手机收到或 802.11 ACK 正常。
- 每方向记录包数、载荷字节（**含重复，不能算文件大小**）、最近 seq/ACK、
  原始 16 位窗口 `win_raw`、SYN 窗口缩放提议 `ws_offer`（缺失为 -1）、
  SYN/FIN/RST/零窗口计数、最后载荷及 ACK 高水位推进的空闲毫秒。
  缩放仅为提议，尚未推定双方协商成立或计算有效窗口；SYN 自身窗口不缩放。
- `gap`/`overlap` 是相对序号高水位的跳跃/重叠迹象，处理 32 位回绕；
  后到补洞也算重叠，**不直接等同丢包率或重传次数**。`repeat_ack` 仅指
  相邻无载荷 ACK 的 ACK 值与窗口均相同，不直接等同拥塞触发的重复 ACK。
- `proxy`：同一 flow 的 `up_read`（lwIP 读出）、`up_written`（Windows
  socket send 接受）、`down_read`（Windows socket 读出）、`down_written`
  （lwIP send 接受）累计值，以及当前阻塞调用阶段。send 接受不等于对端
  应用读取。部分写按每次实际返回字节累计，失败后保留已写前缀。

复用原有 5 秒心跳及进程失败前最终输出；静止的非活跃行只输出最后一次，
活跃代理持续输出以呈现无进展。`t_ms` 为单调时钟，不是 UTC；真实会话应
与现有监管时间戳对齐。只复制计数快照后打印，不持观测锁执行 socket I/O。
这不是跨线程事务快照，短暂交错不应被解读成数据丢失。

IPv6 扩展头按包长有界遍历，TCP 头与选项严格检查长度；分片不重组、ESP
及 jumbogram 不解析，分别计数，不能把未覆盖流量当成无 TCP。未校验 TCP
checksum，未窥视 lwIP PCB 内部接收窗口/拥塞窗口，也未观测 awdld 内部合包
排队时间。诊断开销的真实空口影响尚未测量。

## 本机验证与新发现

CTest 4/4：原 IPC 池/回环测试、五种进程故障情形、新元数据解析测试、真实
lwIP TCP 往返。解析覆盖截断/坏头长/坏选项、IPv6 扩展、分片/ESP、缩放、
序号回绕、重叠/跳跃、零窗口、重复 ACK、投递失败分离、四个代理计数和容量。

往返测试不是模拟计数：Python 标准库构造有效 IPv6/TCP（含 checksum），
经 127.0.0.1 IPC 进入真实 lwIP 握手，再代理到真正的本地接收 socket。
65,536 字节上传、3,400 字节返回、延迟读、上传半关闭后返回响应均通过，
内容逐字节一致，上传 SHA-256 为
`7daca2095d0438260fa849183dfc67faa459fdf4936e1bc91eec6b281b27e4c2`。
这不包含 TLS、AirDrop、合包、USB 或空口，不能作性能基线。

负对照实测：本地接收 socket 不读，代理停在 `local_send`，回程 ACK 停顿，
**通告窗口仍非零，IPC 输入可成功排队**。随后远端 TCP RST 被观测到，
下一轮超过 6 秒后的快照仍 `active=1 / up_phase=local_send / down_phase=local_recv`。
原因边界：代理 Winsock send 没有超时；远端 RST 不能唤醒堵在另一条 Windows
socket 上的发送；异常一侧只半关闭另一 socket，未保证唤醒双向阻塞调用。
此前实际 iPhone 断流是否发生同一路径仍未知。后续清理由 Q29 单独修复。

负对照日志末尾的 `active=0` 发生在测试断开 IPC/本地 socket 的**清理阶段**，
不能当成该版本能自恢复；`EXPECTED DEFECT` 标记在测试主动清理之前。
不能把非零窗口下的停顿直接归因于射频或窗口耗尽；lwIP 接收邮箱回压等
内部原因仍需另证。本轮没有改池容量/窗口、合包、信道或无线参数。

## 复现与证据

```powershell
& 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/awdl_netd -C Release --output-on-failure
python tools/test_netd_tcp.py build/awdl_netd/Release/awdl_netd.exe --case roundtrip
# 仅对保留的修复前构建；预期观测到缺陷后测试返回成功：
python tools/test_netd_tcp.py build/diagnostics/tcp-diag/netd-before-cleanup.exe --case pressure --expect-stuck
```

测试必须先由自身子进程确认持有 IPC 端口，再连接它；本地接收端独占绑定
8771，冲突即退出。只清理自己创建的进程；不接触现有无线链路。

- `evidence/tcp-diag-offline-2026-10-28.log`：CTest 4/4。
- `evidence/tcp-proxy-pre-fix-2026-10-28.log`：可重复回压/RST 负对照。
- `evidence/tcp-diag-offline-2026-10-28.json`：源码、二进制和证据哈希。
- 原始会话与首次夹具失败保留 `build/diagnostics/tcp-diag/`。第一次逐包等待
  触发延迟 ACK 导致测试端限速；第二次无界突发挤爆输入队列；之后限制测试端
  在途突发，并去掉“回压必然出现零窗口”的错误前提，未调整生产 lwIP 参数。

Q25 的本机观测与离线验证已完成，真实 iPhone 断流定位未完成。开发二进制
仅在 build；`dist/app/20261028-143130` 和现有单文件包未替换。
