# 发现延迟 / Discovery

目标：打开 iPhone 隔空投送列表时，AirBridge32 尽快出现，并尽量一直保留。

## 已做

1. **稳定实例 ID**
   - 上游 `receiver.py` 每次启动用 `secrets.token_hex(6)`，iPhone 会把每次启动当成新设备；
   - 现在 `stable_service_id(address)` 从 AWDL IPv6 的 interface identifier 派生 12 位十六进制；
   - 同一网卡/地址下，重启后 ID 不变（实测两次都是 `8e2c9afffe41`）。

2. **更快公告**
   - `ANNOUNCE_PERIOD_S`：2.0 → 0.5 秒；
   - 启动时连续发 3 次（间隔 0.1 秒）；
   - 收到查询时连续回复 2 次；
   - `MIN_REPLY_GAP_S`：0.1 → 0.02 秒。

3. **常驻**
   - 关闭窗口 = 最小化，radio/receiver 不重启；
   - 避免每次打开 app 都重新走发现。

4. **上传期间暂停 mDNS**
   - 空闲时 0.5 秒公告；
   - `/Upload` 开始后 `TRANSFER_BUSY` 置位，mDNS 线程完全暂停；
   - 上传结束/失败后恢复；
   - 之前每 0.5 秒公告会挤占 AWDL 时隙，6 MB 传输只有 ~40 KB/s；暂停后 1.16 MB 用 5.4 秒（~215 KB/s），体感明显恢复。

## 实测反馈

- 用户反馈：比之前快，但还没有到“一打开就在”；
- 说明剩余延迟不完全由公告频率/实例 ID 决定。

## 剩余可能原因

- iPhone 打开 AirDrop 面板时才开始一次新的 AWDL/BLE 扫描，扫描本身需要数秒；
- AirDrop 发现不只依赖 mDNS，还可能依赖 BLE 广播；当前只做 AWDL mDNS；
- `awdl_netd` 的 UDP mDNS 代理增加了一小段转发延迟；
- iPhone 可能缓存旧实例（之前每次随机 ID）需要几次重新发现。

## 下一步

1. 在 `ProxyAnnouncer` 记录每次收到查询的时间，和 app 启动/用户打开列表的时间对比：
   - 如果查询在打开列表后 2–3 秒才到，延迟在 iPhone 扫描，不在我们；
   - 如果查询立刻到但回复慢，修代理/ responder。
2. 完成原生 mDNS（先修 lwIP multicast join），去掉 UDP 代理；
3. 评估 BLE 广播是否必要（需要 RTL8821CU 蓝牙 MI_00，工作量和风险都更大）；
4. 目标：稳定在 1–2 秒内出现，而不是承诺 0 秒。
