# AirBridge32 目标架构（2026-10-27 决定）

## 1. 交付形态

用户侧是**一个程序**：一个安装包、一个托盘/后台入口、一套设置。实现上允许进程分离，用户不需要知道。

```text
AirBridge32（Dart / Flutter 应用）
├─ 托盘、设置、接收确认、文件保存、日志
├─ AWDL 发现 / 同步 / 信道调度（目标：Dart）
├─ AirDrop 应用层（TLS / HTTP / 身份，目标：Dart）
└─ 命名管道 radio_link v3（MIT）
      └─ radio.exe（C++ / devourer，GPL-2.0）
            └─ 独占 RTL8821CU USB：初始化、固件、RX、TX、换信道
```

- **Dart 是“脑和脸”**：应用逻辑、协议状态机、用户交互。
- **C++ `radio.exe` 是“手”**：只做射频和 USB，不包含 UI，不解析 AWDL。
- 进程边界对用户不可见；安装器、托盘、开机启动由 Dart 应用统一管理。

## 2. 为什么无线层必须留在 C++

- devourer 是 C++ 库，没有 C ABI；Dart FFI 只能调用 C 函数，不能直接驱动 `IRadio`。
- 固件下载、PHY 表、寄存器时序、USB bulk 管线已经在 C++ 中实现并有 Windows 构建证据；用 Dart 重写等于重做驱动。
- ACK 由网卡硬件在微秒级完成，语言无关；但驱动管线必须留在原生层。
- 结论：`radio.exe` 保持 C++；Dart 通过管道使用它。

如果以后确实需要 Dart 直接调用，可加一层 `extern "C"` 的 `libairbridge_radio.dll` + Dart FFI。**不作为第一阶段方案**：它增加 GPL 链接边界、崩溃耦合和回调复杂度，收益有限。

## 3. 进程职责

### Dart 应用

- 启动/停止/重启 `radio.exe`，处理异常退出与退避重连。
- 实现 `radio_link` v3 客户端：HELLO、RX、TX、SET_CHANNEL、SET_BSSID_FILTER、SET_ADDRESS。
- 维护 AWDL 状态机与 AirDrop 会话；文件保存必须处理路径穿越、重名、空间不足、中断和部分文件。
- 不直接访问 USB；不要求管理员权限（驱动绑定是安装阶段的一次性动作）。

### `radio.exe`（C++）

- 通过 libusb/WinUSB 独占 MI_02；初始化、下载固件、固定/切换信道。
- 原始 802.11 RX/TX；`SetStationIdentity`、`SetAmpduMode`、`GetTxStats` 按需暴露。
- 单一适配器、单一客户端；日志有界；退出顺序为停 RX → 取消 transfer → 释放 radio → 释放 USB。
- 协议扩展（例如 TX status 查询）必须双端版本化，不能偷偷改线格式。

### AWDL 层

- **目标**：Dart 实现 AWDL 发现、同步、信道调度和 AirDrop 应用层。
- **验证阶段可以先复用** WinAWDL 的 C++ `awdl` 进程作为对照，以便尽快回答“iPhone 能不能发现 Windows”；是否整体迁移到 Dart，在 Phase 3 用实测差距决定，不把重写当作进入手机测试的前置条件。
- 复用时不改 `radio_link` 线格式；AWDL 进程与 `radio.exe` 通过同一条管道交互。

## 4. 接口与错误处理

- `radio_link` v3：8 字节小端头 + 载荷，最大 16384 字节；TX/RX 载荷为 radiotap + 802.11。
- Dart 侧只包含 MIT 的 `radio_link.h` 等价定义；不引入 `radio` 目录的 GPL 代码。
- `radio.exe` 退出码区分：设备缺失、初始化失败、管道被占用、USB 错误。
- Dart 应用负责重启策略：设备重枚举后重新打开；连续失败进入可见错误状态，不静默。
- 日志：机器事件 JSONL（stdout）+ 人类诊断（stderr），与 devourer 现有约定一致。

## 5. 许可证边界

- `radio.exe` 链接 devourer（GPL-2.0），按 GPL-2.0-only 分发，附对应源码与构建脚本。
- 复用 WinAWDL `awdl` 时它是 GPL-3.0-or-later，保持独立进程。
- Dart 应用不链接 GPL 库；只通过 MIT 管道协议通信，避免把应用层变成 GPL 作品。
- Realtek 固件再分发仍未解决；发布前必须处理（见 `docs/LICENSE_MATRIX.md`）。

## 6. 分阶段

| 阶段 | 交付 | 验收 |
| --- | --- | --- |
| Phase 2 | USB radio 后端（`radio.exe`） | 无 regproxy 依赖；管道消息长度/断连；回放与硬件帧可核对 |
| Phase 3 | AWDL/AirDrop 接入 | iPhone 原生 AirDrop 发现；双向 IPv6；丢包/重试有记录 |
| Phase 4 | Dart 应用接收闭环 | 用户确认、文件保存、哈希一致 |
| Phase 5 | 打包与常驻 | 安装/卸载、驱动恢复、开机启动、资源测量 |

## 7. 当前未决

- Dart 重写 AWDL 的规模与收益：Phase 3 决策点。
- Flutter 还是纯 Dart 做托盘/UI：Phase 4 前决定。
- 5 GHz 与硬件 ACK：仍是 Phase 1/2 的实测项。
- 单安装包如何携带 GPL 源码与固件授权：Phase 5 前解决。
