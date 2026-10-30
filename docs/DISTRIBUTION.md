# 分发形态：一个文件夹 + 压缩包（托盘常驻）

2026-10-29。对应队列 Q44。目标：**整个程序在一个文件夹里，打包成压缩包发出去，
解压双击就常驻任务栏托盘**，不在系统任何位置留下文件。

## 1. 产物

```text
dist\DropBridge\                 ← 解压后就是这个文件夹（333 个文件，36.5 MB）
  DropBridge.exe                 ← 唯一入口：托盘监管（winexe，无控制台窗口）
  backend\
    airbridge_radio.exe           RTL8821CU 驱动进程（GPL-2.0）
    radio_pipe_bridge.exe         管道桥（MIT）
    awdl_netd.exe                 lwIP/代理（含 Q38 的输入背压）
    awdld.exe                     AWDL 控制面（GPL-3.0）
    python\                       embeddable Python + cryptography
    airdrop\                      receiver.py 等
    libusb-1.0.dll、msvcp140 等
  tools\Start-Chain.ps1           控制台调试入口（默认 dry-run）
  licenses\                       上游许可证原文
  README.txt                      给用户看的说明（中文）
  VERSION.txt                     构建时间、提交、各后端哈希
dist\DropBridge-portable.zip     压缩包（16.1 MB）
```

运行后**在同一个文件夹里**生成 `data\`：

```text
data\keys\          TLS 自签证书 + AirDrop 实例 ID
data\received\      收到的文件
data\logs\<会话>\   各进程日志
data\awdl_netd.addr
```

## 2. 与旧形态的区别

| | 旧：单文件 exe | 新：文件夹 + zip |
| --- | --- | --- |
| 入口 | 自解压 stub → `app\<时间戳>\backend\DropBridge.exe` | 根目录的 `DropBridge.exe` 直接就是托盘程序 |
| 首次启动 | 先解压（约 0.4 s / 333 文件） | 无解压步骤 |
| 升级 | 换外层 exe，内部按时间戳保留最近 2 个构建 | 覆盖 exe 与 `backend\`，保留 `data\` |
| 分发 | 单文件 exe | 压缩包（也可只发文件夹） |

两种形态仍然都能用：`dist\DropBridge.exe` 是单文件版，`dist\DropBridge-portable.zip`
是文件夹版。二者共用同一套 `data\` 布局，**不要把两者的 data 混用**（各自在各自目录里）。

## 3. 代码改动（为文件夹形态）

1. `src/supervisor/Program.cs` → `ResolveRoot`：exe 所在目录若含 `backend\`，就以它
   为包根；否则维持“exe 在 `backend\` 里、根是父目录”的旧逻辑。这样双击根目录
   exe 不需要任何参数。
2. 同一文件的 `AutostartValue`：文件夹形态下开机自启直接注册根目录 exe 自身；
   自解压形态仍注册外层 stub（升级后路径不变）。
3. 托盘图标：`assets\dropbridge.ico` 由 `tools/make_icon.py` 生成，编译时用
   `/win32icon:` 嵌入，运行时 `Icon.ExtractAssociatedIcon` 取用，取不到就回退系统图标。
4. 新增 `tools/Package-Distributable.ps1`：可重复地生成上面的文件夹与 zip（含
   `README.txt`、`VERSION.txt`、许可证、Python 的 `._pth` 修正）；拒绝在非 `dist\`
   路径上删除，拒绝覆盖含 `data\` 的目录。

## 4. 本机验证（2026-10-29 00:36–00:41）

| 项目 | 结果 |
| --- | --- |
| 打包 | `dist\DropBridge` 333 文件 36.5 MB；zip `16,852,043 B`（`bf…`） |
| 干净解压 | 解压到 `build\diagnostics\dist-check\` 后，根目录只有预期的四类内容 |
| 默认根解析 | 无参数运行 `DropBridge.exe --check` → exit 0、无 MISSING，计划里的路径全部指向解压目录 |
| 托盘常驻 | `--auto-exit 150`：00:38:18 启动，00:39:01 就绪（43 s），49876/49877/8771 与 UDP 15353 全部在听 |
| 单实例 | 第二个实例立即 exit 0，进程数仍为 1，没有第二条链路 |
| 干净退出 | 00:40:49 自动退出；无残留进程、四个端口全空 |
| 数据位置 | `data\keys`、`data\received`、`data\logs`、`awdl_netd.addr` 全在文件夹内；`%LOCALAPPDATA%\AirBridge32` **不存在** |

注意：新文件夹第一次运行会生成**新的** AirDrop 实例 ID（本次 `2c49cdfffeec`），
这是预期行为——实例 ID 跟着 `data\keys` 走，升级时保留 `data\` 就不会变。

### 用这个成品做的真机验证

| 日期 | 文件 | 结果 |
| --- | --- | --- |
| 2026-10-29 00:55 | `IMG_0333.PNG` 1,207,111 B | **1.23 s / 0.98 MB/s**，落盘一致、无 408（满足 <1 MB ≤2 s 那条） |
| 2026-10-29 01:02 | `7545-11.MP4` 29,456,959 B | **26.84 s / 1.0975 MB/s**，落盘 SHA-256 `5d057f9c…` 一致、`input_fail=0`、手机 787 帧/s、CRC 15.1% |

即：**分发出去的这个文件夹，用默认配置（social + 合并）能跑到 1.10 MB/s**，
与历史最好 1.12 MB/s 相差 2% 以内。证据见
[shipped-folder-video-2026-10-29.log](../evidence/shipped-folder-video-2026-10-29.log)。

## 5. 已知限制（分发前必须知道）

- 未做代码签名：SmartScreen 会提示“未知发布者”。
- `backend\airbridge_radio.exe` 静态链接 Realtek 固件，**对外分发前需要解决再分发许可**
  （取得授权 / 由用户自备 / 只发源码，三选一）。当前仅供内部测试。
- 干净 Windows 11 VM、卸载残留检查、24 h 常驻、固件许可以及 3 次中位数验收都还没做。
- 首次使用仍需一次性把网卡 MI_02 绑成 WinUSB（Zadig，管理员），
  这一步不在包内，见 [BINDING_RUNBOOK.md](BINDING_RUNBOOK.md)。

## 6. 重新构建

```powershell
powershell -File tools\Build-Radio.ps1
powershell -File tools\Build-Awdl.ps1
powershell -File tools\Build-Supervisor.ps1        # 需要 assets\dropbridge.ico
powershell -File tools\Build-Python.ps1            # 若还没准备 build\python-embed
powershell -File tools\Package-Distributable.ps1   # → dist\DropBridge + zip
powershell -File tools\Build-Sfx.ps1 -SkipPayload  # 可选：单文件 exe 形态
```

`tools\make_icon.py` 只在需要改图标时运行（依赖 Pillow，产物 `assets\dropbridge.ico`
已提交，构建不依赖 Pillow）。
