# Q29：TCP 代理异常清理与独立测试包

2026-10-28；基于 `58ffe00` 的本机观测/负对照。没有手机、radio/awdld 或
设备实验。此处修复 TCP 代理连接；Q28 修的是另一条 awdld IPC 连接。

## 修复与边界

1. 到本地 receiver 的 Windows socket 设置 5000 ms 发送无进展超时。
   对端长期不读不再无限卡住；超时会终止该连接，不能无限缓存或重试一个
   可能已经部分写入的 TLS 字节流。计数只累计 send 成功返回的字节，失败
   调用可能已经写出的未知前缀不被伪报成成功。
2. 两个代理线程共享退出标记。Winsock 和 lwIP 接收调用均设 1 s 超时，
   超时只是检查标记，健康空闲继续等待；不把空闲 1 s 当成传输失败。
3. TCP RST/接收错误/本地发送失败会通知另一方向退出。正常远端 FIN 仍
   只半关闭本地发送方向，允许 receiver 读取完整请求后返回响应。
4. 本地接收 EOF/错误后先通知并唤醒另一线程，再 join；等两个线程退出
   后才 shutdown/close lwIP socket，避免依赖当前未启用并发关闭支持的
   lwIP 配置去唤醒另一个正在 recv 的线程。Windows handle 同样保留到
   join 完成，不在另一线程阻塞调用中直接关闭、重用 handle。

新增发送超时只针对本地 receiver socket；Q28 的 awdld IPC 发送超时仍是
独立的 5 s。没有更改 TCP 合并、接收窗口/池容量、信道、功率、共存、驱动
或系统网络。没有开启新的 lwIP 全双工选项，也没有修改依赖源码。

这是故障连接清理，不是文件续传或重试；超过发送无进展限制的极端本地
回压可能使当前上传失败，实际手机体验待测。未处理/证明所有 lwIP 回程
发送阻塞、上游合包调度或无线层问题，不承诺整链 ≤10 s 自动恢复。

## 验证事实

最终 CTest **5/5 通过**：IPC 真池/回环、TCP 元数据解析、五种 IPC 进程
情形、TCP 往返、TCP 故障情形。后两项经真实 IPv6/TCP checksum/握手进入
实际 lwIP 并代理到 Windows socket，不是伪造观测计数。

- 65,536 B 上传 / 3,400 B 响应逐字节相同；本地接收端延迟 2.2 s，跨过
  两个接收轮询周期仍正常；远端上传 FIN 后仍能收到完整响应并清理。
- 接收端不读 + 远端 RST：观测到 `local_send_failed error=10060`，随后
  `active=0 / up_phase=local_send_error / down_phase=cancelled`。
  同一场景在保留的修复前构建中，RST 后 **9.235 s** 的新快照仍 active。
- 空闲远端 RST：两个代理线程退出，netd 存活。
- 本地 receiver RST：两个代理线程退出，netd 存活。

日志按 5 s 采样，不能把某条 `active=0` 的快照时刻直接当精确清理延迟；
5 s 也是 send 无进展约束，不是整个请求的绝对截止时间。

首轮仅增加发送超时及 shutdown 时，对侧 recv 仍阻塞；第二轮补本地 recv
轮询后，回压和远端 RST 通过，但本地 receiver RST 暴露 lwIP recv 未退出。
最终补 lwIP recv 轮询并把关闭移至 join 后，通过全部场景。失败日志保留
`build/diagnostics/proxy-cleanup/{first,second}-ctest.log`，未删除。

## 独立内部测试目录（尚未启动）

`build/diagnostics/proxy-cleanup/test-package/` 从冻结包
`dist/app/20261028-143130/backend/` 复制后，只替换 `backend/awdl_netd.exe`。
**334 个后端文件逐个哈希比较，文件集合相同，仅 netd 不同。** 原包及原单文件
exe 保留，未重打包覆盖。完整文件清单在同目录上一级的 `package-manifest.json`。

新 netd SHA-256：
`06264244435625bb24ce9e613e99fdc87264b54c1ab05234fc6da08d8acd4a3b`。

实际入口使用现有 Q27 监管 `build/supervisor/Release/AirBridge32.exe`，
不是测试目录里那个较旧的监管副本；后端与监管版本均已记入清单。
两个 `--check` 计划都通过：fixed 149/20，合并开启 `-j 400` 和关闭 `-j 0`，
路径完整，不启动子进程。离线计划检查会创建会话日志目录，原密钥/接收文件
未覆盖。外层旧单文件 exe 不会自动使用这些新组件。

首次真机对照应沿用**最近 Q27 的 fixed 149/20、合并关闭 `-j 0`**，只改变
netd；不能同时改回默认合并开启后，把速度变化全归给修复。
本机检查过的手动入口（本轮**未执行**，执行会初始化 USB 并发 AWDL）：

```text
build/diagnostics/proxy-cleanup/test-package/Start-Q29-Test.cmd
```

它复用现有 `dist/data` 的证书/实例 ID/接收路径；真机前先只读检查绑定、
无旧链路/端口冲突，记录初态与时间。链路 ready 后再让用户发同一测试文件，
先核对一次结果与新 TCP/代理计数，失败则保留而不叠加恢复；结果可信后再
决定是否重复或单独对照 `-j 400`。本轮不要求手机配合。

## 证据与复现

- `evidence/tcp-proxy-cleanup-2026-10-28.log`：最终 CTest 5/5。
- `evidence/tcp-proxy-cleanup-cases-2026-10-28.log`：三个故障场景完整输出。
- `evidence/tcp-proxy-package-2026-10-28.log`：文件比较与两个离线计划检查。
- `evidence/tcp-proxy-cleanup-2026-10-28.json`：源码/证据/构建/测试包哈希。
- 修复前对照及其二进制见 [TCP 观测](TCP_DIAGNOSTICS.md)。

```powershell
& 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/awdl_netd -C Release --output-on-failure
python tools/test_netd_tcp.py build/awdl_netd/Release/awdl_netd.exe --case faults
```

Q29 本机修复/回归完成；Q25 真机定位、Q20 连续传输、Q7 整链恢复验收仍未
完成。本机成功不代表历史 iPhone 断流已经修复，也不代表真实吞吐提升。
