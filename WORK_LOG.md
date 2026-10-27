# 工作记录 / 下一位 Agent 交接

## 2026-10-27：研究工程建立

用户要求规划并搭建项目，先做 Phase 0；每次完成改动后本地中文 Git 提交。

完成：任务书完整归档；五个公开上游克隆到 upstream/ 并锁版本；WinAWDL 的专用 devourer 子模块已克隆；审计报告、阶段计划、AGENTS.md、只读 USB 枚举脚本和工程检查脚本已建立。

实测：VSBuildTools 位于 D:/VSBuildTools；CMake 识别 MSVC x64 后缺 PkgConfig，日志 evidence/build-configure.log。没有编译成功或运行无线测试。USB PnP 只读枚举成功，但未找到可确认的 RTL8821CU；本地设备实例结果被 .gitignore 排除。

发现：WinAWDL radio 与 PCIe transport/tx_kick 耦合；运行中换频道被忽略；OpenIPC devourer 有 C8821C USB 真实实现，但不能据此声称用户网卡 Windows AirDrop 通过。SwooshDrop 依赖 WSL2，不适用最终运行路线。

边界：没有运行上游安装器/硬件脚本，没有改网络或驱动，没有加载固件、发射帧或复制第三方实现进入项目。主仓库无远程推送。

接续顺序：README → PHASE0_AUDIT → PLAN → sources.lock.json。先确认接入的目标设备，补齐本地构建依赖，再申请针对具体网卡的绑定实验。Phase 0 尚未全部完成，不能把本轮文档交付当成 AirDrop 可用。

验证命令：python tools/check_project.py；tools/Inspect-Hardware.ps1；git diff --check；git status --short。
