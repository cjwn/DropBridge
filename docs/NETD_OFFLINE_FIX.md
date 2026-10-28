# Q28：无需手机的 netd 修复与故障验证

2026-10-28；基于 `e14259a`。用户明确不希望频繁用手机重发文件，本轮
只做本机源码修复和回环故障注入，不启动 radio/awdld、不发无线帧。

## 修了什么

1. **IPC 发送失败不再伪报成功。** `netif_output_ip6` 检查复制及发送
   结果，分别返回 `ERR_BUF` / `ERR_IF`，成功后才累计发送帧与字节。
2. **建立后的 IPC 断开不再假活。** EOF、接收错误、非法帧长度、发送
   错误会标记链路失败并关闭两方向；主线程输出诊断、以代码 2 退出。
   既有监管因此能看到进程失败。首次连接尚未来、正常空闲不会误报。
   不声称整链自动恢复耗时已达标，radio 重初始化仍可能很慢。
3. **输入丢弃可观测，缓冲区按所有权释放。** 分开记录真实 PBUF_POOL
   分配失败、复制失败、lwIP 投递失败；失败释放 pbuf，成功由 lwIP 接管。
   没有改成无界缓存或隐式重试，实际资源耗尽原因仍需看统计。
4. **回程无进展不无限等待。** 本机 IPC socket 设置 5000 ms 发送超时；
   这是 socket send 无进展约束，不是整次手机上传总时限，也不是每个
   完整帧绝对 5 s 上限。部分写失败后关闭整条流，不能从新帧继续发送。

主线程约每 5 秒输出累计 `ipc stats`，使用单调时钟：
`rx_seen/rx_bytes/rx_queued/alloc_fail/copy_fail/input_fail/tx_frames/tx_bytes/tx_fail`。
`rx_queued` 只表示成功投递给 lwIP，不证明 TCP 接受或文件内容已到达。
这些计数不是同一时刻的事务快照，不应拿它们代替逐连接 seq/ACK/窗口。

没有修改 TCP 合并、信道、无线功率/共存、驱动、系统网络和接收协议。
`-j 400` 仍是默认。没有添加外部依赖。

## 已验证的事实

### 真实 socket / lwIP 池检查

`netd_ipc_test` 直接调用本次生产函数，使用真实回环 socket 和真实 lwIP
缓冲池（不是用假的计数模拟通过）：

- 耗尽池后分配失败准确计数；恢复池后继续接收。
- 成功 100 次、投递拒绝 100 次后，池容量不减少，失败路径没有泄漏。
- 正常输出的两字节长度头和原始载荷逐字节一致。
- 人为不完整 pbuf 不能输出垃圾；关闭发送方向后实际发送失败返回 ERR_IF。
- 对端不读取数据时，测试用 100 ms send 超时触发失败，未无限堵塞。
  生产配置是 5000 ms，不能把测试的 100 ms 当成生产恢复时限。
- 头/体分片和连续帧，包含 60,000 字节输入，最终载荷完全一致。

### 真实进程检查

`tools/test_netd_process.py` 只连接本机 `127.0.0.1:49877`，确认它启动的
子进程已持有端口后才连接，不向现有链路或设备发送数据。用模拟 hello
和 IPv6 测试包覆盖五类情形：

- 健康空闲，能持续存活并输出心跳；随后 EOF 退出 2。
- 延迟首次连接仍可工作；随后 EOF 退出 2。
- 半包断开记录 `pending_bytes=10`，不当完整包投递。
- 非法长度明确失败退出。
- 完整 IPv6 包分片送达后才计一次输入，随后 EOF 退出。

最终 CTest **2/2 通过**，其中第二项包含上述五个真实进程场景。
本机记录中 EOF 后观察到退出约 0–0.1 s；这是测试结果，不是实时保证。

### 对旧版的相同故障对照

对冻结后端 `dist/app/20261028-143130/backend/awdl_netd.exe` 做同一个
`--only-eof` 测试：连接结束后它仍存活超过 3 s，测试按预期失败并只清理
自己创建的子进程。**“旧版 IPC 断开后假活”已经在本机复现，不再只是
静态猜测。** 但尚不知道历史 iPhone 断流时是否触发了这个缺口。

## 证据与构建

- `evidence/netd-offline-2026-10-28.log`：最终通过记录。
  CTest 原生日志头使用 Windows 系统编码；提交副本已转 UTF-8，原始字节
  保留 `build/diagnostics/netd-offline/final-ctest.raw.log`。
- `evidence/netd-old-eof-control-2026-10-28.log`：旧版预期失败对照，
  不能误读成新版回归失败。
- `evidence/netd-offline-fix-2026-10-28.json`：源码/二进制及证据哈希。
- 开发时两次测试夹具修正的失败日志保留在
  `build/diagnostics/netd-offline/first-test-missing-core-init.log` 和
  `second-test-fixed-sleep-fixture.log`：分别补齐真实 lwIP core 初始化、
  将固定睡眠改为带截止时间的事件等待；不删除失败历史。

```powershell
$cmake = 'D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S src/awdl_netd -B build/awdl_netd -G 'Visual Studio 17 2022' -A x64
& $cmake --build build/awdl_netd --config Release --parallel 8 --target awdl_netd netd_ipc_test
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir build/awdl_netd -C Release --output-on-failure
```

生产修复二进制在 `build/awdl_netd/Release/awdl_netd.exe`。
原测试包 `dist/app/20261028-143130` 与单文件外壳均未替换/重打包，保留为
历史对照；直接双击旧包不会自动使用新 netd。后续集成应记录包版本与哈希。

## 仍未证明或未处理

- 不宣称已经解决时快时慢、iPhone 断流或空口 ACK 问题；本轮无手机测试。
- 没有完整的 TCP seq/ACK/窗口及重复/乱序观测，Q25 只补齐了底层计数。
- TCP 代理的阻塞连接清理、上游 AWDL 的回程排队和合包提交时机，仍是
  后续可在本机继续排查的方向；不能把这次修复扩大解释为它们也已解决。
- 5000 ms 本机发送无进展会触发进程失败，可能触发现有整链重启；
  极端回压下的手机体验尚未实测。更换 driver/恢复蓝牙不属于本项。
