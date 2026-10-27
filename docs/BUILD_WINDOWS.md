# Windows 构建记录：devourer RTL8811CU/8821CU（Phase 0 任务 2）

状态：**通过**（2026-10-27，本机）。结论：Windows 11 + MSVC 可以构建 C8821C-only 的 devourer 静态库、`rxdemo`、`txdemo`，并通过 71 项无硬件自检；**尚未接入网卡，未做任何硬件测试**。

## 1. 环境

| 项 | 值 |
| --- | --- |
| 系统 | Windows 11 x64（10.0.22631） |
| 编译器 | MSVC 19.44.35228.0（D:/VSBuildTools，x64） |
| Windows SDK | 10.0.26100.0 |
| CMake | 3.31.6-msvc6 |
| 生成器 | Visual Studio 17 2022 |
| 依赖管理 | vcpkg 2026-09-26（隔离在 `build/deps/vcpkg`） |
| libusb | 1.0.30（`x64-windows`，动态库） |
| pkgconf | 3.0.3 |
| 源码 | OpenIPC/devourer `f7e073175852eeb87ccab7bfa619c41156a1b26b` |
| 构建时间（UTC） | 2026-10-27T04:40Z |

## 2. 依赖隔离

依赖全部放在被 `.gitignore` 排除的 `build/deps/` 下，没有执行 `vcpkg integrate install`，没有改 PATH，没有改驱动或网络。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Setup-BuildDeps.ps1
```

该脚本做三件事：浅克隆 vcpkg、bootstrap、安装 `libusb:x64-windows` 与 `pkgconf:x64-windows`。

## 3. 配置

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build-Devourer.ps1
```

等价的核心参数：

```text
-S upstream/devourer -B build/devourer-8821c -G "Visual Studio 17 2022" -A x64
-DCMAKE_TOOLCHAIN_FILE=build/deps/vcpkg/scripts/buildsystems/vcpkg.cmake
-DVCPKG_TARGET_TRIPLET=x64-windows
-DPKG_CONFIG_EXECUTABLE=<...>/pkgconf.exe
-DDEVOURER_JAGUAR2_8821C=ON
其余 DEVOURER_JAGUAR1/8814/JAGUAR2_8822B/JAGUAR3_*/8733B/KESTREL_* 全部 OFF
```

两个关键点：

1. **必须让 pkgconf 输出 MSVC 语法。** CMake 的 `FindPkgConfig` 不会自动加 `--msvc-syntax`；不加时 `-L... -llibusb-1.0` 无法被 MSVC 链接器使用。构建脚本设置 `PKG_CONFIG_ARGN=--msvc-syntax`，pkgconf 输出 `/libpath:... libusb-1.0.lib`。
2. **路径含空格可用。** 项目位于 `J:\New soft project\DropBridge`，pkgconf 的转义由 CMake 的 `separate_arguments` 还原，配置与构建均通过。

## 4. 结果

### 产物（`build/devourer-8821c/Release/`）

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `devourer.lib` | 4093620 | `DAF9E9FB950F5A790760CE4156BAB4F558088F37AE53478B9E2299571CA84828` |
| `rxdemo.exe` | 745472 | `00C497B40C2E5F4A9E181E7016735DCA342A6D8D7C6BF1706E6A93C4132D3032` |
| `txdemo.exe` | 765952 | `E019A277AFA65AFA0FC2866ED92A7A6551A00F9517737B7B7381BCF4A9FB6FEB` |
| `libusb-1.0.dll` | 166400 | `2BF9787665FD31E9394F3DCC87CC144CF932BAFD135204A990F16A1749FA5BFA` |

所有目标（库、demos、自检程序）编译成功，退出码 0。

### 无硬件自检（ctest）

```text
100% tests passed, 0 tests failed out of 71
Total Test time (real) = 3.18 sec
```

3 项跳过，均与 8821C 无关：

- `la_csi_math`：依赖未安装的 Python 数值环境；
- `mt7612u_usb_ids_vs_mt76`、`mt7612u_initvals_generated`：MT7612U 支持默认关闭。

### 日志

| 文件 | 内容 |
| --- | --- |
| `evidence/build-8821c-configure.log` | 配置输出（含 PkgConfig/libusb 版本识别） |
| `evidence/build-8821c-build.log` | 编译输出与警告 |
| `evidence/build-8821c-ctest.log` | 71 项自检结果 |
| `evidence/build-configure.log` | 上一轮的失败记录，保留作历史 |

### 已知警告

编译期大量 `C4819`（源文件含非当前代码页 936 的字符）。这是上游源码里的非 ASCII 注释在中文系统上的提示，不影响产物；没有改上游文件去消除它。

## 5. 未覆盖 / 不能据此宣称

- **没有硬件证据**：没有打开设备、没有下载固件、没有收发包、没有测吞吐。
- **没有 OpenSSL**：`supplicant`、`station_sm`、`ccmp_framing`、`sta_client_headless` 四项加密自检未构建。需要时在 `build/deps` 内安装 `openssl:x64-windows` 后重配。
- **没有构建 WinAWDL `radio`**：它当前只配置 8852C + PCIe/regproxy；8821C USB 版 radio 尚未编写，见 `docs/RADIO_CONTRACT.md`。
- **没有验证 Windows 设备绑定**：WinUSB/libusb 还需要目标接口、INF 和恢复方案，见 `docs/HARDWARE_INTAKE.md`。

## 6. 下一步

1. 接入网卡，记录唯一 VID/PID、接口、现有驱动（`docs/HARDWARE_INTAKE.md`）。
2. 在获得授权后做最小驱动绑定实验，先只读枚举，再 `Init`/RX，最后有限 TX。
3. Phase 1 的通过标准仍是 `docs/PLAN.md` 中的三次重复、独立见证、TX report 关联，而不是本页的编译成功。
