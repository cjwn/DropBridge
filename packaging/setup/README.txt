首次使用：把网卡的 Wi-Fi 接口绑定到 WinUSB
==========================================

每台电脑只做一次。做完之后就不用再管了，除非你换 USB 口/换电脑。

最简单的做法（推荐）
--------------------
直接双击外面的 `DropBridge.exe`：

  - 如果网卡已经绑好，它直接就起来了，什么都不用做；
  - 如果还没绑好，它会弹一次提示。点「是」，再在 Windows 的管理员确认里点「是」，
    程序会自己导入证书、装好 WinUSB 驱动、然后继续启动。**不需要打开 Zadig。**
  - 如果自动安装失败，它会告诉你日志位置（`data\logs\driver-install.txt`），
    这时再用下面的 Zadig 手动兜底。

为什么要装驱动
--------------
这个程序绕开 Windows 自带驱动，直接用 libusb 跟 RTL8821CU 网卡说话，才能收发
AWDL（隔空投送）帧。Windows 默认给这个网卡的 Wi-Fi 接口装的是 Realtek 网络驱动，
libusb 打不开它，所以第一次要换成 WinUSB。驱动本身是 Windows 自带的 winusb.sys，
我们只是把「这个接口用 WinUSB」写进注册表一样的驱动库。

⚠️ 只换一个接口
---------------
你的网卡是一个「复合设备」，有三个接口：

  MI_00   蓝牙        ← 绝对不要动（这个程序不用它）
  MI_01   厂商预留    ← 不要动
  MI_02   Wi-Fi       ← **只换这一个**

自动安装只会碰 MI_02；手动用 Zadig 时也请只选 MI_02。

手动兜底（自动安装失败时）
--------------------------
1. 拔掉再插上网卡（直插机箱后面板，别用前面板或 USB hub）。
2. 双击本文件夹里的 `Bind-WinUSB.cmd`，按提示允许管理员权限。
   - 它说「已经绑好了」就不用往下做；
   - 它说没绑好，会打开 Zadig。
3. 在 Zadig 里：
   a) 菜单 `Options` → 勾上 `List All Devices`；
   b) 上方下拉框里找 **`RTL8821CU` / `802.11ac NIC`**（USB ID `0BDA C820`，接口 `MI_02`）；
      **不要选** `USB Composite Device`，**不要选**带 `Bluetooth` 的项；
   c) 目标驱动确认是 **`WinUSB`**；
   d) 点 **`Replace Driver`**，等 1–2 分钟，中途别拔网卡。
4. 回到脚本窗口按回车，它会复查：
   - `OK: MI_02 is bound to WinUSB` → 成功，双击外面的 `DropBridge.exe`；
   - `FAILED` → 重新插拔再跑一次，仍失败请把窗口文字和 `setup.log` 发出来。

怎么恢复原驱动（后悔药）
------------------------
设备管理器 → 找到 `RTL8821CU` / `802.11ac NIC` → 右键 → 卸载设备 →
勾「删除此设备的驱动程序软件」→ 确定 → 拔插网卡 → Windows 自动装回 Realtek
网络驱动。之后 Wi-Fi 恢复正常，DropBridge 不再能用。

关于这里的文件
--------------
driver\airbridge-winusb.inf / .cat / dropbridge-driver.cer
  程序自动安装用的驱动包。`.cat` 是用 `dropbridge-driver.cer` 对应的自签证书
  签名的目录文件，安装时会把证书导入「受信任的根」和「受信任的发布者」。
zadig-2.9.exe
  第三方工具（Pete Batard，GPLv3），只在手动兜底时用。许可证见
  `licenses\zadig-GPL-3.0.txt`。它能替换任意 USB 设备的驱动，**请不要用它动
  别的设备**（键盘、鼠标、U 盘、摄像头），否则那个设备会坏掉。
