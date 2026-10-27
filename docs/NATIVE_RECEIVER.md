# 原生 receiver 迁移

目标：把 `receiver.py` 的 mDNS、TLS、`/Discover`、`/Ask`、`/Upload` 直接做进
`awdl_netd`，去掉 Python 和本地 8771 TCP 跳。

## M1：原生 mDNS

状态：**已实现 C++ responder，但 iPhone 还不接受它的记录；默认仍走 Python mDNS。**

- 代码：`src/awdl_netd/native_mdns.cpp`；
- 开关：`AWDL_NETD_NATIVE_MDNS=1`；
- 记录集按 `mdns.py` 构造：PTR / SRV / TXT / AAAA / NSEC，端口 8771；
- 每 2 秒向 `ff02::fb:5353` 发一次；`lwip_recvfrom` 非阻塞，查询也会触发回复。

实测：

- `awdld` 的 `tx data` 每 2 秒增长，说明包确实发出；
- iPhone 隔空投送看不到设备；
- 恢复 `AWDL_NETD_MDNS_PROXY=1` + Python `mdns.py` 后可见。

下一步：抓取原生 responder 的 DNS 报文，与 `mdns.py` 的 `Service.response()` 逐字节对比，重点检查
DNS 压缩指针、NSEC bitmap、以及 lwIP 的 multicast scope/source；通过后再切换默认。

## M2：原生 TLS + `/Discover`

`/Discover` 返回 binary plist：

```text
ReceiverMediaCapabilities = data(b'{"Version": 1}')
ReceiverComputerName      = AirBridge32
ReceiverModelName         = MacBookPro18,3
```

需要 OpenSSL TLS server（BIO 接到 lwIP socket）+ HTTP/1.1。完成后再做 `/Ask`、`/Upload`。

## M3：去 Python、打包

- `awdl_netd` 直接监听 8771；
- 删除本地 TCP 代理和 Python receiver；
- 便携 ZIP 回到 ~20–25 MB；
- 目标 3 MB / 3 秒，实测为准。
