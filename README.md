# DropBridge / AirBridge32

Windows 11 原生 AirDrop 接收研究工程。目录名保持 DropBridge，AirBridge32 为任务书暂定产品名；没有 ESP32 固件要求。

**当前：研究工程和阶段计划已搭建；Phase 0 部分完成，硬件确认和 Windows USB 构建仍未通过。尚无可用 AirDrop 接收程序。**

目标是让 iPhone 系统原生隔空投送通过专用 RTL8821CU USB 网卡把文件交给 Windows，不依赖 WSL2、虚拟机或 iPhone 第三方应用。

## 阅读入口

- [原始任务书](docs/用户任务书.md)
- [源码审计及 Go / No-Go](docs/PHASE0_AUDIT.md)
- [阶段计划与验收](docs/PLAN.md)
- [版本锁定](docs/sources.lock.json)
- [工作记录及交接](WORK_LOG.md)
- [开发约定](AGENTS.md)

## 现在可运行的工具

在项目根目录的 PowerShell 执行：

```powershell
.\tools\Inspect-Hardware.ps1
python .\tools\check_project.py
```

硬件检查仅枚举当前 USB 设备，生成 evidence/usb-devices.local.json；包含设备实例信息，默认不提交。它不会初始化无线芯片、替换驱动或发射无线帧。若权限不足会明确报错，不能把错误当成没有设备。

上游源码位于 upstream/，不作为主仓库代码发布；恢复时按 sources.lock.json 中 URL 克隆并 checkout 对应完整 commit。WinAWDL 的无线依赖还需 `git -C upstream/winawdl submodule update --init upstream/devourer`，版本固定在 8d936de2a3805ce126e5236349afe40e645aaf9b。

## 已建立的证据

- 五个上游源码已下载并锁定 SHA；WinAWDL 专用 devourer 子模块也已取回。
- evidence/upstream-issues.json：近期 Issue/PR 摘要快照，每库最多八条，不代表全面审计全部历史。
- evidence/build-configure.log：实际 MSVC/CMake 配置失败记录，缺少 PkgConfig。
- 本机 USB 枚举已执行，未找到可确认的目标无线网卡；仍需接入目标设备后复查。

本轮不复制上游实现，不赋予第三方代码统一许可证。产品发布许可证将在复用范围确定后处理。
