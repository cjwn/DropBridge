# DropBridge / AirBridge32：给下一位 AI 的完整交接

更新：2026-10-28，Asia/Hong_Kong（UTC+8）。
代码与实验基线：本地 `main`，`991d2f4`。本交接自身的文档提交在其后。
工作目录：`J:\New soft project\DropBridge`。

**先读结论：已经实现 Windows 原生接收 iPhone 隔空投送的真实文件闭环，
但连续传输会低速或中断，发现也不稳定。历史最好约 1.12 MB/s，不能作为
当前稳定速度。根因未定，稳定性验收未通过。实验链路目前已经停止。**

本页集中整理事实、推断、未知、代码位置和下一步；无须依赖前一段聊天。
任务状态仍以 [docs/WORK_QUEUE.md](docs/WORK_QUEUE.md) 为唯一队列入口。

## 1. 用户目标与工作约定

- 用现有 RTL8821CU USB 网卡，让 iPhone 系统原生隔空投送把文件交给
  Windows 11；不要求 iPhone 安装应用，不使用 WSL2/Linux VM 作为最终
  运行依赖，不购买替代网卡。
- 用户侧一个 exe + 托盘即可，内部允许多进程。所有数据留在 exe 所在
  文件夹内；保留原件、失败日志和可复现实验。
- 用户明确要求：参考已有实现排障；对比以前正常与现在异常的状态；
  特别关注蓝牙异常是否有关。先回答判断，再按授权操作，不用猜测代替事实。
- 每完成一项可独立检查的改动，验证、更新队列/证据、做中文本地 Git
  提交。**不自动推送**；不覆盖、重置或夹带用户改动。
- 读取 `AGENTS.md`、README、PHASE0_AUDIT、PLAN、FINAL_PLAN、WORK_QUEUE
  和 WORK_LOG。早期文档有旧状态，按时间与实测证据判断，不能照旧方案重做。
- 分清源码实现、上游报告、本地编译、硬件测试、真实文件传输五种证据。
  队列里的“实验记录完成”不等于“产品验收通过”。

### 授权与本次交接范围

- 队列记录 2026-10-27 23:37 已授权初始化、AWDL 同步、速度/时隙测量；
  本轮用户也明确配合了 A/B 尝试。不要再次把这些已授权事项当成完全未授权。
- 换驱动、改系统网络/防火墙仍没有授权。不要运行上游安装器或发射脚本。
- 不要因为历史有人运行过 `pnputil`，就把它当成可以重复执行的恢复手段。
  驱动/复合设备恢复必须先明确接口、影响和恢复办法，遵守用户授权边界。
- 用户不接受依赖反复物理拔插解决问题；系统重启或任何影响整机的恢复
  需要与用户协调。不要自动做来“改善实验初态”。
- 本次新请求仅整理交接文档；没有追加无线实验、设备复位或功能改动。

## 2. 当前实物与交接时状态

- VID:PID `0bda:c820`，同一 USB 复合设备：MI_02 Wi-Fi、MI_00 蓝牙。
- 芯片寄存器 `SYS_CFG2 0x00FC = 0x09` 已确认 RTL8821C，初始化记录
  `cut=4 (UMC), 1T1R, rfe_type=0x22`。不能只靠商品型号确认芯片。
- MI_02 已从厂商 `netrtwlanu.inf / RtlWlanu` 绑定为
  `WinUSB / oem155.inf / libwdi 6.1.7600.16385`。
- 本次交接只读复查：MI_02 `OK / problem 0 / WinUSB`；MI_00 `Error`。
  **蓝牙 Error 与用户正常关闭蓝牙开关不是同一件事。**
- 原蓝牙异常记录为 Problem 31 / `CM_PROB_FAILED_ADD`；最新检查脚本
  只再次显示 Error，没有在本轮重新读取并证明仍是同一 problem code。
- 项目 supervisor/radio/bridge/netd/awdld 进程目前没有残留；两组实验的
  receiver 也已停止。不要把没有发现接收端误认为后台目前仍应在运行。
- WinUSB 回滚演练、蓝牙成功恢复、完整 24 h 验收均没有完成。

硬件底座已有证据：只读 open/claim、芯片 ID、固件 bring-up、2.4/5 GHz
被动 RX；2.4 GHz TX 曾由 ESP32-S3 独立监听见证，三个完整 reset 轮次各
20/20 唯一序号。ESP32-S3 不能见证 5 GHz；USB 提交成功但空中零帧也曾
出现。首个真实 AirDrop 是 2,911,531 字节 PNG，证据
`evidence/airdrop-first-transfer.txt`；这些历史通过不能替代当前连续验收。

## 3. 实际架构与交付形态

```text
iPhone 原生 AirDrop
  -> RTL8821CU / devourer / libusb
  -> airbridge_radio.exe       TCP 127.0.0.1:49876，radio_link v3
  -> radio_pipe_bridge.exe    TCP <-> 命名管道
  -> awdld.exe                WinAWDL + OWL，AWDL 控制面/IPv6
  -> awdl_netd.exe            IPv6 IPC 127.0.0.1:49877，lwIP
  -> Python receiver.py       本地 TLS/HTTP 8771，DVZip/cpio
  -> data/received
```

- C#/.NET Framework 4.8 监管（系统自带运行库）：拉起、托盘、单实例、
  轮转日志、通知、看门狗。Dart/Flutter 已删除，不要重新引入。
- `radio` 为 GPL-2.0 路线，`awdld` 为 GPL-3.0 路线，其余按现有许可
  边界维护；不要直接合并成单进程。许可证细项见 LICENSE_MATRIX。
- 当前运行路径使用用户态 lwIP，不依赖 Wintun 适配器/Windows IPv6 路由；
  旧 WinAWDL 构建脚本仍检查 Wintun DLL，构建依赖不等于运行数据路径。
- mDNS：receiver 经本地 UDP `127.0.0.1:15353` 代理到 AWDL 组播，上传
  时暂停公告。原生 mDNS 已有候选实现，但 Q12 目前非必需。
- 外层 `dist/AirBridge32.exe` 是约 16 MB 自解压入口：

```text
<程序所在文件夹>/
  AirBridge32.exe
  app/<构建时间戳>/backend/...
  data/keys/                  证书与持久化 airdrop-service-id
  data/received/              接收文件
  data/logs/<会话>/           分进程日志
  data/awdl_netd.addr
```

外层 exe 目前**不转发 `--chanseq` 诊断参数**。最新实验使用新监管配合
原后端，不是重新打包后双击外层 exe。不要误以为现存外层包包含最新监管。

## 4. 当前配置与容易踩的坑

- 默认 `149 / 20 MHz`；40 MHz 多次更慢且不稳定，当前不用，80 MHz
  不作为本轮方向。
- 主控：`AWDL_ELECTION_METRIC=200`、`AWDL_ELECTION_COUNTER=100000`。
  OWL 先比较 counter；之前只提高 metric 仍会被 iPhone 抢主控。
- `awdld` 当前参数：`-c 149 -s 2 -j 400 -v`，join 等待值单位为微秒。
- 监管新增 `--chanseq social|fixed`，默认 social；手动脚本对应
  `-ChannelSequence social|fixed`。
- social：本端公告 slot 8 为信道 6，其余主信道。fixed：本端全 149。
  **本端这样公告，不等于手机已经采纳；必须检查手机实际公告的序列。**
- `patches/owl-airbridge.patch` 的开关判断是 `getenv(...) != NULL`。
  fixed 必须真正移除 `AWDL_CHANSEQ`；设成 `fixed`、`0` 不能关闭。
  新监管及脚本两种模式都移除继承的 `AWDL_FOLLOW_MASTER_SEQ`，防止污染。
- radio：`AIRBRIDGE_FASTRETUNE_FW=2`、`AIRBRIDGE_LINK_STATS=1`；
  20 ms 有界等待 C2H，然后 RF18 回读确认信道。无法确认会断开链路。
  跨频段完成后恢复功率表/Wi-Fi-only 配置，更新温度补偿信道上下文。
- 旧的“0.22–0.28 ms 快切”只是命令提交耗时，不能当真实空口换台完成。
- `radio tx failed=0` 只证明主机提交未报错，不证明对端收到或 802.11 ACK。
  TCP ACK 与 802.11 硬件 ACK 还必须分开。
- `joined` 是 WinAWDL TCP 合并器追加的段数，不是全部有效上传帧数。
  radio 少量数据帧也不等于 receiver 仍收到文件内容。
- RSSI、thermal 是芯片/驱动原始指标，不能当 dBm/摄氏度；
  `rssi_n=0` 时的 RSSI=0 不表示信号变成零。Q18 之前的统计口径也有污染。

## 5. 正常、低速、断流和蓝牙异常的时间线

除注明外，文件为同一视频，落盘大小 **29,456,959 字节**。
MB 使用 10^6 字节；传输体字节与最终文件字节不同。

### 蓝牙异常之前：已经存在速度方差，甚至一次断流

- 01:19：约 81.2 s / 363 KB/s。
- 01:40：30.37 s / 970 KB/s；01:34 启动曾经历多次固件失败和重试。
- 01:44：117.46 s / 251 KB/s；01:51：30.95 s / 952 KB/s。
- 12:52:29.218–12:52:55.482：26.26 s / **1121 KB/s**，counter 修正后
  的历史最好结果。证据 `evidence/transfer-970kbs.log`。
- 13:42:44 起的 social + 固件快切实验，统计一度约 930 帧/s，随后
  13:43:30 HTTP 408（约 8,425,846 传输体字节 / 47.5 s）。
  证据 `evidence/awdl-sequence-and-fastretune.log`。旧统计和根因解释需复核。

**关键：约 13:47 才发生下面的蓝牙异常。低速和上述一次断流都早于它。**
因此蓝牙异常值得检查，但不能当作全部历史低速/断流的唯一解释；历史峰值
也不是相同射频/软件初态下的受控基线。没有峰值当时的蓝牙开关/驱动状态
同步快照，不能写成“最高速已证明蓝牙开启正常”。

### 约 13:47：软件 USB 复位后出现蓝牙 Error

- 历史运行了针对 MI_02 的 `pnputil /restart-device`。Wi-Fi 仍能
  open/claim，MI_00 出现 Problem 31。记录提交 `271faf4`（13:50）。
- 历史试过蓝牙 restart、disable/enable、remove+scan、重启 bthserv 等，
  都未记录成功恢复。见 `evidence/software-usb-reset.log`。
- 此摘要关于“复合设备重启是否尝试”的表格与后文存在表述不一致，
  不应照搬其“正确恢复方式”当成已验证方案。
- “重启系统或完整冷重枚举能恢复”是旧文档建议，本轮未验证恢复成功。
  不要写成已证明的唯一恢复法，也不要把“Error 不影响 AirDrop”当定论。

### 14:19–14:45：确定的代码修复与一次三轮验收

- `1db868f`：管理/Null/C2H 不混入上传统计，有效 PHY/RSSI 分母、真实
  采样间隔、并发计数、温度监测异常修复。
- `d007eee`：换台完成确认、RF 回读、跨频段功率/天线恢复与温度上下文。
  编译、两项 C++ 离线测试及补丁复现通过；不代表性能问题已修好。
- 会话 `20261028-143206`，social / 149/20 / 主控：
  第一次 75.25 s、0.39145 MB/s 完成；第二次 77.45 s、0.38034 MB/s 完成；
  第三次 62.5 s 后 408，约 10,212,587 传输体字节，最后 30 s 无新内容。
- 整会话 1324 次换台确认中位 1.559 ms、最大 33.339 ms；
  **第三次失败上传窗口内最大仅 2.175 ms、120 次确认**。
  不能把全会话峰值直接归为这次断流原因，也没有独立空口完成时序证明。
- 第三次断流时温度约 `29/+3`；主控未被抢，控制面仍在，上传数据停顿。
  这不证明手机停止空口发送，也不证明硬件/TCP ACK 正常。
- `0c2f028` 保存未通过结论；随后用户要求继续排障。

### 15:03–15:16：A 组 fixed（最新完整文件与断流）

- 会话 `20261028-150311`；新监管 `0741be7`，原后端包
  `dist/app/20261028-143130`，相同证书/实例 ID/接收目录。
- 启动约 120 s：15:03:11 启动，固件 ready 失败一次后已有路径重试，
  15:05:11 radio ready。没有额外 pnputil/Hub 复位。
- 第一次 15:07:18.833–15:08:49.969，**91.136 s、0.32322 MB/s**，
  完整落盘 `dist/data/received/7545-4.MP4`。
- 第二次 15:10:30.267–15:15:54.080，**323.813 s 后 HTTP 408**；
  15,982,835 传输体字节，没有完成文件，最后 30 s 无内容。
- 全组零换台，温度有效读数 `28/+2`。两次之间没有重启或拔插。
  手机仍公告包含 36/149/6 的稀疏序列，没有证据表明采纳全 149。
- 第二次失败后按停止条件不做第三次。空窗中仍有少量 RX/TX data，
  action RX 增加 53，joined 不增加；这些帧究竟是什么尚未解析。
- 结论仅为“**关闭当前跳频不足以解决**”。没有统一物理冷启动初态，
  仍可能继承更早的射频状态，不能据此彻底排除跨频段的历史影响。

### 15:16 之后：B 组 social（未能发现接收端）

- 会话 `20261028-151652`，相同监管与后端，只改 social。
- 启动约 43 s，15:17:35 ready。我们仍是主控，能看到手机邻居，
  记录 264 次切入 6 的换台。
- 用户报告列表没有 AirBridge32；关闭重开分享面板后仍没有。
  receiver 零 `/Discover`、零 `/Upload`。
- 这是**发现失败**，没有上传尝试，不能记成上传失败或给出吞吐。
- 两组启动重试/射频初态不同，且 B 没有上传，**未完成有效三次对三次 A/B**。
- 已保存现场并停止链路，没有通过自动复位或反复发文件强行凑成功结果。

### 文件完整性边界

上述多次成功接收文件 SHA-256 相同：

```text
5d057f9c5bad719872cf316a9b6935fc965b6ff80ecc65ec5fe82ef768c73c54
```

尚未取得手机源文件哈希，只能证明接收副本相互一致，不能写“两端哈希已核对”。

## 6. 当前判断：事实、假设与未知

**事实：**真实 AirDrop 曾完整传文件；同一配置/会话内会先成功后退化；
控制面可在 receiver 无内容时继续工作；最新温度没有明显上升；fixed 仍
断流；蓝牙 Error 持续存在；恢复、稳定速度与发现均没有完成验收。

**值得检验的假设，尚无定论：**

1. 蓝牙/共存或天线授权状态、初始化遗留状态参与后续退化。
   确实存在 `coex_wlan_only`、跨频段功率表与补偿代码；但具体物理
   天线/电源共享拓扑未核实，不能只凭同一 USB 设备就推定原因。
2. 802.11 ACK 或回程 TX 不可靠，让对端 TCP 退避；USB 提交成功不能排除。
3. TCP 接收窗口、重复/乱序、IPC/lwIP 丢弃或回程 ACK 停顿。
4. WinAWDL TCP 合并在新宿主 lwIP 上的行为需要单独验证。
   `-j 400` 原为面向 Windows ACK 行为的优化；它可能没问题，不能直接
   说“与 lwIP 不兼容”。历史正常速度时也不能凭空假设没有合并。
5. B 组发现问题可能在公告、IPv6 可达、回程或手机缓存等位置，尚未定位。

**不能继续当成已确定结论：**手机熄屏节流；RSSI 下降证明射频坏了；
只有物理拔插才能恢复；蓝牙 Error 肯定无影响/肯定是根因；Python 太慢；
CRC 比例相近证明手机主动少发；起步帧率高证明换台和 ACK 全部正常。

## 7. 下一位 AI 建议的排查顺序

### 第一步：先做正常与异常的源码/状态对比，避免再同时改多个变量

- 对照正常时间段、13:43 断流、13:47 蓝牙异常、`3037619`、`1db868f`、
  `d007eee` 的变化，明确哪些现象在蓝牙异常前已经存在。
- 只读核查 MI_00/MI_02 状态、现有初始化/共存/天线授权路径。优先确认
  状态恢复是否有缺口，不要为验证猜测就再换驱动或复位。
- 若安排蓝牙正常/异常对照，先写清如何安全恢复到正常状态；整机重启、
  USB 复位会改变的不止蓝牙，不能把恢复后的变化全归给蓝牙开关。

### 第二步：Q25 被动 TCP 诊断

- 在 awdld→netd IPC 输入、lwIP 输入/输出、本地 receiver 代理三个边界
  记录同一连接的载荷字节、TCP seq/ACK、通告窗口、重传/乱序迹象、
  pbuf/input 分配或投递失败；保留时间标记，不记录明文文件内容/身份令牌。
- 要区分“文件载荷停了但探测/ACK 还在”与“整个 RX/USB 没有活动”。
  也要区分 netd 已投递/已读与手机真的收到空口 ACK。
- 保持日志低频/计数式，先离线回放验证，不用逐包海量日志改变时序。
- `-j 0` 是现成关闭 TCP 合并的开关，可做单变量对照；当前监管参数
  仍硬编码 `-j 400`，**尚未新增可调 join 参数，也尚未跑关闭合并实验**。
  不要把“建议对照”写成已经完成。
- 一次只变一个因素，同一文件、位置、手机状态，记录启动重试和初始
  状态；失败保留，不连续叠加恢复。链路 ready 后再提示用户操作 iPhone。

### 后续而非当前优先项

Q20 连续成功与速度验收、Q7 假死恢复/24 h、驱动回滚、安全的蓝牙恢复、
固件再分发授权、签名、干净机器。不要先重写 Python、换硬件或继续做 GUI。
接收端离线约 228 MB/s，说明 TLS/zlib/cpio/磁盘这一段有余量；它不包含
lwIP/IPC/空口，不能用来排除整个接收链路。

## 8. 代码和参考实现导航

### 本仓库

- `src/radio_usb/main.cpp`：USB bring-up、RX/上传统计、radio_link、TX、
  SET_CHANNEL、完成确认失败时断链；`peer_telemetry.h` 与对应测试。
- `src/radio_usb/channel_completion_test.cpp`：完成等待的离线验证；
  被测头文件是补丁新增的 `build/devourer-airbridge/src/ChannelSwitchCompletion.h`，
  由 `patches/devourer-airbridge.patch` 跟踪，不在 radio 源目录。
- `patches/devourer-airbridge.patch`：GPL 无线库补丁。
- `src/awdl_netd/main.cpp`：IPv6 IPC、pbuf、lwIP netif 输入/输出。
- `src/awdl_netd/lwip_proxy.cpp`：lwIP socket ↔ 本地 TCP/UDP 代理。
- `src/awdl_netd/lwipopts.h`：MSS 1360、TCP_WND 240 KiB、RCV_SCALE 2、
  PBUF_POOL_SIZE 256、MEM_SIZE 512 KiB；这些是编译值，不是实测窗口。
- `src/supervisor/Program.cs`：启动参数/子进程环境、日志、看门狗、模式。
- `tools/analyze_session.py` / `tools/test_analyze_session.py`：速度、失败
  窗口关联；`--diagnose --json`，六项离线回归。时钟关联要求单次 <12 h。

### 锁定的开源参考（本轮没有联网更新）

- [OpenIPC/devourer](https://github.com/OpenIPC/devourer)：
  `f7e073175852eeb87ccab7bfa619c41156a1b26b`。
  原始研究副本在 `upstream/devourer`，构建用打补丁副本
  `build/devourer-airbridge`，由 `tools/Prepare-Devourer.ps1` 创建。
- [raph559/winawdl](https://github.com/raph559/winawdl)：
  `2196fb48b97cef1ee17ee1350c442972c4efeaf0`。
  `awdl/win/io_win.c`、`tcp_merge.c/.h`、`host_if_ipc.c` 是数据路径重点。
  `JOIN_MAX_HOLD_US=3000`；`joined` 在继续合并时加一。
- 实际构建的嵌套 OWL：`upstream/winawdl/upstream/owl`，
  `da255a70f221784c836d943dd3f243bc798f223b`，重点 `src/state.c`、
  `src/election.c`、`src/rx.c`、`src/channel.c`、`daemon/core.c`。
- 单独锁定的 [seemoo-lab/owl](https://github.com/seemoo-lab/owl)：
  `0ee3263f944ea721c355f826aa90d636d012a512`，**与嵌套构建版本不同**。
- [SwooshDrop](https://github.com/AlbinoPriest/swooshdrop)：
  `c0eaff15ab608766973333a710eb364699a9393e`；
  [OpenDrop](https://github.com/seemoo-lab/opendrop)：
  `11fe7ba7861093b302bc0637e8cb10adf2d29337`。只作参考，非当前接收路径。
- WinAWDL 原有 devourer 子模块 `8d936de2a3805ce126e5236349afe40e645aaf9b`
  不是当前 USB radio 所用的 OpenIPC 基线。

`docs/sources.lock.json` 锁原始 SHA；WinAWDL/OWL 本地应用了主仓库补丁，
不能只 checkout 原 SHA 就以为恢复了完整可运行版本，也不能 reset 掉本地补丁。
现有补丁包括 `awdl-ipc-host.patch`、`airdrop-identity.patch`、
`owl-airbridge.patch`、`devourer-airbridge.patch`，不得跨许可证直接混合实现。

可参考 devourer 的 `docs/experiments/dwell1-ab-injection.md`：它区分命令
提交和真实完成，要求独立见证正确频道。其 DUT/频段与本机不同，不能把
上游测试成功当成本机 AWDL/ACK 成功。8821C 接收方向硬件 BlockAck 缺口
见 `docs/RADIO_CONTRACT.md`，不要把能接收环境聚合帧当作可应答 BlockAck。

## 9. 证据、原始日志与文件位置

### 随 Git 提交的材料

- `evidence/transfer-970kbs.log`：历史 251/363/952/970/1121 KB/s。
- `evidence/awdl-sequence-and-fastretune.log`：13:43 断流与旧快切计时。
- `evidence/software-usb-reset.log`：蓝牙异常历史；只看记录，不复制执行。
- `evidence/research-transfer-2026-10-28.log`：14:32 会话三次验收。
- `evidence/stall-context-2026-10-28.json`：冻结旧会话哈希与分层计数。
- `evidence/fixed-channel-transfer-2026-10-28.json`：A 组成功/失败、哈希。
- `evidence/social-discovery-2026-10-28.json`：B 组用户观察、零请求与后端哈希。
- `evidence/chanseq-offline-2026-10-28.log`：监管模式的离线构建/子进程检查。
- `evidence/radio-telemetry-fix.log`、`evidence/channel-confirmation-offline.log`：
  Q18/Q19 编译、离线回归、补丁可复现证据。

### 只在这台机器上的原始材料（Git 忽略）

- 原日志 `dist/data/logs/20261028-143206/`、`20261028-150311/`、
  `20261028-151652/`。监管会保留最近 10 会话，**优先使用冻结副本**：
- `build/diagnostics/baseline-20261028/20261028-143206/`
- `build/diagnostics/fixed-20261028/20261028-150311/`
- `build/diagnostics/social-20261028/20261028-151652/`
- 每份含 supervisor/radio/bridge/netd/awdld/receiver 日志。原始日志含姓名、
  手机地址、传输标识和令牌，不能原样公开或混入外部交接内容。
- `build/soak/soak.csv` 为常驻采样；旧文档只报告约 1 小时且跨多次重启，
  不能作为 24 h 无泄漏验收。本次未重新分析完整 soak。
- `upstream/`、`build/`、`dist/` 都未纳入主仓库。**把本页单独给远程 AI
  不会同时给它二进制、上游副本或原始日志**；如需读取这些，必须提供
  工作区访问或另行脱敏材料，不能声称它已拥有完整抓包。

## 10. 运行、构建与验证入口

下面命令以项目根目录 PowerShell 为工作目录。交接时不自动执行实验。

### 无线无关的检查

```powershell
git status --short
git log -8 --oneline
python tools/check_project.py
python -m unittest discover -s tools -p test_analyze_session.py -v
.\tools\Check-Binding.ps1
```

### 分析已有冻结会话（不启动链路）

```powershell
python tools/analyze_session.py build/diagnostics/baseline-20261028/20261028-143206 --diagnose --json
python tools/analyze_session.py build/diagnostics/fixed-20261028/20261028-150311 --diagnose --json
python tools/analyze_session.py build/diagnostics/social-20261028/20261028-151652 --json
```

### 构建（不等于硬件/传输验证）

```powershell
.\tools\Build-Supervisor.ps1 -Test
.\tools\Build-Radio.ps1
.\tools\Apply-AwdlPatches.ps1
.\tools\Build-Awdl.ps1
```

- CMake 路径：`D:\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`。
  MSVC 19.44 / Windows SDK 10.0.26100，依赖位于 `build/deps/vcpkg`。
- `Build-Radio` 先准备锁定 devourer 副本，再构建 radio/桥并跑
  peer_telemetry/channel_completion 两项离线测试。
- `Build-Supervisor -Test` 会重编译监管并运行仅启动 PowerShell 的环境
  继承检查；不是 radio smoke。构建时先确认旧 exe 未在运行。
- netd 已有 `build/awdl_netd` 配置，可用上述 CMake
  `--build build/awdl_netd --config Release`；完整依赖见构建文档与 CMakeLists。
- `Package-Headless.ps1` + `Build-Sfx.ps1` 可重新打包，但可能替换暂存包。
  先保护现有后端基线与 data，当前不需要为排障重打包。

### 仅在准备好记录新实验并提示用户后启动

保留原后端的最新监管入口（**会初始化并发 AWDL 帧**）：

```powershell
.\build\supervisor\Release\AirBridge32.exe --root "J:\New soft project\DropBridge\dist\app\20261028-143130" --data "J:\New soft project\DropBridge\dist\data" --chanseq fixed
```

改 social 只改变最后的模式。启动前确认无旧链路/端口占用；不要同时启动
外层 exe。可先加 `--check --out <本地文件>` 校验路径，`--check` 不启动。
新监管曾验证 SHA-256：

```text
A610FFA4A283ADBD9E2C04DBE90F4110D0BAB8216E444127033345D8159AEEBE
```

它在 `build/supervisor/Release/AirBridge32.exe`；重新编译后应重新记录哈希。
原 14:32 实验外层包 SHA 在 `evidence/research-transfer-2026-10-28.log`，
radio/awdld/netd/bridge/receiver 的相对路径哈希在 B 组 evidence JSON。

## 11. 提交、验收与接手清单

最近关键提交：

- `3037619`：social + 固件快切；`271faf4`：软件 USB 复位/蓝牙副作用。
- `1db868f`：统计/监测修复；`d007eee`：真实换台确认与跨频段补偿。
- `0c2f028`：三次真实传输未达标；`cf3bbb3`：失败会话分层诊断。
- `0741be7`：fixed/social 模式和继承环境隔离。
- `991d2f4`：A 组断流、B 组发现失败、接收空窗诊断与证据归档。

已验证：六项 Python 日志分析回归；监管编译、真实 PowerShell 子进程
环境继承检查、两种模式 `--check`、非法模式启动前退出；早前 radio 两项
C++ 离线回归、干净副本补丁复现。文档检查、diff 检查、三份冻结日志
清单哈希核对通过。不要把这些通过写成 Q20/真机稳定性通过。

Q20 仍进行中且未达标；Q22–Q24 是诊断/实验记录完成；Q25 被动 TCP
观测未实现。当前只需接手排障，无需重建整个项目。

最终目标仍是稳定手机体验优先于速度：连续 20 次零人工干预、24 h 常驻、
假死恢复等尚未通过；FINAL_PLAN 的 2.5–3.5 MB/s 是目标而非当前能力。
临时“连续三次约 29 MB ≥700 KB/s”的诊断门槛也没有通过。

发布遗留：Realtek 固件再分发授权、GPL 对应源码/分发义务、代码签名、
干净 Windows 11 机器验收、WinUSB 回滚演练。不要通过换语言/打包来
替代空口、TCP 和恢复证据，也不要因发现图标或进程 ready 就宣称成功。

给下一位 AI 的第一条工作指令：

> 请先核对本地提交和这份交接，对照正常/异常时间段，审查蓝牙异常与
> 共存状态的关系，并设计最小的 TCP 载荷/ACK/窗口观测。明确哪些只是
> 假设，再决定一个单变量实验。保留历史日志与后端基线；不自动复位、
> 换驱动、改网络或推送。每项独立改动验证后更新工作队列并本地中文提交。
