# 绑定运行手册：把 MI_02 换成 WinUSB（Zadig）

状态：**已于 2026-10-27 14:21 (+08:00) 执行成功**。MI_02 → WinUSB（`oem155.inf`，libwdi 6.1.7600.16385），蓝牙 MI_00 未受影响，`WLAN 2` 适配器消失；libusb 已能 open 并 claim interface 2。回滚尚未演练。本页最初写给用户以管理员身份操作；AI 不会自动改驱动。

## 0. 目的

devourer 通过 libusb 直接访问网卡的 bulk 端点。Windows 当前把 MI_02 交给 Realtek 的 `netrtwlanu` 网络驱动，用户态程序拿不到这些端点。把 **MI_02 一个接口**绑定到 WinUSB 后：

- libusb 可以 `open` / `claim_interface` / 读写端点；
- 这张网卡不再作为普通 Wi-Fi 使用（符合预期）；
- 蓝牙 MI_00 和蓝牙网络不受影响；
- 有线 I226 继续上网。

## 1. 当前基线（2026-10-27 13:08 +08:00）

| 项 | 值 |
| --- | --- |
| 设备 | `USB\VID_0BDA&PID_C820`，复合设备，`usbccgp` |
| 接口 | MI_00 蓝牙；MI_02 Wi-Fi（class `ff/ff/ff`） |
| MI_02 驱动 | `netrtwlanu.inf`，1030.38.712.2019，提供者 Microsoft，服务 `RtlWlanu` |
| MI_02 状态 | ProblemCode 0；网络适配器 `WLAN 2`，Disconnected |
| 蓝牙 MI_00 | `oem154.inf`，1.5.1012.3，服务 `BTHUSB`，正常 |
| 连接位置 | `Port_#0003.Hub_#0006`（父 hub `05e3:0608`） |

**建议先做**：把网卡从外接 hub 换到主板直连 USB 口，再重新运行 `tools/Inspect-Hardware.ps1` 并更新基线。直连可以减少绑定/拔插实验中的枚举问题。

## 2. 准备工作

1. 确认有线网络（I226）正常，因为绑定后这张 USB 网卡不能上网。
2. 关闭可能占用网卡的程序（Realtek 控制面板、Wi-Fi 工具）。
3. 从官网下载 Zadig：<https://zadig.akeo.ie/>（开源项目 libwdi 的图形前端）。不要从第三方下载站取。
4. 记录将要绑定的硬件 ID：`USB\VID_0BDA&PID_C820&MI_02`。

## 3. 操作步骤（需要管理员）

1. 右键 `zadig.exe` → **以管理员身份运行**。
2. 菜单 `Options` → 勾选 **List All Devices**。
3. 顶部下拉框选择 **`0bda:c820` 的 Interface 2**。
   - 显示通常是 `0BDA C820`，接口号 2；
   - **不要选** Interface 0（蓝牙），**不要选**整个复合设备；
   - 选错接口可能让蓝牙失效，但可以回滚。
4. 确认右侧当前驱动显示为 `RtlWlanu` 或类似 Realtek 驱动。
5. 目标驱动选择 **WinUSB**（不要选 libusbK / libusb-win32；libusb 1.0 用 WinUSB 后端）。
6. 点击 **Replace Driver**，等待完成。
7. 如果弹出驱动签名/安全启动警告：**不要关闭 Secure Boot、不要关闭驱动签名强制**。先取消，把完整错误记下来，回到本项目讨论替代方案。

## 4. 绑定后验证（只读）

```powershell
.\tools\Check-Binding.ps1
```

期望输出：

- `MI_02` 的 service 为 `WinUSB`，驱动不再是 `RtlWlanu`；
- 蓝牙 MI_00 仍然正常；
- `WLAN 2` 网络适配器消失或不再由 Realtek 驱动管理；
- 脚本退出码 0。

再补充记录：

```powershell
Get-PnpDevice -PresentOnly | Where-Object InstanceId -like '*VID_0BDA&PID_C820*' |
    Select-Object Class,FriendlyName,InstanceId,Status
```

**本步骤不做**：不初始化芯片、不下载固件、不发帧。那些需要单独授权。

## 5. 回滚（恢复到 Realtek 驱动）

1. 设备管理器 → 找到绑定后的设备（可能在“通用串行总线设备”下，名称类似 `WinUSB Device` 或原 Wi-Fi 名称）。
2. 右键 → **卸载设备**；**不要**勾选“删除此设备的驱动程序软件”。
3. 菜单 `操作` → **扫描检测硬件改动**。
4. Windows 应从驱动库重新安装 `netrtwlanu.inf`；`WLAN 2` 适配器恢复。
5. 如果没恢复：拔插网卡，或重启；仍不行时用管理员 PowerShell：

   ```powershell
   pnputil /scan-devices
   ```

6. 再次运行 `tools/Check-Binding.ps1`，确认回到 `RtlWlanu`。

回滚不需要删除驱动包，也不需要改注册表。

## 6. 可能失败的情况

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| Zadig 报签名/证书错误 | 系统策略拒绝 libwdi 的驱动包 | 不要关 Secure Boot；记录错误，改用 INF + 测试签名等方案前先讨论 |
| 选错到 MI_00 | 蓝牙失效 | 按第 5 节卸载并扫描，蓝牙会恢复 |
| 绑定后设备变成未知设备 | INF 未匹配 | 卸载 → 扫描；必要时拔插 |
| 网卡枚举成 `0bda:1a2b` | ZeroCD 形态 | 不要自动 eject/切模式；先记录再决定 |
| 绑定成功但 libusb 打不开 | 可能是接口/端点或权限问题 | 先跑只读打开探针，不要直接 Init |

## 7. 需要记录的证据

写入 `evidence/binding.local.json`（已被 `*.local.json` 忽略，不提交）：

```text
UTC 时间：
Zadig 版本：
绑定前 MI_02 驱动：
绑定目标（硬件 ID）：
绑定后 MI_02 驱动/服务：
蓝牙 MI_00 状态：
WLAN 2 适配器状态：
是否回滚测试：
回滚后驱动：
失败时的完整错误文本：
```

之后我会把不含实例路径的摘要整理进 `evidence/hardware-identity.json` 或新的提交证据。

## 8. 边界

- 本步骤只换驱动，不初始化芯片、不加载固件、不发无线帧。
- 不改网络、防火墙、注册表或 Secure Boot。
- 所有操作可回滚；回滚以第 5 节为准。
