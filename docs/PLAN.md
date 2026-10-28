# 开发计划与验收

当前交付口径以 [FINAL_PLAN.md](FINAL_PLAN.md) 为准：删除 Dart/Flutter 壳、改为单文件自解压 + 原生托盘监管、Python 接收端保留、速度按“时隙 → ACK → USB → mDNS”四条实测线推进。

## 架构决定

目标数据流：iPhone 原生 AirDrop → RTL8821CU → WinUSB/libusb → devourer C8821C → USB radio 进程 → radio_link v3 命名管道 → WinAWDL/OWL Windows AWDL 与 IPv6 → AirDrop 接收/用户确认 → 安全保存文件。

这是一条待验证的目标路线；当前有研究资料、隔离的 Windows 构建产物和只读枚举工具，无线层尚未上硬件验证。无线层优先 C++/C，上层先复用，不提前决定重写为单一语言或系统服务。首轮不用数据库、GUI、安装器。

交付形态（2026-10-27 决定）：用户侧是**一个程序**——一个安装包、一个托盘或后台入口；实现上允许 2～3 个进程：GPL-2.0 的 USB 无线进程、GPL-3.0 的 AWDL 进程、MIT 的应用层，用 MIT 的 `radio_link` 管道通信。**不承诺单进程单 exe**：devourer 是 GPL-2.0-only，OWL/WinAWDL 的 AWDL 层是 GPL-3.0-or-later，二者不能链接成一个作品。

实现语言（2026-10-27 决定）：**Dart/Flutter 负责应用层与 AWDL/AirDrop 逻辑；C++ `radio.exe` 保留 devourer/libusb 无线后端**。Dart 不直接访问 USB；通过命名管道使用 `radio_link` v3。AWDL 的目标实现是 Dart；Phase 3 可先用可复用的 C++ AWDL 作对照，再决定迁移范围。详见 [ARCHITECTURE.md](ARCHITECTURE.md)。

**该条已被 2026-10-28 的决定取代**：Dart/Flutter 壳已删除（`app/` 移除），应用层与托盘改用系统自带 .NET Framework 4.8 的 C#；AWDL 继续用 OWL/awdld（GPL-3.0），接收端保留 Python。现行交付形态见 [FINAL_PLAN.md](FINAL_PLAN.md)。

## Phase 0：补齐进入实验的条件（进行中）

已完成：源任务书归档、五个仓库固定版本、专用分支检查、关键调用链审计、初次本地配置尝试、Git 约定；隔离依赖下的 C8821C-only Windows 构建与 74 项无硬件自检（含 3 项 OpenSSL 加密自检）；目标网卡身份（`0bda:c820`，MI_02 已绑 WinUSB，libusb 可 open/claim，芯片 ID 只读确认 `0x09`）；WinAWDL ↔ devourer 契约对照；许可证矩阵；Zadig 绑定手册与只读 USB 探针。

未完成任务：
1. WinUSB 回滚演练：把 MI_02 恢复到 `netrtwlanu.inf`，确认 `WLAN 2` 适配器回来，再记录一次。
2. 首次 `Init`（写芯片、下载固件）仍需单独授权；它是 Phase 1 的入口。

退出条件：真实设备身份有记录（含芯片 ID，已达成）；Windows 构建通过（已达成）；最小实验和驱动恢复路径可执行（最小实验已做，回滚待演练）。

## Phase 1：独立 USB 无线后端

前置：只读身份已记录（`0bda:c820`，MI_02 为厂商接口，现有驱动 `netrtwlanu.inf`）；绑定只针对 MI_02，保留 MI_00 蓝牙和有线/I226 上网。绑定前先跑 `build/usb_probe/Release/usb_probe.exe`，确认当前驱动下 open 失败的基线，再按 `docs/BINDING_RUNBOOK.md` 以管理员绑定。

进度（2026-10-27）：只读 open/claim、芯片 ID `0x09`、首次 bring-up（频道 6，固件下载、BB/RF 表、IQK、RX 使能，零发帧）已完成；被动 RX 在频道 6（15 s，500 FCS 正常）和频道 36（12 s，4956 FCS 正常）各通过一次；2.4 GHz 空口 TX 由 ESP32-S3 见证，3/3 完整 reset 轮次各收到 20/20 唯一序号。完整 reset 是重复 TX 的必要条件（跳过 reset 曾出现 USB 提交成功但空中零帧）。下一步：反向验证、ACK/5 GHz 见证；TX 仍需单独授权。

顺序：只读 open/claim → 芯片 ID/初始化 → 固定频道有限 RX → 显式有限 TX → 空口见证与 TX report/ACK 关联 → 双向 unicast → 换频道 → 取消/退出/重开/拔插恢复。先用 `rxdemo` 验证初始化与 RX，再用 `txdemo` 发指定帧；每次发送前说明帧、频道和次数。

验收：每项至少三次重复记录；RX 有真实预期帧，TX 有独立见证，ACK 不以 USB 发送返回值代替；记录切换延迟分布和失败次数；退出无残留占用，目标设备可恢复普通驱动。失败先定位，不能以开启 monitor mode 代替通过。

## Phase 2：USB radio 适配

仅在 Phase 1 原始双向链路通过后开始。保持命名管道协议，替换 PCIe 打开和队列逻辑；实现真实换频道、地址和过滤语义。先单帧不聚合，提供有界队列、取消和错误报告。

已知缺口：8821C 没有接收方向的硬件 BlockAck（见 `docs/RADIO_CONTRACT.md`）。先关闭接收方向聚合，测量 iPhone 侧 ADDBA 重试与吞吐，再决定是否实现软件 BlockAck；不能默认聚合可用。

验收：无 regproxy 依赖的 USB 目标可构建；管道消息长度/断连处理通过；回放和硬件帧流可核对；修改范围限制在 radio 与必要构建脚本。协议扩展必须双方同时版本化。

## Phase 3：AWDL 与 IPv6

由用户手动在 iPhone 打开 AirDrop，记下 iOS 版本、操作时间、手机型号和测试环境；工具只记录用户标记，不推断手机状态。

验收：真实 AWDL 帧、邻居发现与同步证据，调度与频道实测，双向 IPv6 有效载荷，丢包/重传/超时有记录。界面发现不等于网络链路成功。

## Phase 4：真实接收闭环

复用应用层协议，先命令行接收及明确接受/拒绝，再做轻量常驻封装。文件保存需处理路径穿越、重名、空间不足、中断和部分文件；不自动执行或打开来件。

验收：iPhone 原生菜单发现并发送；Windows 用户确认；同一已知测试文件的实际共享字节、大小与 SHA-256 一致。避免用会被手机重新编码的照片作为首个严格哈希测试。至少小文件、大文件和连续多文件各三轮；保存原始日志与错误案例。

## Phase 5：性能与产品化

仅闭环通过后进行。用固定文件、固定位置和单变量设置比较，报告应用层 MB/s（明确定义 10^6 字节）、持续时间、CPU、工作集/私有内存、USB 效率、重试和失败率。5～10 MB/s 只是期望，无现有承诺。

先优化实测瓶颈，再决定聚合/队列/重试参数。最后完成驱动安装卸载、普通网络恢复、开机启动、托盘、断线恢复和发布许可清单；资源占用不靠“原生”推断。

## 工作方式与投入上限

每个可验证小改动完成后本地中文提交。硬件实验以一次一个变量组织，失败也保留记录。不要给 Phase 2～5 承诺日期；先用一次构建和一次硬件带入实验决定后续投入。若硬件无法完成必要原始帧收发、可靠 ACK 或频道控制，停止扩展并输出原因及复现实验，不直接购买替代设备。
