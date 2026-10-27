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
- 手动链路（不带 Python mDNS）下，`awdl_netd` 日志：`native mDNS ifindex=0 join=-1`，即 lwIP multicast join 失败；
- iPhone 隔空投送看不到设备；
- 恢复 `AWDL_NETD_MDNS_PROXY=1` + Python `mdns.py` 后可见。

下一步：先修正 lwIP multicast join（`ipv6_mreq.ifindex`/`netif_get_by_index`），再对比 DNS 报文；另外确认 lwIP IPv6 的 hop limit 是否 255（mDNS 要求 255，Python 显式设置）。

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
